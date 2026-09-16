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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <quazip/JlCompress.h>

#include "core/map.h"
#include "core/map_view.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_directory_format.h"
#include "fileformats/xml_directory_format_p.h"


/* The zip archive stores the map folder's member files at the archive root,
 * named by their relative path. The zip format is a thin storage wrapper
 * around the directory format: the map folder is written to / read from a
 * temporary folder, which is then compressed into / extracted from the
 * archive. */

namespace OpenOrienteering {


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
	auto const target = QFileInfo(path).absoluteFilePath();

	QTemporaryDir tmp_dir_;
	if (!tmp_dir_.isValid())
		throw FileFormatException(tr("Cannot create a temporary directory for saving the map."));
	auto const folder = QDir(tmp_dir_.path()).filePath(QStringLiteral("map"));

	// Write the map folder with the directory exporter.
	XMLDirectoryExporter folder_exporter{ folder, map, view };
	folder_exporter.setOption(QString::fromLatin1("autoFormatting"), option(QString::fromLatin1("autoFormatting")));
	if (!folder_exporter.doExport())
	{
		for (auto const& warning : folder_exporter.warnings())
			addWarning(warning);
		return false;
	}

	// Compress the map folder into a temporary archive.
	QString const tmp_zip = QDir(tmp_dir_.path()).filePath(QStringLiteral("map.omapzip"));
	if (!JlCompress::compressDir(tmp_zip, folder))
	{
		addWarning(tr("Cannot create the zip archive\n%1.").arg(target));
		return false;
	}

	// Move the temporary archive into place, replacing an existing file.
	QString backup;
	if (QFile::exists(target))
	{
		backup = QDir(QFileInfo(target).absolutePath()).filePath(QStringLiteral(".map.omapzip.old"));
		QFile::remove(backup);
		if (!QFile::rename(target, backup))
			throw FileFormatException(tr("Cannot replace the existing map file\n%1:\n%2").arg(target, tr("The existing file could not be renamed.")));
	}
	if (!QFile::rename(tmp_zip, target))
	{
		if (!backup.isEmpty())
			QFile::rename(backup, target);
		throw FileFormatException(tr("Cannot create map file\n%1:\n%2").arg(target, tr("The temporary file could not be moved to its final location.")));
	}
	if (!backup.isEmpty())
		QFile::remove(backup);

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
	auto const zip_file = info.absoluteFilePath();

	QTemporaryDir tmp_dir;
	if (!tmp_dir.isValid())
		throw FileFormatException(tr("Cannot create a temporary directory for reading the map."));

	// Extract the archive into the temporary folder.
	QStringList const extracted = JlCompress::extractDir(zip_file, tmp_dir.path());
	if (extracted.isEmpty())
		throw FileFormatException(tr("Cannot read the zip archive\n%1:\n%2").arg(zip_file, tr("No files could be extracted.")));

	// Import the map folder with the directory importer.
	XMLDirectoryImporter folder_importer{ tmp_dir.path(), map, view };
	if (!folder_importer.doImport())
	{
		for (auto const& warning : folder_importer.warnings())
			addWarning(warning);
		return false;
	}
	return true;
}


void XMLZipImporter::validate()
{
	// The map validation is done by the inner folder importer during
	// importImplementation(); there is nothing to validate here.
}


}  // namespace OpenOrienteering