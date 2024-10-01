# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Builds the SDK interface and compiler specs files for Mac OS. #]=]

include(GNUInstallDirs)
include(RetroUtilities)

#[[
Tries to find a working copy of MPW in the given directory. If found,
creates a libinterface target that uses the copy of MPW.
#]]
function(_try_add_mpw dir)
    find_any_path(cincludes "${dir}" ConditionalMacros.h)
    find_any_path(rincludes "${dir}" ConditionalMacros.r)

    if(RETRO_CARBON)
        find_any_path(carbon_cincludes "${dir}" Carbon.h)

        if(NOT carbon_cincludes STREQUAL cincludes)
            list(APPEND cincludes "${carbon_cincludes}")
            message(WARNING
                "Carbon.h not in same directory as ConditionalMacros.h. "
                "This is confusing.")
        endif()
    endif()

    if(RETRO_68K)
        find_any_path(libs "${dir}" Interface.o libInterface.a)
    elseif(RETRO_PPC)
        find_any_path(libs "${dir}" OpenTransportAppPPC.o)
        find_any_path(sharedlibs "${dir}" InterfaceLib)

        if(NOT sharedlibs)
            message(WARNING
                "Could not find InterfaceLib. Using bundled ImportLibraries.")
            set(sharedlibs "${CMAKE_CURRENT_SOURCE_DIR}/ImportLibraries")
        endif()

        if(RETRO_CARBON)
            find_any_path(carbon_libs "${dir}" CarbonLib)

            if(NOT carbon_libs STREQUAL sharedlibs)
                list(APPEND sharedlibs "${carbon_libs}")
                message(WARNING
                    "CarbonLib not in same directory as InterfaceLib. "
                    "This is confusing.")
            endif()
        endif()
    endif()

    if(NOT cincludes OR NOT rincludes OR NOT libs OR (RETRO_PPC AND NOT sharedlibs))
        return()
    endif()

    if(RETRO_68K)
        enable_language(ASM)
        find_program(convert_obj ConvertObj REQUIRED)
        set(extra_args
            "-DCMAKE_ASM_COMPILER=${CMAKE_ASM_COMPILER}"
            "-DRETRO_CONVERT_OBJ=${convert_obj}"
        )
    elseif(RETRO_PPC)
        find_program(res_info ResInfo)
        find_program(make_import MakeImport)
        set(extra_args
            "-DRETRO_SHAREDLIBS_DIR=${sharedlibs}"
            "-DRETRO_RES_INFO=${res_info}"
            "-DRETRO_MAKE_IMPORT=${make_import}"
        )
    endif()

    get_filename_component(dir "${dir}" ABSOLUTE)
    message(STATUS "Found MPW: ${dir}")

    _add_libinterface_target(
        "-DRETRO_CINCLUDES_DIR=${cincludes}"
        "-DRETRO_RINCLUDES_DIR=${rincludes}"
        "-DRETRO_LIBS_DIR=${libs}"
        ${extra_args}
    )
endfunction()

#[[
Tries to find a working copy of multiversal in the given directory. If found,
creates a libinterface target that uses multiversal.
#]]
function(_try_add_multiversal dir)
    if(NOT EXISTS "${dir}/make-multiverse.rb"
        AND dir STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/multiversal")
        find_program(git git QUIET)
        if(git)
            execute_process(
                COMMAND "${git}" submodule update --init
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_QUIET ERROR_QUIET
                RESULT_VARIABLE git_init
            )
            if(NOT git_init EQUAL 0)
                set(error "running `git submodule update --init` failed. "
                    "Try running it yourself")
            elseif(NOT EXISTS "${dir}/make-multiverse.rb")
                set(error "running `git submodule update --init` did not "
                    "solve the problem")
            endif()
        else()
            set(error "could not find `git` to try fixing it. Try running "
                "`git submodule update --init` yourself")
        endif()

        if(error)
            message(WARNING
                "Could not find make-multiverse.rb in '${multiversal_dir}' "
                "and ${error}.")
        endif()
    endif()

    find_any_path(multiverse "${dir}" make-multiverse.rb)

    if(NOT multiverse)
        return()
    endif()

    find_program(ruby ruby)
    if(NOT ruby)
        message(STATUS "Ruby not found. Not building multiversal")
        return()
    endif()

    message(STATUS "Found multiversal: ${multiverse}")
    _add_libinterface_target(
        "-DRETRO_MULTIVERSAL_DIR=${multiverse}"
        "-DRETRO_RUBY=${ruby}"
    )
endfunction()

#[[ Adds the libinterface target to the build system. #]]
function(_add_libinterface_target #[[ extra_args... ]])
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/libinterface")
    if(RETRO_68K)
        set(lib_dir "lib68k")
    else()
        set(lib_dir "libppc")
    endif()

    # Theoretically, all output files should be covered in case any of the
    # output files get modified, but in practice they are going into the build
    # directory anyway so it does not need to be super robust
    set(outputs "${out_dir}/CIncludes/ConditionalMacros.h")

    add_custom_command(
        OUTPUT ${outputs}
        COMMENT "Building libinterface"
        COMMAND "${CMAKE_COMMAND}"
        -E env "PATH=\"${CMAKE_SYSTEM_PROGRAM_PATH}:$ENV{PATH}\""
        "${CMAKE_COMMAND}"
        -DRETRO_68K=${RETRO_68K}
        -DRETRO_PPC=${RETRO_PPC}
        -DRETRO_CARBON=${RETRO_CARBON}
        "-DRETRO_PRECOMPILED_PPCLIBS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/ImportLibraries"
        "-DRETRO_BINARY_DIR=${out_dir}"
        "-DCMAKE_AR=${CMAKE_AR}"
        ${ARGN}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ConvertInterfaces/ConvertInterfaces.cmake"
    )

    # libinterface should always build even if nothing links to it since it is
    # a valid target for installation on its own
    add_custom_target(libinterface-build ALL DEPENDS ${outputs})

    # libretro needs an interface target to link to so it can be built before
    # libinterface has been installed
    add_library(libinterface INTERFACE)
    add_dependencies(libinterface libinterface-build)
    target_include_directories(libinterface SYSTEM INTERFACE "${out_dir}/CIncludes")
    target_link_directories(libinterface INTERFACE "${out_dir}/${lib_dir}")

    # Install SDK to the side because some header filenames conflict with
    # standard library headers, they can be shared by all Mac OS targets, and it
    # makes it easier to upgrade or replace the compiler and SDK in isolation
    set(install_dir "${CMAKE_INSTALL_DATADIR}/Retro68/MacSDK")
    install(
        DIRECTORY "${out_dir}/CIncludes" "${out_dir}/RIncludes" "${out_dir}/${lib_dir}"
        DESTINATION "${install_dir}")

    # Specs file teaches the compiler to use the SDK
    set(specs_destination "${CMAKE_INSTALL_LIBDIR}/gcc/${RETRO_ABI}/specs")
    install(CODE "
        set(specs_source \"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RetroMacInterfaces.specs.in\")
        set(specs_destination \"${specs_destination}\")
        set(cincludes \"${install_dir}/CIncludes\")
        set(rincludes \"${install_dir}/RIncludes\")
        set(libs \"${install_dir}/${libdir}\")")
    install(CODE [[
        message(STATUS "Installing: ${CMAKE_INSTALL_PREFIX}/${specs_destination}")
        configure_file("${specs_source}" "${CMAKE_INSTALL_PREFIX}/${specs_destination}" @ONLY)
    ]])
endfunction()

#[[ Try to detect and add a compatible SDK from the given directory. #]]
function(add_sdk dir)
    _try_add_multiversal("${dir}")
    if(NOT TARGET libinterface)
        _try_add_mpw("${dir}")
    endif()
    if(NOT TARGET libinterface)
        message(FATAL_ERROR "Could not find usable SDK in '${dir}'.")
    endif()
endfunction()
