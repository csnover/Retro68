#   Copyright 2012 Wolfgang Thaller.
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

find_package(Boost COMPONENTS wave filesystem system thread regex program_options REQUIRED)

find_package(BISON 3.0.2 REQUIRED)

BISON_TARGET(RezLib_bison RezParser.yy RezParser.generated.cc DEFINES_FILE RezParser.generated.hh)

add_library(RezLib
    RezParser.yy
    ${BISON_RezLib_bison_OUTPUTS}

    RezLexer.h
    RezLexer.cc
    RezLexerWaveToken.h
    RezLexerNextToken.cc

    RezWorld.cc
    RezWorld.h

    ResourceDefinitions.cc
    ResourceDefinitions.h

    Expression.cc
    Expression.h

    ResourceCompiler.cc
    ResourceCompiler.h

    ResSpec.h

    Diagnostic.h Diagnostic.cc)

target_include_directories(RezLib PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RezLib
    PUBLIC
        ResourceFiles
    PRIVATE
        Boost::headers
        Boost::wave
        Boost::filesystem
        Boost::system
        Boost::thread
        Boost::regex
        Boost::program_options)

add_executable(Rez Rez.cc)
target_link_libraries(Rez PRIVATE RezLib ResourceFiles)

install(TARGETS Rez)

add_subdirectory(Test)
