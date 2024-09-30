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

cmake_minimum_required(VERSION 3.18)
cmake_policy(VERSION 3.18...3.30)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please specify a "
        "build directory when running CMake "
        "(`${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR} -B <dir_name>`).\n"
        "You should also delete these files, which have already been generated:\n"
        "* ${CMAKE_BINARY_DIR}/CMakeCache.txt\n"
        "* ${CMAKE_BINARY_DIR}/CMakeFiles")
endif()

project(Retro)
enable_testing()

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

# Prefer local CMake modules over ones that are installed to the prefix,
# since otherwise making changes to e.g. add_application.cmake will silently
# not work
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

if(CMAKE_CROSSCOMPILING AND RETRO_BOOTSTRAP)
    message(STATUS "Bootstrapping target")
    add_subdirectory(libretro)

    if(RETRO_MAC)
        set(RETRO_SDK_DIR "" CACHE STRING "Path to Mac OS SDK. Can be MPW or multiversal")
        if(NOT RETRO_SDK_DIR)
            message(WARNING "-DRETRO_SDK_DIR not set. Defaulting to built-in multiversal")
            set(RETRO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/multiversal" CACHE STRING "" FORCE)
        endif()
        include(RetroMacInterfaces)
    endif()
elseif(CMAKE_CROSSCOMPILING)
    message(STATUS "Building target applications")
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

    include(FeatureSummary)
    include(RetroExtraHelp)
    include(RetroOptions)
    include(RetroToolchains)
    include(RetroBuildCompiler)

    add_subdirectory(hfsutils.retro68)
    add_subdirectory(ResourceFiles)

    if(RETRO_MAC)
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

    list(JOIN RETRO_SUPPORTED_ABIS "\n * " abi_string)
    message(STATUS "Building ABIs:\n\n * ${abi_string}\n")

    include(GetTargets)
    get_exe_targets(targets "${CMAKE_CURRENT_LIST_DIR}")
    list(JOIN targets "\n * " targets)
    feature_summary(
        DESCRIPTION "Enabled components:\n\n * ${targets}"
        WHAT ENABLED_FEATURES)
    feature_summary(
        DESCRIPTION "Disabled components:\n"
        WHAT DISABLED_FEATURES)
    feature_summary(
        DESCRIPTION "Missing optional packages:\n"
        WHAT PACKAGES_NOT_FOUND INCLUDE_QUIET_PACKAGES)
endif()
