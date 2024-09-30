# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Builds the SDK interface and compiler specs files for Mac OS. #]=]

#[[
Recursively search within a directory to find a file with the given filename.
Returns the absolute path of the directory containing the file.
#]]
function(find_any_path var basedir)
    get_filename_component(basedir "${basedir}" ABSOLUTE)
    foreach(file IN LISTS ARGN)
        if(EXISTS "${basedir}/${file}")
            set(${var} "${basedir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    list(TRANSFORM ARGN PREPEND "${basedir}/*/")
    file(GLOB_RECURSE file ${ARGN})
    if(file)
        list(GET file 0 file)
        get_filename_component(file "${file}" DIRECTORY)
        set(${var} "${file}" PARENT_SCOPE)
    else()
        set(${var} NOTFOUND PARENT_SCOPE)
    endif()
endfunction()

#[[
Tries to find a working copy of MPW in the given directory. If found,
creates a libinterface target that uses the copy of MPW.
#]]
function(_find_libinterface_mpw dir)
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

#[[ Adds the libinterface target to the build system. #]]
function(_add_libinterface_target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/libinterface")
    if(RETRO_68K)
        set(lib_dir "lib68k")
    else()
        set(lib_dir "libppc")
    endif()

    set(outputs "${out_dir}/CIncludes/ConditionalMacros.h")

    add_custom_command(
        OUTPUT ${outputs}
        COMMENT "Running multiversal"
        COMMAND "${CMAKE_COMMAND}"
        -E env "PATH=\"${CMAKE_SYSTEM_PROGRAM_PATH}:$ENV{PATH}\""
        "${CMAKE_COMMAND}"
        -DRETRO_68K=${RETRO_68K}
        -DRETRO_PPC=${RETRO_PPC}
        -DRETRO_CARBON=${RETRO_CARBON}
        "-DRETRO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DRETRO_BINARY_DIR=${out_dir}"
        "-DCMAKE_AR=${CMAKE_AR}"
        ${ARGN}
        -P "${CMAKE_CURRENT_LIST_FILE}"
    )
    add_custom_target(libinterface ALL DEPENDS ${outputs})
    set_target_properties(libinterface PROPERTIES
        _LI_CINCLUDES "${out_dir}/CIncludes"
        _LI_RINCLUDES "${out_dir}/RIncludes"
        _LI_LIBRARIES "${out_dir}/${lib_dir}"
    )
endfunction()

#[[
Tries to find a working copy of multiversal in the given directory. If found,
creates a libinterface target that uses multiversal.
#]]
function(_find_libinterface_multiversal dir)
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
        -DRETRO_BUILD_MULTIVERSAL=1
        "-DRETRO_RUBY=${ruby}"
    )
endfunction()

#[[ The configuration step. #]]
function(configure_libinterface)
    _find_libinterface_multiversal("${RETRO_SDK_DIR}")
    if(NOT TARGET libinterface)
        _find_libinterface_mpw("${RETRO_SDK_DIR}")
    endif()
    if(NOT TARGET libinterface)
        message(FATAL_ERROR "Could not find usable SDK in '${RETRO_SDK_DIR}'.")
    endif()

    get_target_property(cincludes libinterface _LI_CINCLUDES)
    get_target_property(rincludes libinterface _LI_RINCLUDES)
    get_target_property(libs libinterface _LI_LIBRARIES)
    set(specs
        "*cpp:\n+ -isystem @cincludes@\n"
        "*cc1:\n+ -isystem @cincludes@\n"
        "*link:\n+ -L@libs@\n")

    find_program(REZ Rez QUIET)
    if(REZ)
        string(APPEND specs
            "*rez_compiler:\n ${REZ}\n"
            "@rez:\n %(rez_compiler) -I @rincludes@ %{o*} %i\n"
            ".r:\n @rez\n"
            ".R:\n @rez\n")
    endif()

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/specs" "${specs}")
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/specs"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/gcc/${RETRO_ABI}/specs")
    install(
        DIRECTORY "${cincludes}" "${rincludes}" "${libs}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/libinterface")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    include("${CMAKE_CURRENT_LIST_DIR}/RetroMacInterfaces/BuildStep.cmake")
    build_libinterface()
else()
    include(GNUInstallDirs)
    configure_libinterface()
endif()
