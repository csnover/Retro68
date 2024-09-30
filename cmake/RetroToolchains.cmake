# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Builds RETRO_SUPPORTED_ABIS list and cross-compiler CMake toolchains. #]=]

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
    cmake/CMakeDetermineRezCompiler.cmake
    cmake/CMakeRezCompiler.cmake.in
    cmake/CMakeRezInformation.cmake
    cmake/CMakeTestRezCompiler.cmake
    DESTINATION share/cmake)
install(FILES cmake/Platform/Retro.cmake DESTINATION share/cmake/Platform)
