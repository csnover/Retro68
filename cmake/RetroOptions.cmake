# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ User-defined options for building host applications. #]=]

if(CMAKE_CROSSCOMPILING AND RETRO_BOOTSTRAP)
    if(RETRO_MAC)
        set(RETRO_SDK_DIR "" CACHE FILEPATH "Path to Mac OS SDK. Can be MPW or multiversal")
        if(NOT RETRO_SDK_DIR)
            message(WARNING "-DRETRO_SDK_DIR not set. Defaulting to built-in multiversal")
            set(RETRO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/multiversal" CACHE STRING "" FORCE)
        endif()
    else()
        set(RETRO_SDK_DIR "" CACHE FILEPATH "Path to Palm OS SDK")
        if(NOT RETRO_SDK_DIR)
            message(FATAL_ERROR "Missing required -DRETRO_SDK_DIR")
        endif()
    endif()
else()
    option(RETRO_THIRDPARTY "Build third-party components" ON)
    add_feature_info("binutils and GCC" RETRO_THIRDPARTY "")

    option(RETRO_PALMOS "Enable Palm OS" OFF)
    add_feature_info("Palm OS support" RETRO_PALMOS "")

    option(RETRO_PPC "Enable Mac PowerPC" OFF)
    add_feature_info("PowerPC Mac support" RETRO_PPC "")

    option(RETRO_68K "Enable Mac 68K" OFF)
    add_feature_info("68K Mac support" RETRO_68K "")

    option(RETRO_CARBON "Enable Mac Carbon" OFF)
    add_feature_info("Carbon API support" RETRO_CARBON "")

    if(RETRO_CARBON AND NOT RETRO_PPC)
        message(WARNING "RETRO_CARBON implicitly enables RETRO_PPC")
        set(RETRO_PPC ON CACHE BOOL "" FORCE)
    endif()

    # Match the convenience options set by the toolchain file so there is no
    # confusion between cross-compiling and host CMake environments
    if(RETRO_PPC OR RETRO_68K)
        set(RETRO_MAC ON)
    endif()
endif()
