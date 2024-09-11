#   Copyright 2019 Wolfgang Thaller.
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

cmake_minimum_required(VERSION 3.15)
cmake_policy(VERSION 3.15...3.30)
project(Retro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror=return-type -Wno-multichar")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror=return-type -Werror=strict-prototypes -Wno-multichar")

enable_testing()

if(CMAKE_CROSSCOMPILING)
    message(STATUS "Building cross-compiled libraries")
    add_subdirectory(libretro)

    # add library path so that GCC picks up the freshly-built libretro
#    link_directories(${CMAKE_CURRENT_BINARY_DIR}/libretro)
#    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libretro)
#    set(REZ_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libretro:${CMAKE_INSTALL_PREFIX}/RIncludes" )

    include(add_application)
    add_subdirectory(TestApps)

    if(RETRO_MAC)
        add_subdirectory(AutomatedTests)
        add_subdirectory(Console)
        add_subdirectory(LaunchAPPL)
        add_subdirectory(Samples/Dialog)
        add_subdirectory(Samples/HelloWorld)
        add_subdirectory(Samples/Raytracer)

        if(RETRO_68K)
            add_subdirectory(Samples/Launcher)
            add_subdirectory(Samples/SystemExtension)
        else()
            add_subdirectory(Samples/SharedLibrary)
        endif()

        if(NOT RETRO_CARBON)
            add_subdirectory(Samples/MPWTool)
            add_subdirectory(Samples/WDEF)
        endif()
    endif()
else()
    message(STATUS "Building host applications")

    option(RETRO_PALMOS "Enable Palm OS" OFF)
    option(RETRO_PPC "Enable Mac PowerPC" OFF)
    option(RETRO_68K "Enable Mac 68K" OFF)
    option(RETRO_CARBON "Enable Mac Carbon" OFF)

    # TODO: It would be trivial to only build utilities that apply to the
    # platforms being built; the only reason this is not done currently is
    # to make sure no Apple stuff is broken by work to add Palm OS support
    # (Palm and Mac utilities are so close to the same thing that it seems bad
    # to have two different systems)
    add_subdirectory(hfsutils.retro68)
    add_subdirectory(ConvertDiskImage)
    add_subdirectory(ConvertObj)
    add_subdirectory(Elf2Mac)
    add_subdirectory(LaunchAPPL)
    add_subdirectory(PalmUtils)
    add_subdirectory(PEFTools)
    add_subdirectory(ResourceFiles)
    add_subdirectory(Rez)
    add_subdirectory(libelf)

    function(add_legacy_toolchain name RETRO_ABI RETRO_CARBON)
        set(filename ${name}.toolchain.cmake)
        string(TOLOWER "${filename}" filename)
        configure_file(cmake/legacy-toolchain.cmake.in ${filename} @ONLY)
        install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/${filename}
            DESTINATION share/cmake)
        install(FILES
            cmake/Platform/Retro.cmake
            RENAME ${name}.cmake
            DESTINATION share/cmake/Platform)
        set(RETRO_SUPPORTED_ABIS "${RETRO_SUPPORTED_ABIS}" PARENT_SCOPE)
    endfunction()

    if(RETRO_68K)
        add_legacy_toolchain(Retro68 m68k-apple-macos OFF)
    endif()

    if(RETRO_PPC)
        add_legacy_toolchain(RetroPPC powerpc-apple-macos OFF)
        if(RETRO_CARBON)
            add_legacy_toolchain(RetroCarbon powerpc-apple-macos ON)
        endif()
    endif()

    # No legacy toolchain file for this new target
    if(RETRO_PALMOS)
        list(APPEND RETRO_SUPPORTED_ABIS m68k-none-palmos)
    endif()

    configure_file(cmake/Retro.toolchain.cmake.in Retro.toolchain.cmake @ONLY)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/Retro.toolchain.cmake
        cmake/add_application.cmake
        DESTINATION share/cmake)
    install(FILES cmake/Platform/Retro.cmake DESTINATION share/cmake/Platform)
endif()
