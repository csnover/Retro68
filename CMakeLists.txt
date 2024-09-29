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
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

add_compile_options(
    -Wall
    -Wextra
    -pedantic
    -Wno-multichar
    -Werror=return-type
    $<$<COMPILE_LANGUAGE:C>:-Werror=strict-prototypes>
)

enable_testing()

if(CMAKE_CROSSCOMPILING)
    # Prefer local CMake modules over ones that are installed to the prefix,
    # since otherwise making changes to e.g. add_application.cmake will silently
    # not work
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

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

    if(RETRO_CARBON AND NOT RETRO_PPC)
        message(WARNING "RETRO_CARBON implicitly enables RETRO_PPC")
        set(RETRO_PPC ON)
    endif()

    # Match the convenience options set by the toolchain file so there is no
    # confusion between cross-compiling and host CMake environments
    if(RETRO_PPC OR RETRO_68K)
        set(RETRO_MAC ON)
    endif()

    set(RETRO_SUPPORTED_ABIS)

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
        list(APPEND RETRO_SUPPORTED_ABIS "${RETRO_ABI}")
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

    if(NOT RETRO_SUPPORTED_ABIS)
        message(FATAL_ERROR "No ABIs were enabled. Specify one or more of:\n"
            "  -DRETRO_PALMOS=ON\n  -DRETRO_PPC=ON\n  -DRETRO_68K=ON")
    endif()

    configure_file(cmake/Retro.toolchain.cmake.in Retro.toolchain.cmake @ONLY)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/Retro.toolchain.cmake
        cmake/add_application.cmake
        DESTINATION share/cmake)
    install(FILES cmake/Platform/Retro.cmake DESTINATION share/cmake/Platform)

    add_subdirectory(ResourceFiles)

    if(RETRO_MAC)
        add_subdirectory(hfsutils.retro68)
        add_subdirectory(ConvertDiskImage)
        add_subdirectory(LaunchAPPL)
        add_subdirectory(Rez)
    endif()

    if(RETRO_68K)
        add_subdirectory(ConvertObj)
    endif()

    if(RETRO_68K OR RETRO_PALMOS)
        add_subdirectory(Elf2Mac)
        add_subdirectory(libelf)
    endif()

    if(RETRO_PALMOS)
        add_subdirectory(PalmUtils)
    endif()

    if(RETRO_PPC)
        add_subdirectory(PEFTools)
    endif()
endif()
