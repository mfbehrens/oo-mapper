#
#    Copyright 2026 The OpenOrienteering contributors
#
#    This file is part of OpenOrienteering.
#
#    OpenOrienteering is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    OpenOrienteering is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.

# QuaZip (https://stachenov.github.io/quazip/) provides the ZIP archive
# container used by the .omapzip format. QuaZip is a CMake-native library, so
# FetchContent downloads the pinned source tarball and adds it to the build,
# exposing the "QuaZip" target. Consumers use the "QuaZip::QuaZip" alias.

# The Mapper build targets Qt5, so force QuaZip to use Qt5 (it would otherwise
# prefer Qt6). Must be set in the cache before the subproject is added.
set(QUAZIP_QT_MAJOR_VERSION 5 CACHE STRING "Qt version to use for QuaZip (4, 5 or 6)" FORCE)

# Build a static library (consistent with the other embedded dependencies).
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)
FetchContent_Declare(QuaZip
  URL https://github.com/stachenov/quazip/archive/refs/tags/v1.5.tar.gz
  URL_HASH MD5=e5e266ce8a0acf0d55496d104ee09ece
)
FetchContent_MakeAvailable(QuaZip)

# Normalize the target name for consumers, matching QuaZip's own imported name.
if(TARGET QuaZip AND NOT TARGET QuaZip::QuaZip)
	add_library(QuaZip::QuaZip ALIAS QuaZip)
endif()
