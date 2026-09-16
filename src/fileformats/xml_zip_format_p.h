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

#ifndef OPENORIENTEERING_XML_ZIP_FORMAT_P_H
#define OPENORIENTEERING_XML_ZIP_FORMAT_P_H

#include <memory>

#include <QString>

#include "fileformats/xml_directory_format_p.h"

class QuaZip;

namespace OpenOrienteering {


/** Map exporter: streams the map directly into a zip archive. */
class XMLZipExporter : public XMLDirectoryExporter
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::XMLZipExporter)

public:
	XMLZipExporter(const QString& path, const Map* map, const MapView* view);
	XMLZipExporter() = delete;
	XMLZipExporter(const XMLZipExporter&) = delete;
	XMLZipExporter(XMLZipExporter&&) = delete;
	~XMLZipExporter() override;

	XMLZipExporter& operator=(const XMLZipExporter&) = delete;
	XMLZipExporter& operator=(XMLZipExporter&&) = delete;

protected:
	bool exportImplementation() override;

private:
	void writeMemberFile(const QString& filename, const std::function<void()>& writer) override;
	bool embedTemplateFile(const Template* temp, const QDir& folder) override;

	/** The zip archive being written (owned). */
	std::unique_ptr<QuaZip> zip;
};


/** Map importer: expands the archive and reads it with the folder importer. */
class XMLZipImporter : public XMLDirectoryImporter
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::XMLZipImporter)

public:
	XMLZipImporter(const QString& path, Map* map, MapView* view);
	XMLZipImporter() = delete;
	XMLZipImporter(const XMLZipImporter&) = delete;
	XMLZipImporter(XMLZipImporter&&) = delete;
	~XMLZipImporter() override;

	XMLZipImporter& operator=(const XMLZipImporter&) = delete;
	XMLZipImporter& operator=(XMLZipImporter&&) = delete;

protected:
	bool importImplementation() override;

	/** Validation is done by the inner folder importer; skipped here. */
	void validate() override;
};


}  // namespace OpenOrienteering

#endif // OPENORIENTEERING_XML_ZIP_FORMAT_P_H
