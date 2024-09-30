# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Builds binutils and GCC for each ABI in RETRO_SUPPORTED_ABIS. ]=]

include(ExternalProject)

find_program(_build_compiler_make_cmd NAMES gmake nmake make REQUIRED)

# Logic from ExternalProject.cmake
if(CMAKE_GENERATOR MATCHES "Makefiles")
    set(_build_compiler_jobaware_cmd "$(MAKE)")
else()
    set(_build_compiler_jobaware_cmd "${_build_compiler_make_cmd}")
endif()

set(_build_compiler_extra_args)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
    set(_build_compiler_extra_args "COMMAND_ERROR_IS_FATAL ANY")
endif()

function(build_compiler abi)
    # autoconf does not like receiving empty arguments and CMake will still send
    # a generator expression as an argument even if it is empty, so use
    # variables instead
    set(binutils_args)
    set(gcc_args)
    if (abi STREQUAL powerpc-apple-macos)
        set(binutils_args "--disable-plugins")
        set(gcc_args "--disable-lto")
    endif()

    ExternalProject_Add("binutils-${abi}"
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/binutils"
        PREFIX "binutils-${abi}"
        BINARY_DIR "binutils-${abi}/build"
        STAMP_DIR "binutils-${abi}/stamp"
        # Even though binutils and GCC are basically always built together, GCC
        # needs to get a fully installed copy of binutils to compile target
        # runtime libraries instead of using the intermediate binutils build,
        # but we do not want to do any actual installing until
        # `cmake --install`, so make the build step of binutils install to a
        # fake prefix
        INSTALL_DIR "binutils-${abi}/out"
        INSTALL_COMMAND "${_build_compiler_jobaware_cmd}" install "prefix=<INSTALL_DIR>"
        CONFIGURE_COMMAND
            "<SOURCE_DIR>/configure"
            "--prefix=${CMAKE_INSTALL_PREFIX}"
            "--target=${abi}"
            ${binutils_args}
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "CFLAGS=${CMAKE_C_FLAGS}"
            "CXXFLAGS=${CMAKE_CXX_FLAGS}"
    )

    ExternalProject_Get_Property("binutils-${abi}" BINARY_DIR)
    set(binutils_build_dir "${BINARY_DIR}")
    ExternalProject_Get_Property("binutils-${abi}" INSTALL_DIR)
    set(binutils_dir "${INSTALL_DIR}")
    install(CODE "
        message(STATUS \"Installing binutils for ${abi}\")
        execute_process(
            COMMAND \"${_build_compiler_make_cmd}\" install
            ${_build_compiler_extra_args}
            WORKING_DIRECTORY \"${binutils_build_dir}\")")

    ExternalProject_Add("gcc-${abi}"
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gcc"
        PREFIX "gcc-${abi}"
        BINARY_DIR "gcc-${abi}/build"
        STAMP_DIR "gcc-${abi}/stamp"
        # We do not want to do any actual installing until `cmake --install`
        INSTALL_COMMAND ""
        CONFIGURE_COMMAND
            "${CMAKE_COMMAND}" -E env "PATH=${binutils_dir}/bin:$ENV{PATH}"
            "<SOURCE_DIR>/configure"
            "--prefix=${CMAKE_INSTALL_PREFIX}"
            "--target=${abi}"
            "--with-build-time-tools=${binutils_dir}/${abi}/bin"
            --enable-languages=c,c++
            --disable-libssp
            ${gcc_args}
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "CFLAGS=${CMAKE_C_FLAGS}"
            "CXXFLAGS=${CMAKE_CXX_FLAGS}"
            "target_configargs=--disable-nls --enable-libstdcxx-dual-abi=no --disable-libstdcxx-verbose"
        DEPENDS "binutils-${abi}"
    )

    ExternalProject_Get_Property("gcc-${abi}" BINARY_DIR)
    set(gcc_build_dir "${BINARY_DIR}")
    install(CODE "
        message(STATUS \"Installing GCC for ${abi}\")
        execute_process(
            COMMAND \"${_build_compiler_make_cmd}\" install
            ${_build_compiler_extra_args}
            WORKING_DIRECTORY \"${gcc_build_dir}\")")
endfunction()

foreach(abi IN LISTS RETRO_SUPPORTED_ABIS)
    if(RETRO_THIRDPARTY
        OR NOT EXISTS "${CMAKE_INSTALL_PREFIX}/${abi}/bin/as"
        OR NOT EXISTS "${CMAKE_INSTALL_PREFIX}/bin/${abi}-gcc"
    )
        if(NOT RETRO_THIRDPARTY)
            message(WARNING
                "Could not find compiler for ${abi} in ${CMAKE_INSTALL_PREFIX}. "
                "Ignoring -DRETRO_THIRDPARTY=OFF for this ABI.")
        endif()

        build_compiler(${abi})
    endif()

    # TODO: Probably libretro should be treated as a totally independent project
    # ExternalProject_Add("libretro-${abi}"
    #     PREFIX "${CMAKE_CURRENT_BINARY_DIR}/bootstrap"
    #     SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    #     CMAKE_ARGS
    #         "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_BINARY_DIR}/Retro.toolchain.cmake"
    #         -DRETRO_ABI=${abi}
    #         -DRETRO_BOOTSTRAP=ON
    #         -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    #     DEPENDS "binutils-${abi}" "gcc-${abi}"
    # )
endforeach()
