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

#ifndef OPENORIENTEERING_XML_ZIP_FORMAT_H
#define OPENORIENTEERING_XML_ZIP_FORMAT_H

#include <memory>

#include "fileformats/file_format.h"

class QString;

namespace OpenOrienteering {

class Exporter;
class Importer;
class Map;
class MapView;


/**
 * @brief Zip file format (see XMLDirectoryFormat for the folder layout).
 *
 * A thin storage wrapper around XMLDirectoryFormat.
 */
class XMLZipFormat : public FileFormat
{
public:
	/** @brief Creates a new file format of type XMLZip.
	 */
	XMLZipFormat();

	/** @brief Returns true for data which can be imported from a zip map file.
	 */
	ImportSupportAssumption understands(const char* buffer, int size) const override;

	/** @brief Creates an importer for zip map files.
	 */
	std::unique_ptr<Importer> makeImporter(const QString& path, Map* map, MapView* view) const override;

	/** @brief Creates an exporter for zip map files.
	 */
	std::unique_ptr<Exporter> makeExporter(const QString& path, const Map* map, const MapView* view) const override;
};


}  // namespace OpenOrienteering


#endif // OPENORIENTEERING_XML_ZIP_FORMAT_H
