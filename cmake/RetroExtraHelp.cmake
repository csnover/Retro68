# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Extra help for Mac users that existed in the original bash build script. ]=]

if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "Power Macintosh")
    list(FIND CMAKE_CXX_COMPILE_FEATURES cxx_std_17 has_min_ver)
    if(has_min_ver EQUAL -1)
        message(FATAL_ERROR
            "Apple's version of GCC on Power Macs is too old. Please "
            "explicitly specify the C and C++ compilers using the "
            "-DCMAKE_C_COMPILER AND -DCMAKE_CXX_COMPILER options. "
            "You can install a usable compiler using tigerbrew.")
    endif()
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # present-day Mac users are likely to install dependencies via the homebrew
    # package manager
    if(IS_DIRECTORY /opt/homebrew)
        message(STATUS "Being very helpful and adding /opt/homebrew to CMAKE_PREFIX_PATH")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    endif()

    # or they could be using MacPorts. Default install location is /opt/local
    if(IS_DIRECTORY /opt/local/include)
        message(STATUS "Being very helpful and adding /opt/local to CMAKE_PREFIX_PATH")
        list(APPEND CMAKE_PREFIX_PATH "/opt/local")
    endif()

    message(STATUS "Being very helpful and adding /usr/local to CMAKE_PREFIX_PATH")
    list(APPEND CMAKE_PREFIX_PATH "/usr/local")
endif()
