/*
 *    Copyright 2026 Michael Behrens
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xml_zip_format.h"
#include "xml_zip_format_p.h"

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <quazip/JlCompress.h>
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>
#include <quazip/quazipnewinfo.h>

#include "core/map.h"
#include "core/map_view.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_directory_format.h"
#include "fileformats/xml_directory_format_p.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"


/* The archive stores the map folder's member files (and "templates/<name>")
 * directly at the archive root, as entries named by their relative path. */

namespace OpenOrienteering {

namespace {

/**
 * Returns the XML namespace used by the map data of the XML formats.
 *
 * This must match the namespace used by the directory and XML file formats.
 */
QString mapperNamespace()
{
	return QStringLiteral("http://openorienteering.org/apps/mapper/xml/v2");
}

}  // namespace


// ### XMLZipFormat definition ###

XMLZipFormat::XMLZipFormat() : FileFormat(
	MapFile,
	"XMLZip",
	::OpenOrienteering::ImportExport::tr("OpenOrienteering Mapper Map (zip)"),
	QString::fromLatin1("omapzip"),
	Feature::FileOpen | Feature::FileSave | Feature::FileSaveAs) {}


FileFormat::ImportSupportAssumption XMLZipFormat::understands(const char* buffer, int size) const
{
	// A zip archive can be recognized by its local file header magic ("PK\3\4").
	if (size >= 4 &&
	    buffer[0] == 'P' && buffer[1] == 'K' &&
	    buffer[2] == '\x03' && buffer[3] == '\x04')
		return FileFormat::FullySupported;
	return FileFormat::Unknown;
}


std::unique_ptr<Importer> XMLZipFormat::makeImporter(const QString& path, Map* map, MapView* view) const
{
	return std::make_unique<XMLZipImporter>(path, map, view);
}


std::unique_ptr<Exporter> XMLZipFormat::makeExporter(const QString& path, const Map* map, const MapView* view) const
{
	return std::make_unique<XMLZipExporter>(path, map, view);
}


// ### XMLZipExporter definition ###

XMLZipExporter::XMLZipExporter(const QString& path, const Map* map, const MapView* view)
 : XMLDirectoryExporter(path, map, view)
{
	// The zip output is not meant to be read by humans, so minified XML is used.
	setOption(QString::fromLatin1("autoFormatting"), false);
}


XMLZipExporter::~XMLZipExporter() = default;


bool XMLZipExporter::exportImplementation()
{
	auto const path_info = QFileInfo(path);
	auto const parent_dir = path_info.absolutePath();
	auto const target = path_info.absoluteFilePath();

	// Write to a temp file first, then move it into place.
	QTemporaryDir tmp_dir;
	if (!tmp_dir.isValid())
		throw FileFormatException(tr("Cannot create a temporary directory for saving the map."));
	auto const tmp_path = QDir(tmp_dir.path()).filePath(QStringLiteral("map.omapzip"));
	tmp_dir.setAutoRemove(false);

	zip = std::make_unique<QuaZip>(tmp_path);
	if (!zip->open(QuaZip::mdCreate))
	{
		addWarning(tr("Cannot create the zip archive\n%1:\n%2").arg(tmp_path, zip->getZipError()));
		zip.reset();
		return false;
	}

	output_path = tmp_dir.path();

	XMLFileFormat::active_version = XMLFileFormat::current_version;
	xml.setAutoFormatting(option(QString::fromLatin1("autoFormatting")).toBool());

	// Write all member files into the archive.
	exportAllFiles();

	zip->close();
	zip.reset();

	// Move the temporary archive into place (replacing an existing file).
	QString backup;
	if (QFile::exists(target))
	{
		backup = QDir(parent_dir).filePath(QStringLiteral(".map.omapzip.old"));
		QFile::remove(backup);
		if (!QFile::rename(target, backup))
			throw FileFormatException(tr("Cannot replace the existing map file\n%1:\n%2").arg(target, tr("The existing file could not be renamed.")));
	}
	if (!QFile::rename(tmp_path, target))
	{
		if (!backup.isEmpty())
			QFile::rename(backup, target);
		throw FileFormatException(tr("Cannot create map file\n%1:\n%2").arg(target, tr("The temporary file could not be moved to its final location.")));
	}
	if (!backup.isEmpty())
		QFile::remove(backup);

	return true;
}


void XMLZipExporter::writeMemberFile(const QString& filename, const std::function<void()>& writer)
{
	QuaZipFile out{ zip.get() };
	if (!out.open(QIODevice::WriteOnly, QuaZipNewInfo(filename)))
		throw FileFormatException(tr("Cannot create archive entry %1: %2").arg(filename, zip->getZipError()));

	xml.setDevice(&out);
	xml.writeDefaultNamespace(mapperNamespace());
	xml.writeStartDocument();
	writer();
	xml.writeEndDocument();

	if (xml.hasError())
	{
		out.close();
		throw FileFormatException(tr("Error while writing archive entry\n%1.").arg(filename));
	}
	out.close();
}


bool XMLZipExporter::embedTemplateFile(const Template* temp, const QDir&)
{
	auto const source = QFileInfo(temp->getTemplatePath());
	if (!source.isFile())
		return false;

	QString const base = source.completeBaseName();
	QString const suffix = source.completeSuffix();
	QString const filename = suffix.isEmpty() ? base : base + QLatin1Char('.') + suffix;
	QString const relative_path = QStringLiteral("templates/") + filename;

	QFile file{ source.absoluteFilePath() };
	if (!file.open(QIODevice::ReadOnly))
	{
		addWarning(tr("Cannot embed template file\n%1:\n%2").arg(source.absoluteFilePath(), file.errorString()));
		return false;
	}
	QByteArray const data = file.readAll();
	file.close();

	QuaZipFile out{ zip.get() };
	if (!out.open(QIODevice::WriteOnly, QuaZipNewInfo(relative_path)))
	{
		addWarning(tr("Cannot embed template file\n%1").arg(source.absoluteFilePath()));
		return false;
	}
	out.write(data);
	out.close();

	embedded_template_paths.emplace_back(const_cast<Template*>(temp), temp->getTemplateRelativePath());
	const_cast<Template*>(temp)->setTemplateRelativePath(relative_path);
	return true;
}


// ### XMLZipImporter definition ###

XMLZipImporter::XMLZipImporter(const QString& path, Map* map, MapView* view)
 : XMLDirectoryImporter(path, map, view)
{}


XMLZipImporter::~XMLZipImporter() = default;


bool XMLZipImporter::importImplementation()
{
	auto const info = QFileInfo(path);
	if (!info.isFile())
		throw FileFormatException(tr("Not a zip map file: %1").arg(path));

	// Expand into a temp folder, then read it with the folder importer.
	QTemporaryDir temp_dir;
	if (!temp_dir.isValid())
		throw FileFormatException(tr("Cannot create a temporary directory for reading the map."));

	auto const folder_path = temp_dir.path();

	// Extract the entries manually; JlCompress::extractDir needs a common sub-dir.
	QuaZip zip{ path };
	if (!zip.open(QuaZip::mdUnzip))
	{
		addWarning(tr("Cannot open the zip archive\n%1: %2").arg(path, zip.getZipError()));
		return false;
	}
	bool extract_ok = true;
	for (bool go = zip.goToFirstFile(); go; go = zip.goToNextFile())
	{
		QString const entry_name = zip.getCurrentFileName();
		if (entry_name.isEmpty() || entry_name.endsWith(QLatin1Char('/')))
			continue;
		QuaZipFile file{ &zip };
		if (!file.open(QIODevice::ReadOnly))
		{
			addWarning(tr("Cannot read archive entry %1: %2").arg(entry_name, file.getZipError()));
			extract_ok = false;
			break;
		}
		QByteArray const data = file.readAll();
		file.close();

		QFileInfo const out_info{ QDir(folder_path).filePath(entry_name) };
		if (!QDir{}.mkpath(out_info.absolutePath()))
		{
			addWarning(tr("Cannot create directory for archive entry %1").arg(entry_name));
			extract_ok = false;
			break;
		}
		QFile out{ out_info.absoluteFilePath() };
		if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
		{
			addWarning(tr("Cannot write archive entry %1").arg(entry_name));
			extract_ok = false;
			break;
		}
	}
	zip.close();

	if (!extract_ok)
		return false;

	XMLDirectoryImporter inner{ folder_path, map, view };
	inner.setLoadSymbolsOnly(loadSymbolsOnly());

	auto const ok = inner.doImport();
	for (auto const& warning : inner.warnings())
		addWarning(warning);

	// Load template images into memory before the temp folder is cleaned up.
	for (int i = 0; i < map->getNumTemplates(); ++i)
		map->getTemplate(i)->tryToFindAndReloadTemplateFile(folder_path);
	for (int i = 0; i < map->getNumClosedTemplates(); ++i)
		map->getClosedTemplate(i)->tryToFindAndReloadTemplateFile(folder_path);

	return ok;
}


void XMLZipImporter::validate()
{
	// The inner folder importer already performed the symbol and template
	// post-processing against the extracted map folder (the correct path).
	// Nothing to do here.
}


}  // namespace OpenOrienteering
