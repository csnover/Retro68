#   Copyright 2014 Wolfgang Thaller.
#
#   This file is part of Retro68.
#
#   Retro68 is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   Retro68 is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with Retro68.  If not, see <http://www.gnu.org/licenses/>.

add_library(ResourceFiles
    ResourceFork.h ResourceFork.cc
    BinaryIO.h BinaryIO.cc
    ResType.h ResType.cc
    ResourceFile.h ResourceFile.cc)

target_include_directories(ResourceFiles INTERFACE .)
target_link_libraries(ResourceFiles PRIVATE hfsutils::hfs)

if (RETRO_PALMOS)
    target_compile_definitions(ResourceFiles PUBLIC PALMOS)
endif()

find_package(Boost COMPONENTS program_options QUIET)

if (NOT TARGET Boost::program_options)
    message(STATUS "Boost::program_options not found. Building ResourceFiles without ResInfo")
endif()

add_executable(ResInfo ResInfo.cc)
target_link_libraries(ResInfo PRIVATE
    ResourceFiles
    Boost::program_options)

install(TARGETS ResInfo)
