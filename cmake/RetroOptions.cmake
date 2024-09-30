# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ User-defined options for building host applications. #]=]

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
