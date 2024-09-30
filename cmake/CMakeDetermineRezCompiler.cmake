# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: https://cmake.org/licensing

if(NOT CMAKE_Rez_COMPILER)
    # prefer the environment variable REZ
    if(NOT $ENV{REZ} STREQUAL "")
        get_filename_component(CMAKE_Rez_COMPILER_INIT $ENV{REZ} PROGRAM PROGRAM_ARGS CMAKE_Rez_FLAGS_ENV_INIT)
        if(CMAKE_Rez_FLAGS_ENV_INIT)
            set(CMAKE_Rez_COMPILER_ARG1 "${CMAKE_Rez_FLAGS_ENV_INIT}" CACHE STRING "Arguments to Rez compiler")
        endif()
        if(NOT EXISTS ${CMAKE_Rez_COMPILER_INIT})
            message(FATAL_ERROR "Could not find compiler set in environment variable REZ:\n$ENV{REZ}.")
        endif()
    endif()

    # next try prefer the compiler specified by the generator
    if(CMAKE_GENERATOR_REZ)
        if(NOT CMAKE_Rez_COMPILER_INIT)
            set(CMAKE_Rez_COMPILER_INIT ${CMAKE_GENERATOR_REZ})
        endif()
    endif()

    # finally list compilers to try
    if(CMAKE_Rez_COMPILER_INIT)
        set(_CMAKE_Rez_COMPILER_LIST     ${CMAKE_Rez_COMPILER_INIT})
        set(_CMAKE_Rez_COMPILER_FALLBACK ${CMAKE_Rez_COMPILER_INIT})
    elseif(NOT _CMAKE_Rez_COMPILER_LIST)
        set(_CMAKE_Rez_COMPILER_LIST Rez)
    endif()

    # Find the compiler.
    find_program(CMAKE_Rez_COMPILER NAMES ${_CMAKE_Rez_COMPILER_LIST} DOC "Rez compiler")
    if(_CMAKE_Rez_COMPILER_FALLBACK AND NOT CMAKE_Rez_COMPILER)
        set(CMAKE_Rez_COMPILER "${_CMAKE_Rez_COMPILER_FALLBACK}" CACHE FILEPATH "Rez compiler" FORCE)
    endif()
    unset(_CMAKE_Rez_COMPILER_FALLBACK)
    unset(_CMAKE_Rez_COMPILER_LIST)
endif()

mark_as_advanced(CMAKE_Rez_COMPILER)

set(CMAKE_Rez_OUTPUT_EXTENSION .rsrc)
set(CMAKE_Rez_COMPILER_ENV_VAR "REZ")

# configure variables set in this file for fast reload later on
configure_file(${CMAKE_CURRENT_LIST_DIR}/CMakeRezCompiler.cmake.in
    ${CMAKE_BINARY_DIR}/${CMAKE_FILES_DIRECTORY}/${CMAKE_VERSION}/CMakeRezCompiler.cmake IMMEDIATE @ONLY)
