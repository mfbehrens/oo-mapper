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

#ifndef OPENORIENTEERING_XML_DIRECTORY_FORMAT_P_H
#define OPENORIENTEERING_XML_DIRECTORY_FORMAT_P_H

#include <functional>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/symbols/symbol.h"
#include "fileformats/file_import_export.h"

class QDir;

namespace OpenOrienteering {

class Template;


/** Map exporter for the directory based map format. */
class XMLDirectoryExporter : public Exporter
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::XMLDirectoryExporter)

public:
	XMLDirectoryExporter(const QString& path, const Map* map, const MapView* view);
	XMLDirectoryExporter() = delete;
	XMLDirectoryExporter(const XMLDirectoryExporter&) = delete;
	XMLDirectoryExporter(XMLDirectoryExporter&&) = delete;
	~XMLDirectoryExporter() override;

	XMLDirectoryExporter& operator=(const XMLDirectoryExporter&) = delete;
	XMLDirectoryExporter& operator=(XMLDirectoryExporter&&) = delete;

protected:
	bool exportImplementation() override;
	bool supportsQIODevice() const noexcept override { return false; }

	/**
	 * Writes all member files of the map folder / archive.
	 *
	 * The member files (and the embedded template files) are written through
	 * writeMemberFile() and embedTemplateFile(), which are overridable so that
	 * a different storage backend (e.g. a zip archive) can be used.
	 */
	void exportAllFiles();

	/**
	 * Writes a single member file.
	 *
	 * The filename is the relative member-file name within the map folder /
	 * archive (e.g. "colors.xml", "parts/index.xml"). The given function must
	 * write the root element of the member file (including its children).
	 */
	virtual void writeMemberFile(const QString& filename, const std::function<void()>& writer);

	/**
	 * Embeds the given template's image file into the current storage backend.
	 *
	 * Returns true when the file was embedded. Overridden by the zip backend.
	 */
	virtual bool embedTemplateFile(const Template* temp, const QDir& folder);

	/** The XML stream used to write the member files. */
	QXmlStreamWriter xml;

	/** The path of the folder the member files are written/restored to. */
	QString output_path;

private:
	/** Exports the colors to colors.xml. */
	void exportColorsFile();

	/** Exports the symbols to symbols.xml. */
	void exportSymbolsFile();

	/**
	 * Exports the map parts to parts/<name>.xml, where <name> is derived
	 * from the part's name, and a parts/index.xml which lists all part files.
	 */
	void exportPartsFiles();

	/**
	 * Exports the templates to templates/index.xml, embedding the template
	 * image files into the templates sub-folder.
	 */
	void exportTemplatesFile();

	/** Exports the printer configuration to print.xml, if any. */
	void exportPrintFile();

	/** Exports the view and undo/redo data to state.xml. */
	void exportStateFile();

	/** Exports the map metadata to metadata.xml. */
	void exportMetadataFile();

	/** Exports the georeferencing to georeferencing.xml. */
	void exportGeoreferencingFile();

	// The per-section writers below serialize the map data in the same XML
	// element layout as the classic single-file XML map format. They are
	// duplicated here (rather than inherited) so that the directory format is
	// independent of the XML format; the actual serialization lives in the
	// data classes' save()/load() methods.

	/** Writes the &lt;colors&gt; element. */
	void exportColors();

	/** Writes the &lt;symbols&gt; element. */
	void exportSymbols();

	/** Writes the &lt;georeferencing&gt; element. */
	void exportGeoreferencing();

	/** Writes the &lt;view&gt; element. */
	void exportView();

	/** Writes the &lt;undo&gt; element. */
	void exportUndo();

	/** Writes the &lt;redo&gt; element. */
	void exportRedo();

	/** Writes the &lt;print&gt; element, if the map has a printer configuration. */
	void exportPrint();

	/**
	 * The relative paths of the part files written to the map folder,
	 * in map order. The parts index (parts/index.xml) writes these as
	 * part references.
	 */
	QStringList part_filenames;

protected:
	/** The relative paths of the embedded templates, to be restored after export. */
	std::vector<std::pair<Template*, QString>> embedded_template_paths;

};


/** Map importer for the directory based map format. */
class XMLDirectoryImporter : public Importer
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::XMLDirectoryImporter)

public:
	XMLDirectoryImporter(const QString& path, Map* map, MapView* view);
	XMLDirectoryImporter() = delete;
	XMLDirectoryImporter(const XMLDirectoryImporter&) = delete;
	XMLDirectoryImporter(XMLDirectoryImporter&&) = delete;
	~XMLDirectoryImporter() override;

	XMLDirectoryImporter& operator=(const XMLDirectoryImporter&) = delete;
	XMLDirectoryImporter& operator=(XMLDirectoryImporter&&) = delete;

protected:
	bool importImplementation() override;
	bool supportsQIODevice() const noexcept override { return false; }

private:
	/**
	 * Reads a single member file of the map folder.
	 *
	 * The given function must consume the root element of the member file
	 * (including its children), starting at the current element.
	 * Throws FileFormatException if the file cannot be read.
	 */
	void readMemberFile(const QString& filename, const std::function<void()>& reader);

	/** Reads the georeferencing from georeferencing.xml, if present. */
	void importGeoreferencingFile();

	/** Reads the map notes from metadata.xml. */
	void importMetadataFile();

	/** Reads the colors from colors.xml. */
	void importColorsFile();

	/** Reads the symbols from symbols.xml. */
	void importSymbolsFile();

	/** Reads the map parts from the files listed in parts/index.xml. */
	void importPartsFiles();

	/** Reads the templates from templates/index.xml, if present. */
	void importTemplatesFile();

	/** Reads the printer configuration from print.xml, if present. */
	void importPrintFile();

	/** Reads the view and undo/redo data from state.xml, if present. */
	void importStateFile();

	// The per-section readers below read the map data in the same XML element
	// layout as the classic single-file XML map format. They are duplicated
	// here (rather than inherited) so that the directory format is independent
	// of the XML format.

	/** Adds a warning for an unsupported element in the current stream. */
	void addWarningUnsupportedElement();

	/** Reads the map notes from the current &lt;notes&gt; element. */
	void importMapNotes();

	/** Reads the &lt;georeferencing&gt; element. */
	void importGeoreferencing();

	/** Verifies and fixes an inconsistent georeferencing, if necessary. */
	void validateGeoreferencing();

	/** Reads the &lt;colors&gt; element. */
	void importColors();

	/** Reads the &lt;symbols&gt; element. */
	void importSymbols();

	/** Reads the &lt;templates&gt; element. */
	void importTemplates();

	/** Reads the &lt;view&gt; element. */
	void importView();

	/** Reads the &lt;undo&gt; element. */
	void importUndo();

	/** Reads the &lt;redo&gt; element. */
	void importRedo();

	/** Reads the &lt;print&gt; element, if present. */
	void importPrint();

	/** The XML stream used to read the member files. */
	QXmlStreamReader xml;

	/** The dictionary of symbols loaded so far, used during symbol loading. */
	SymbolDictionary symbol_dict;

	/** The version of the format the member files were written with. */
	int version = -1;

	/** Whether the georeferencing was offset-adjusted on import. */
	bool georef_offset_adjusted = false;
};


}  // namespace OpenOrienteering

#endif // OPENORIENTEERING_XML_DIRECTORY_FORMAT_P_H
