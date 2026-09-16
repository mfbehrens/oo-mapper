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

#include "xml_directory_format.h"
#include "xml_directory_format_p.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <QtGlobal>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QScopedValueRollback>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "settings.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_grid.h"
#include "core/map_part.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/map_printer.h"  // IWYU pragma: keep
#include "core/map_view.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "undo/undo_manager.h"  // IWYU pragma: keep
#include "util/xml_stream_util.h"


namespace OpenOrienteering {

// ### XMLDirectoryFormat definition ###

constexpr int XMLDirectoryFormat::current_version = 1;


XMLDirectoryFormat::XMLDirectoryFormat()
 : FileFormat(MapFile,
              "Directory",
              ::OpenOrienteering::ImportExport::tr("OpenOrienteering Mapper Folder"),
              QString::fromLatin1("map"),
              Feature::FileOpen | Feature::FileSave | Feature::FileSaveAs)
{}


FileFormat::ImportSupportAssumption XMLDirectoryFormat::understands(const char*, int) const
{
	// A map folder cannot be identified from the contents of a single file.
	// It is identified by the "map" file extension, and validated by the
	// importer which checks for the presence of index.xml.
	return Unknown;
}


std::unique_ptr<Importer> XMLDirectoryFormat::makeImporter(const QString& path, Map* map, MapView* view) const
{
	return std::make_unique<XMLDirectoryImporter>(path, map, view);
}


std::unique_ptr<Exporter> XMLDirectoryFormat::makeExporter(const QString& path, const Map* map, const MapView* view) const
{
	return std::make_unique<XMLDirectoryExporter>(path, map, view);
}



// ### A namespace which collects various string constants of type QLatin1String. ###

namespace literal
{
	static const QLatin1String notes("notes");
	static const QLatin1String file("file");
	static const QLatin1String metadata("metadata");
	static const QLatin1String georeferencing("georeferencing");
	static const QLatin1String colors("colors");
	static const QLatin1String symbols("symbols");
	static const QLatin1String part("part");
	static const QLatin1String parts("parts");
	static const QLatin1String templates("templates");
	static const QLatin1String print("print");
	static const QLatin1String state("state");
	static const QLatin1String view("view");
	static const QLatin1String current_part_index("current_part_index");
	static const QLatin1String undo("undo");
	static const QLatin1String redo("redo");
	static const QLatin1String count("count");
	static const QLatin1String symbol("symbol");
	static const QLatin1String id("id");
	static const QLatin1String template_string("template");
	static const QLatin1String area_hatching_enabled("area_hatching_enabled");
	static const QLatin1String baseline_view_enabled("baseline_view_enabled");
	static const QLatin1String grid("grid");
	static const QLatin1String map_view("map_view");
	static const QLatin1String first_front_template("first_front_template");
	static const QLatin1String defaults("defaults");
	static const QLatin1String use_meters_per_pixel("use_meters_per_pixel");
	static const QLatin1String meters_per_pixel("meters_per_pixel");
	static const QLatin1String dpi("dpi");
	static const QLatin1String scale("scale");
	
	// Top-level member file names
	static const QLatin1String metadata_file("metadata.xml");
	static const QLatin1String colors_file("colors.xml");
	static const QLatin1String symbols_file("symbols.xml");
	static const QLatin1String georeferencing_file("georeferencing.xml");
	static const QLatin1String print_file("print.xml");
	static const QLatin1String state_file("state.xml");
	
	// Directory and member file names of the parts sub-folder
	static const QLatin1String parts_dir("parts");
	static const QLatin1String parts_index_file("parts/index.xml");
	
	// Directory and member file names of the templates sub-folder
	static const QLatin1String templates_dir("templates");
	static const QLatin1String templates_index_file("templates/index.xml");
}



// ### XMLDirectoryExporter definition ###

namespace {

/**
 * Returns the XML namespace used by the map data of the XML formats.
 *
 * The directory format stores the map data in the same XML namespace as the
 * classic single-file XML map format. This must match the namespace used by
 * the XML file format.
 */
QString mapperNamespace()
{
	return QStringLiteral("http://openorienteering.org/apps/mapper/xml/v2");
}

/**
 * Makes a file name (without the directory and suffix) from a map part's name.
 *
 * Replaces characters which are not allowed in file names, and ensures the
 * result is non-empty, not longer than a safe limit, and not a reserved
 * device name on Windows. The caller must still make the file name unique
 * among the parts.
 */
QString makePartFilename(const QString& name)
{
	auto sanitized = name.toLower();
	sanitized.replace(QLatin1Char('/'),QLatin1Char('_'));
	sanitized.replace(QLatin1Char('\\'),QLatin1Char('_'));
	return QStringLiteral("part_") + sanitized;
}


}  // namespace


XMLDirectoryExporter::XMLDirectoryExporter(const QString& path, const Map* map, const MapView* view)
 : Exporter(path, map, view)
{
	// The map folder is meant to be human-readable and version-controlled, so
	// pretty-printing is the default. It can be turned off (minified output)
	// through the "autoFormatting" option, which is used e.g. for the .zip export.
	setOption(QString::fromLatin1("autoFormatting"), true);
}

XMLDirectoryExporter::~XMLDirectoryExporter() = default;



void XMLDirectoryExporter::exportAllFiles()
{
	exportColorsFile();
	exportSymbolsFile();
	exportPartsFiles();
	exportTemplatesFile();
	exportPrintFile();
	exportStateFile();
	exportMetadataFile();
	exportGeoreferencingFile();
}


bool XMLDirectoryExporter::exportImplementation()
{
	auto const path_info = QFileInfo(path);
	if (path_info.exists() && !path_info.isDir())
		throw FileFormatException(tr("Cannot save the map to a folder: %1 is not a directory.").arg(path));
	
	auto const parent_dir = path_info.absolutePath();
	auto const folder_name = path_info.fileName();
	
	// Stage the output in a sibling temporary folder, so that a crash does not
	// leave behind a partially written map folder.
	QTemporaryDir staging{ QDir(parent_dir).filePath(QLatin1String(".") + folder_name + QLatin1String(".XXXXXX")) };
	if (!staging.isValid())
		throw FileFormatException(tr("Cannot create a temporary folder for saving the map."));
	auto const staging_path = staging.path();
	QDir folder{ staging_path };
	if (!folder.mkpath(literal::parts_dir) || !folder.mkpath(literal::templates_dir))
		throw FileFormatException(tr("Cannot create the map folder structure."));
	
	output_path = staging_path;
	XMLFileFormat::active_version = XMLFileFormat::current_version;
	xml.setAutoFormatting(option(QString::fromLatin1("autoFormatting")).toBool());
	
	exportAllFiles();
	
	// Swap the staged folder into place.
	auto const target = path_info.absoluteFilePath();
	QString backup;
	if (QFileInfo::exists(target))
	{
		backup = QDir(parent_dir).filePath(QLatin1String(".") + folder_name + QLatin1String(".old"));
		QDir(backup).removeRecursively();
		if (!QDir().rename(target, backup))
			throw FileFormatException(tr("Cannot replace the existing map folder\n%1:\n%2").arg(target, tr("The existing folder could not be renamed.")));
	}
	staging.setAutoRemove(false);
	if (!QDir().rename(staging_path, target))
	{
		if (!backup.isEmpty())
			QDir().rename(backup, target);
		throw FileFormatException(tr("Cannot create map folder\n%1:\n%2").arg(target, tr("The temporary folder could not be moved to its final location.")));
	}
	if (!backup.isEmpty())
		QDir(backup).removeRecursively();
	
	return true;
}



void XMLDirectoryExporter::writeMemberFile(const QString& filename, const std::function<void()>& writer)
{
	QFile file{ QDir(output_path).filePath(filename) };
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		throw FileFormatException(tr("Cannot open file\n%1:\n%2").arg(file.fileName(), file.errorString()));
	
	xml.setDevice(&file);
	xml.writeDefaultNamespace(mapperNamespace());
	xml.writeStartDocument();
	writer();
	xml.writeEndDocument();
	
	if (xml.hasError())
	{
		file.close();
		throw FileFormatException(tr("Error while writing file\n%1.").arg(file.fileName()));
	}
	file.close();
}


void XMLDirectoryExporter::exportColorsFile()
{
	writeMemberFile(literal::colors_file, [this]() {
		exportColors();
	});
}


void XMLDirectoryExporter::exportSymbolsFile()
{
	writeMemberFile(literal::symbols_file, [this]() {
		exportSymbols();
	});
}


void XMLDirectoryExporter::exportPartsFiles()
{
	auto const num_parts = map->getNumParts();
	part_filenames.clear();
	part_filenames.reserve(num_parts);
	QSet<QString> used_filenames;
	
	// Make a unique file name for every part, based on the part's name.
	for (auto i = 0; i < num_parts; ++i)
	{
		auto const base = makePartFilename(map->getPart(i)->getName());
		auto filename = base;
		for (auto counter = 1; used_filenames.contains(filename); ++counter)
			filename = base + QStringLiteral("-%1").arg(counter);
		used_filenames.insert(filename);
		part_filenames.push_back(QString::fromLatin1("parts/") + filename + QString::fromLatin1(".xml"));
	}
	
	for (auto i = 0; i < num_parts; ++i)
	{
		writeMemberFile(part_filenames[i], [this, i]() {
			map->getPart(i)->save(xml);
		});
	}
	
	// Write the parts index, listing all part files.
	writeMemberFile(literal::parts_index_file, [this, num_parts]() {
		XmlElementWriter parts_element(xml, literal::parts);
		parts_element.writeAttribute(literal::count, std::size_t(num_parts));
		for (auto const& filename : part_filenames)
		{
			writeLineBreak(xml);
			xml.writeEmptyElement(literal::part);
			xml.writeAttribute(literal::file, QFileInfo(filename).fileName());
		}
		writeLineBreak(xml);
	});
}


void XMLDirectoryExporter::exportTemplatesFile()
{
	QDir const folder{ output_path };
	
	// Embed the template image files into the templates sub-folder.
	embedded_template_paths.clear();
	for (int i = 0; i < map->getNumTemplates(); ++i)
		embedTemplateFile(map->getTemplate(i), folder);
	for (int i = 0; i < map->getNumClosedTemplates(); ++i)
		embedTemplateFile(map->getClosedTemplate(i), folder);
	
	// Do not write absolute source paths of the embedded template files.
	QScopedValueRollback<bool> suppress{ Template::suppressAbsolutePaths, true };
	
	writeMemberFile(literal::templates_index_file, [this]() {
		xml.writeStartElement(literal::templates);
		xml.writeAttribute(literal::count, QString::number(map->getNumTemplates() + map->getNumClosedTemplates()));
		xml.writeAttribute(literal::first_front_template, QString::number(map->getFirstFrontTemplate()));
		for (int i = 0; i < map->getNumTemplates(); ++i)
		{
			xml.writeCharacters(QStringLiteral("\n"));
			map->getTemplate(i)->saveTemplateConfiguration(xml, true, nullptr);
		}
		xml.writeCharacters(QStringLiteral("\n"));
		xml.writeEndElement();
	});
	
	// Restore the relative paths of the embedded templates.
	for (auto&& item : embedded_template_paths)
		item.first->setTemplateRelativePath(item.second);
	embedded_template_paths.clear();
}


bool XMLDirectoryExporter::embedTemplateFile(const Template* temp, const QDir& folder)
{
	auto const source = QFileInfo(temp->getTemplatePath());
	if (!source.isFile())
		return false;
	
	QDir const templates_dir{ folder.filePath(literal::templates_dir) };
	QString const base = source.completeBaseName();
	QString const suffix = source.completeSuffix();
	QString filename = suffix.isEmpty() ? base : base + QLatin1Char('.') + suffix;
	for (int counter = 1; QFileInfo(templates_dir.filePath(filename)).exists(); ++counter)
		filename = base + QStringLiteral("-%1").arg(counter) + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
	
	QString const relative_path = QStringLiteral("templates/") + filename;
	if (!QFile::copy(source.absoluteFilePath(), templates_dir.filePath(filename)))
	{
		addWarning(tr("Cannot embed template file\n%1:\n%2").arg(source.absoluteFilePath(), temp->errorString()));
		return false;
	}
	
	embedded_template_paths.emplace_back(const_cast<Template*>(temp), temp->getTemplateRelativePath());
	const_cast<Template*>(temp)->setTemplateRelativePath(relative_path);
	return true;
}


void XMLDirectoryExporter::exportPrintFile()
{
	if (!map->hasPrinterConfig())
		return;
	
	writeMemberFile(literal::print_file, [this]() {
		exportPrint();
	});
}


void XMLDirectoryExporter::exportStateFile()
{
	writeMemberFile(literal::state_file, [this]() {
		XmlElementWriter state_element(xml, literal::state);
		state_element.writeAttribute(literal::current_part_index, std::size_t(map->getCurrentPartIndex()));
		
		writeLineBreak(xml);
		exportView();
		
		if (Settings::getInstance().getSetting(Settings::General_SaveUndoRedo).toBool()
		    && (map->undoManager().canUndo() || map->undoManager().canRedo()) )
		{
			writeLineBreak(xml);
			exportUndo();
			writeLineBreak(xml);
			exportRedo();
		}
		writeLineBreak(xml);
	});
}


void XMLDirectoryExporter::exportGeoreferencingFile()
{
	writeMemberFile(literal::georeferencing_file, [this]() {
		exportGeoreferencing();
	});
}

void XMLDirectoryExporter::exportMetadataFile()
{
	writeMemberFile(literal::metadata_file, [this]() {
		XmlElementWriter metadata_element(xml, literal::metadata);
		xml.writeTextElement(literal::notes, map->getMapNotes());
	});
}



// The per-section writers replicate the element layout of the single-file XML
// map format (they are intentionally duplicated rather than inherited, so that
// the directory format is independent of the XML format).

void XMLDirectoryExporter::exportColors()
{
	map->color_set->save(xml);
}

void XMLDirectoryExporter::exportSymbols()
{
	XmlElementWriter symbols_element(xml, literal::symbols);
	auto const id = map->symbolSetId();
	int num_symbols = map->getNumSymbols();
	symbols_element.writeAttribute(literal::count, num_symbols);
	if (!id.isEmpty())
		symbols_element.writeAttribute(literal::id, id);
	for (int i = 0; i < num_symbols; ++i)
	{
		writeLineBreak(xml);
		map->getSymbol(i)->save(xml, *map);
	}
	writeLineBreak(xml);
}

void XMLDirectoryExporter::exportGeoreferencing()
{
	map->getGeoreferencing().save(xml);
	writeLineBreak(xml);
}

void XMLDirectoryExporter::exportView()
{
	XmlElementWriter view_element(xml, literal::view);
	view_element.writeAttribute(literal::area_hatching_enabled, bool(map->renderable_options & Symbol::RenderAreasHatched));
	view_element.writeAttribute(literal::baseline_view_enabled, bool(map->renderable_options & Symbol::RenderBaselines));
	
	writeLineBreak(xml);
	map->getGrid().save(xml);
	
	if (view)
	{
		writeLineBreak(xml);
		view->save(xml, literal::map_view);
	}
	writeLineBreak(xml);
}

void XMLDirectoryExporter::exportPrint()
{
	if (map->hasPrinterConfig())
	{
		map->printerConfig().save(xml, literal::print);
		writeLineBreak(xml);
	}
}

void XMLDirectoryExporter::exportUndo()
{
	map->undoManager().saveUndo(xml);
	writeLineBreak(xml);
}

void XMLDirectoryExporter::exportRedo()
{
	map->undoManager().saveRedo(xml);
	writeLineBreak(xml);
}



// ### XMLDirectoryImporter definition ###

XMLDirectoryImporter::XMLDirectoryImporter(const QString& path, Map* map, MapView* view)
 : Importer(path, map, view)
{}

XMLDirectoryImporter::~XMLDirectoryImporter() = default;



bool XMLDirectoryImporter::importImplementation()
{
	QScopedValueRollback<MapCoord::BoundsOffset> rollback { MapCoord::boundsOffset() };
	MapCoord::boundsOffset().reset(true);
	georef_offset_adjusted = false;
	
	if (!QFileInfo(path).isDir())
		throw FileFormatException(tr("Not a map folder: %1").arg(path));
	
	// The member files are written using the current XML serialization.
	version = XMLFileFormat::current_version;
	
	importMetadataFile();
	importGeoreferencingFile();
	
	if (loadSymbolsOnly())
	{
		importColorsFile();
		importSymbolsFile();
		return true;
	}
	
	importColorsFile();
	importSymbolsFile();
	importPartsFiles();
	importTemplatesFile();
	importPrintFile();
	importStateFile();
	
	if (!map->parts.empty())
	{
		if (map->current_part_index >= map->parts.size())
			map->current_part_index = 0;
		emit map->currentMapPartIndexChanged(map->current_part_index);
		emit map->currentMapPartChanged(map->getPart(map->current_part_index));
	}
	
	auto offset = MapCoord::boundsOffset();
	if (!offset.isZero())
	{
		addWarning(tr("Some coordinates were out of bounds for printing. Map content was adjusted."));
		
		MapCoordF offset_f { offset.x / 1000.0, offset.y / 1000.0 };
		
		// Apply the offset
		auto printer_config = map->printerConfig();
		auto& print_area = printer_config.print_area;
		print_area.translate( -offset_f );
		
		// Verify the adjusted print area, and readjust if necessary
		if (print_area.top() <= -1000000.0 || print_area.bottom() > 1000000.0)
			print_area.moveTop(-print_area.width() / 2);
		if (print_area.left() <= -1000000.0 || print_area.right() > 1000000.0)
			print_area.moveLeft(-print_area.width() / 2);
		
		map->setPrinterConfig(printer_config);
		
		if (!georef_offset_adjusted)
		{
			// We need to adjust the georeferencing.
			auto georef = map->getGeoreferencing();
			auto ref_point = MapCoordF { georef.getMapRefPoint() };
			auto new_projected = georef.toProjectedCoords(ref_point + offset_f);
			georef.setProjectedRefPoint(new_projected, false, false);
			georef.setCombinedScaleFactor(georef.getCombinedScaleFactor()); // keep combined scale factor
			georef.setGrivation(georef.getGrivation());  // keep grivation, update declination
			map->setGeoreferencing(georef);
		}
	}
	return true;
}



void XMLDirectoryImporter::readMemberFile(const QString& filename, const std::function<void()>& reader)
{
	QFile file{ QDir(path).filePath(filename) };
	if (!file.open(QIODevice::ReadOnly))
		throw FileFormatException(tr("Cannot open file\n%1:\n%2").arg(file.fileName(), file.errorString()));
	
	xml.setDevice(&file);
	if (!xml.readNextStartElement())
	{
		auto const error = xml.hasError() ? xml.errorString() : tr("The file is empty.");
		file.close();
		throw FileFormatException(tr("Cannot read file\n%1:\n%2").arg(file.fileName(), error));
	}
	reader();
	if (xml.hasError())
	{
		auto const error = tr("Error at line %1 column %2: %3")
		                   .arg(xml.lineNumber())
		                   .arg(xml.columnNumber())
		                   .arg(xml.errorString());
		file.close();
		throw FileFormatException(tr("Error in file\n%1:\n%2").arg(file.fileName(), error));
	}
	file.close();
}


void XMLDirectoryImporter::importGeoreferencingFile()
{
	if (!QFileInfo::exists(QDir(path).filePath(literal::georeferencing_file)))
		return;
	
	readMemberFile(literal::georeferencing_file, [this]() {
		if (loadSymbolsOnly())
		{
			xml.skipCurrentElement();
			return;
		}
		
		if (xml.name() != literal::georeferencing)
			throw FileFormatException(tr("Not a map georeferencing file."));
		importGeoreferencing();
	});
}

void XMLDirectoryImporter::importMetadataFile()
{
	readMemberFile(literal::metadata_file, [this]() {
		if (loadSymbolsOnly())
		{
			xml.skipCurrentElement();
			return;
		}
		
		if (xml.name() != literal::metadata)
		{
			xml.skipCurrentElement();
			return;
		}
		
		while (xml.readNextStartElement())
		{
			if (xml.name() == literal::notes)
				importMapNotes();
			else
				xml.skipCurrentElement();
		}
	});
}


void XMLDirectoryImporter::importColorsFile()
{
	readMemberFile(literal::colors_file, [this]() {
		importColors();
	});
}


void XMLDirectoryImporter::importSymbolsFile()
{
	readMemberFile(literal::symbols_file, [this]() {
		importSymbols();
	});
}


void XMLDirectoryImporter::importPartsFiles()
{
	// Read the parts index, which lists all part files.
	QStringList part_files;
	readMemberFile(literal::parts_index_file, [this, &part_files]() {
		if (xml.name() != literal::parts)
			throw FileFormatException(tr("Not a map parts index file."));
		
		XmlElementReader parts_element(xml);
		while (xml.readNextStartElement())
		{
			if (xml.name() == literal::part)
			{
				XmlElementReader part_element(xml);
				auto const filename = part_element.attribute<QString>(literal::file);
				if (!filename.isEmpty())
					part_files.push_back(QString::fromLatin1("parts/") + filename);
			}
			else
				xml.skipCurrentElement();
		}
	});
	
	map->parts.clear();
	map->parts.reserve(qMin<qsizetype>(part_files.size(), 20)); // 20 is not a limit
	
	for (auto const& filename : part_files)
	{
		readMemberFile(filename, [this]() {
			if (xml.name() != literal::part)
				throw FileFormatException(tr("Not a map part file."));
			
			auto recovery = XmlRecoveryHelper(xml);
			auto part = MapPart::load(xml, *map, symbol_dict);
			if (xml.hasError() && recovery())
			{
				addWarning(tr("Some invalid characters had to be removed."));
				delete part;
				part = MapPart::load(xml, *map, symbol_dict);
			}
			map->parts.push_back(part);
		});
	}
}


void XMLDirectoryImporter::importTemplatesFile()
{
	auto const filename = literal::templates_index_file;
	if (!QFileInfo::exists(QDir(path).filePath(filename)))
		return;
	
	readMemberFile(filename, [this]() {
		importTemplates();
	});
}


void XMLDirectoryImporter::importPrintFile()
{
	if (!QFileInfo::exists(QDir(path).filePath(literal::print_file)))
		return;
	
	readMemberFile(literal::print_file, [this]() {
		importPrint();
	});
}


void XMLDirectoryImporter::importStateFile()
{
	if (!QFileInfo::exists(QDir(path).filePath(literal::state_file)))
		return;
	
	readMemberFile(literal::state_file, [this]() {
		if (xml.name() != literal::state)
			return;  // unsupported; nothing to import
		
		XmlElementReader state_element(xml);
		if (state_element.hasAttribute(literal::current_part_index))
			map->current_part_index = state_element.attribute<std::size_t>(literal::current_part_index);
		
		while (xml.readNextStartElement())
		{
			if (xml.name() == literal::view)
				importView();
			else if (xml.name() == literal::undo)
				importUndo();
			else if (xml.name() == literal::redo)
				importRedo();
			else
				xml.skipCurrentElement();
		}
	});
}


// The per-section readers replicate the element layout of the single-file XML
// map format (they are intentionally duplicated rather than inherited, so that
// the directory format is independent of the XML format).

void XMLDirectoryImporter::addWarningUnsupportedElement()
{
	addWarning(tr("Unsupported element: %1 (line %2 column %3)").
	  arg(xml.name().toString()).
	  arg(xml.lineNumber()).
	  arg(xml.columnNumber())
	);
}

void XMLDirectoryImporter::importMapNotes()
{
	auto recovery = XmlRecoveryHelper(xml);
	map->setMapNotes(xml.readElementText());
	if (xml.hasError() && recovery())
	{
		addWarning(tr("Some invalid characters had to be removed."));
		map->setMapNotes(xml.readElementText());
	}
}

void XMLDirectoryImporter::importGeoreferencing()
{
	FILEFORMAT_ASSERT(xml.name() == literal::georeferencing);
	
	bool check_for_offset = MapCoord::boundsOffset().check_for_offset;
	
	Georeferencing georef;
	georef.load(xml, loadSymbolsOnly());
	map->setGeoreferencing(georef);
	if (georef.getState() == Georeferencing::BrokenGeospatial)
	{
		QString error_text = georef.getErrorText();
		if (error_text.isEmpty())
			error_text = tr("Unknown error");
		addWarning(tr("Unsupported or invalid georeferencing specification '%1': %2").
		           arg(georef.getProjectedCRSSpec(), error_text));
	}
	
	if (MapCoord::boundsOffset().isZero())
		// Georeferencing was not adjusted on import.
		MapCoord::boundsOffset().reset(check_for_offset);
	else if (check_for_offset)
		// Georeferencing was adjusted on import, before other coordinates.
		georef_offset_adjusted = true;
	
	validateGeoreferencing();
}

void XMLDirectoryImporter::validateGeoreferencing()
{
	auto const& loaded_georef = map->getGeoreferencing();
	if (loaded_georef.getState() != Georeferencing::Geospatial)
		return;
	
	// Check for georeferencings with inconsistent declination/grivation,
	// e.g. from GH-1206 (georef setup bug)
	auto valid_georef = Georeferencing(loaded_georef);
	// Keep grivation, force calculation of declination
	valid_georef.setGrivation(loaded_georef.getGrivation());
	if (!qFuzzyCompare(loaded_georef.getDeclination(), valid_georef.getDeclination()))
	{
		map->setGeoreferencing(valid_georef);
		addWarning(tr("Inconsistent declination/grivation detected. "
		              "Resolved by automatic adjustment of the declination to %1°.")
		           .arg(QLocale().toString(valid_georef.getDeclination()) ) );
	}
}

void XMLDirectoryImporter::importColors()
{
	for (auto const& warning : map->color_set->load(xml, *map))
		addWarning(warning);
}

void XMLDirectoryImporter::importSymbols()
{
	QScopedValueRollback<MapCoord::BoundsOffset> offset { MapCoord::boundsOffset() };
	MapCoord::boundsOffset().reset(false);
	
	XmlElementReader symbols_element(xml);
	map->setSymbolSetId(symbols_element.attribute<QString>(literal::id));
	auto num_symbols = symbols_element.attribute<std::size_t>(literal::count);
	map->symbols.reserve(qMin(num_symbols, std::size_t(1000))); // 1000 is not a limit
	
	symbol_dict[map->findSymbolIndex(map->getUndefinedPoint())] = map->getUndefinedPoint();
	symbol_dict[map->findSymbolIndex(map->getUndefinedLine())] = map->getUndefinedLine();
	
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::symbol)
		{
			map->symbols.push_back(Symbol::load(xml, *map, symbol_dict, version).release());
		}
		else
		{
			addWarningUnsupportedElement();
			xml.skipCurrentElement();
		}
	}
	
	if (num_symbols > 0 && num_symbols != map->symbols.size())
		addWarning(tr("Expected %1 symbols, found %2.").
		  arg(num_symbols).
		  arg(map->symbols.size())
		);
}

void XMLDirectoryImporter::importTemplates()
{
	FILEFORMAT_ASSERT(xml.name() == literal::templates);
	
	XmlElementReader templates_element(xml);
	int first_front_template = templates_element.attribute<int>(literal::first_front_template);
	
	auto num_templates = templates_element.attribute<std::size_t>(literal::count);
	map->templates.reserve(qMin(num_templates, std::size_t(20))); // 20 is not a limit
	map->closed_templates.reserve(qMin(num_templates, std::size_t(20))); // 20 is not a limit
	
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::template_string)
		{
			bool opened = true;
			auto temp = Template::loadTemplateConfiguration(xml, *map, opened);
			if (opened)
				map->templates.push_back(std::move(temp));
			else
				map->closed_templates.push_back(std::move(temp));
		}
		else if (xml.name() == literal::defaults)
		{
			XmlElementReader defaults_element(xml);
			Map::ImageTemplateDefaults defaults = { defaults_element.attribute<bool>(literal::use_meters_per_pixel),
			                                        defaults_element.attribute<double>(literal::meters_per_pixel),
			                                        defaults_element.attribute<double>(literal::dpi),
			                                        defaults_element.attribute<double>(literal::scale) };
			map->setImageTemplateDefaults(defaults);
		}
		else
		{
			qDebug("Unsupported element: %s", qPrintable(xml.qualifiedName().toString()));
			xml.skipCurrentElement();
		}
	}
	
	map->first_front_template = qMax(0, qMin(map->getNumTemplates(), first_front_template));
}

void XMLDirectoryImporter::importView()
{
	FILEFORMAT_ASSERT(xml.name() == literal::view);
	
	XmlElementReader view_element(xml);
	if (view_element.attribute<bool>(literal::area_hatching_enabled))
		map->renderable_options |= Symbol::RenderAreasHatched;
	if (view_element.attribute<bool>(literal::baseline_view_enabled))
		map->renderable_options |= Symbol::RenderBaselines;
	
	while (xml.readNextStartElement())
	{
		if (xml.name() == literal::grid)
		{
			map->setGrid(MapGrid().load(xml));
		}
		else if (xml.name() == literal::map_view)
		{
			if (view)
				view->load(xml, version);
			else
				xml.skipCurrentElement();
		}
		else
		{
			xml.skipCurrentElement(); // unsupported
		}
	}
}

void XMLDirectoryImporter::importPrint()
{
	FILEFORMAT_ASSERT(xml.name() == literal::print);
	
	try
	{
		map->setPrinterConfig(MapPrinterConfig(*map, xml));
	}
	catch (FileFormatException& e)
	{
		addWarning(::OpenOrienteering::ImportExport::tr("Error while loading the printing configuration at %1:%2: %3")
		           .arg(xml.lineNumber()).arg(xml.columnNumber()).arg(e.message()));
	}
}

void XMLDirectoryImporter::importUndo()
{
	if (!Settings::getInstance().getSetting(Settings::General_SaveUndoRedo).toBool())
	{
		xml.skipCurrentElement();
		return;
	}
	
	try
	{
		map->undoManager().loadUndo(xml, symbol_dict);
	}
	catch (FileFormatException& e)
	{
		addWarning(::OpenOrienteering::ImportExport::tr("Error while loading the undo/redo steps at %1:%2: %3")
		           .arg(xml.lineNumber()).arg(xml.columnNumber()).arg(e.message()));
		map->undoManager().clear();
	}
}

void XMLDirectoryImporter::importRedo()
{
	if (!Settings::getInstance().getSetting(Settings::General_SaveUndoRedo).toBool())
	{
		xml.skipCurrentElement();
		return;
	}
	
	try
	{
		map->undoManager().loadRedo(xml, symbol_dict);
	}
	catch (FileFormatException& e)
	{
		addWarning(::OpenOrienteering::ImportExport::tr("Error while loading the undo/redo steps at %1:%2: %3")
		           .arg(xml.lineNumber()).arg(xml.columnNumber()).arg(e.message()));
		map->undoManager().clear();
	}
}


}  // namespace OpenOrienteering
