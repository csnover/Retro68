# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ The build step for generating Mac OS SDK files. #]=]

#[[ Transforms MPW CIncludes to support modern compilers on modern OS. #]]
function(build_mpw_cincludes dirs out_dir)
    # Maybe Carbon was detected in a separate directory, causing there to be
    # more than one include directory
    foreach(dir IN LISTS dirs)
        # Must glob and then filter because glob is case-insensitive on
        # case-insensitive filesystems
        file(GLOB include_files RELATIVE "${dir}"
            "${dir}/*.[Hh]" "${dir}/CoreFoundation/*.[Hh]")

        # Filter by file names.
        # Some CIncludes packages include the MPW standard library.
        # Header files from that standard library would overwrite
        # newlib header files and stop things from working.

        # Apple/MPW standard library internals
        list(FILTER include_files EXCLUDE REGEX [[(Def|^FSpio)\.h$]])

        # whitelist all uppercase headers
        list(APPEND valid "[A-Z].*")

        # whitelist OpenTransport
        list(APPEND valid
            cred dlpi miioccom mistream modnames strlog stropts strstat tihdr)

        # Non-standard floating point headers that don't conflict
        list(APPEND valid ddrt fp)

        # newlib does not provide fenv.h, so use Apple's
        list(APPEND valid fenv)

        # veclib headers
        list(APPEND valid "v.*")

        list(JOIN valid "|" valid)

        # unsupported:    intrinsics.h   perf.h

        # TODO: Verify this is not necessary with specs file providing headers
        # from a separate directory instead of using a bunch of symbolic links.
        # On case-insensitive file systems, there will be some conflicts with
        # newlib. For now, universal interfaces get the right of way.
        #rm -f "$1/Threads.h"        # threads.h: does not currently work anyways
        #rm -f "$1/Memory.h"         # memory.h: non-standard aliasof string.h
        #cp "$1/strings.h" "$1/bsdstrings.h"
        #rm -f "$1/Strings.h"        # strings.h: traditional bsd string functions

        # all other (lowercase) headers: conflict with GCC or newlib headers
        set(valid "^(${valid})\\.h$")
        list(FILTER include_files INCLUDE REGEX "${valid}")

        set(patch_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Headers")
        foreach(file IN LISTS include_files)
            file(READ "${dir}/${file}" CONTENT)
            string(REPLACE "\r" "\n" CONTENT "${CONTENT}")

            if(file STREQUAL "ConditionalMacros.h")
                string(REPLACE "__GNUC__" "__GNUC_DISABLED__" CONTENT "${CONTENT}")
                configure_file("${patch_dir}/${file}.in" "${out_dir}/${file}")
            elseif(file STREQUAL "MixedMode.h")
                string(REPLACE "Opaque##name##*" "Opaque##name *" CONTENT "${CONTENT}")
                file(WRITE "${out_dir}/${file}" "${CONTENT}")
            elseif(file STREQUAL "CGBase.h" OR file STREQUAL "fp.h")
                configure_file("${patch_dir}/${file}.in" "${out_dir}/${file}")
            else()
                file(WRITE "${out_dir}/${file}" "${CONTENT}")
            endif()
        endforeach()
    endforeach()

    foreach(file IN ITEMS Types.h Memory.h Windows.h Errors.h)
        if(NOT EXISTS "${out_dir}/${file}")
            file(WRITE "${out_dir}/${file}" "#include \"Mac${file}\"\n")
        elseif(NOT EXISTS "${out_dir}/Mac${file}")
            file(WRITE "${out_dir}/Mac{$file}" "#include \"${file}\"\n")
        endif()
    endforeach()
endfunction()

#[[ Transforms MPW RIncludes to support modern compilers on modern OS. #]]
function(build_mpw_rincludes dir out_dir)
    # Must glob and then filter because glob is case-insensitive on
    # case-insensitive filesystems
    file(GLOB include_files RELATIVE "${dir}" "${dir}/*.[Rr]")
    list(FILTER include_files EXCLUDE REGEX ^[a-z])

    foreach(file IN LISTS include_files)
        file(READ "${dir}/${file}" CONTENT)
        string(REPLACE "\r" "\n" CONTENT "${CONTENT}")
        file(WRITE "${out_dir}/${file}" "${CONTENT}")
    endforeach()
endfunction()

#[[ Converts MPW 68K static libraries to GCC static libraries. #]]
function(build_mpw_libraries_68k dir out_dir)
    file(GLOB libraries RELATIVE "${dir}" "${dir}/*.[Oo]")

    message(STATUS "Converting 68K static libraries...")
    foreach(file IN LISTS libraries)
        get_filename_component(basename "${file}" NAME_WLE)
        set(out_file "lib${basename}.a")

        message(STATUS "  ${file} => ${out_file}")
        execute_process(
            COMMAND "${RETRO_CONVERT_OBJ}" "${dir}/${file}"
            COMMAND "${CMAKE_ASM_COMPILER}" -o "${out_dir}/${file}" -c -x assembler -
            COMMAND_ERROR_IS_FATAL LAST
            RESULTS_VARIABLE result)
        if(results STREQUAL "0;0")
            execute_process(
                COMMAND "${CMAKE_AR}" cqs
                    "${out_dir}/${out_file}"
                    "${out_dir}/${file}"
                COMMAND_ERROR_IS_FATAL ANY)
        endif()
        file(REMOVE "${out_dir}/${file}")
    endforeach()
endfunction()

#[[ Copies pregenerated GCC PPC static libraries. #]]
function(copy_mpw_libraries_ppc_shared dir out_dir)
    message(STATUS "Copying readymade PowerPC import libraries...")
    file(GLOB libraries RELATIVE "${dir}" "${dir}/*.a")
    foreach(file IN LISTS libraries)
        message(STATUS "  ${file} => ${file}")
        file(COPY "${dir}/${file}" "${out_dir}/${file}")
    endforeach()
endfunction()

#[[ Converts MPW PPC shared libraries to GCC libraries. #]]
function(build_mpw_libraries_ppc_shared dir fallback_dir out_dir)
    # Maybe Carbon was detected in a separate directory, causing there to be
    # more than one shared directory
    list(GET dir 0 main_dir)

    if(RETRO_RES_INFO AND RETRO_MAKE_IMPORT)
        execute_program(
            COMMAND "${RETRO_RES_INFO}" -n "${main_dir}/InterfaceLib"
            OUTPUT_VARIABLE result)
    else()
        set(result "0")
    endif()

    if(result STREQUAL "0")
        message(WARNING "Couldn't read resource fork for InterfaceLib. "
                "Falling back to included import libraries.")
        copy_mpw_libraries_ppc_shared("${fallback_dir}" "${out_dir}")
    else()
        message(STATUS "Building PowerPC import libraries...")
        foreach(shared_dir IN LISTS dir)
            file(GLOB libraries RELATIVE "${shared_dir}" "${shared_dir}/*")
            foreach(file IN LISTS libraries)
                get_filename_component(basename "${file}" NAME)
                set(out_file "lib${basename}.a")

                message(STATUS "  ${file} => ${out_file}")
                execute_process(COMMAND
                    "${RETRO_MAKE_IMPORT}"
                    "${shared_dir}/${file}"
                    "${out_dir}/${out_file}"
                    COMMAND_ERROR_IS_FATAL ANY)
            endforeach()
        endforeach()
    endif()
endfunction()

#[[ Converts MPW PPC static libraries to GCC static libraries. #]]
function(build_mpw_libraries_ppc_static dir out_dir)
    file(GLOB objects RELATIVE "${static_dir}" "${static_dir}/OpenT*.o"
        "${static_dir}/CarbonAccessors.o" "${static_dir}/CursorDevicesGlue.o")
    foreach(file IN LISTS objects)
        get_filename_component(basename "${file}" NAME_WE)
        set(out_file "lib${basename}.a")

        message(STATUS "  ${file} => ${out_file}")
        execute_process(COMMAND
            "${CMAKE_AR}" cqs
            "${out_dir}/${out_file}"
            "${static_dir}/${file}"
            COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
endfunction()

#[[ Builds libinterface for GCC. #]]
function(build_libinterface)
    if(RETRO_BUILD_MULTIVERSAL)
        execute_process(
            COMMAND "${RETRO_RUBY}" make-multiverse.rb
                -G CIncludes
                -o "${RETRO_BINARY_DIR}"
            WORKING_DIRECTORY "${RETRO_SOURCE_DIR}/multiversal"
            RESULT_VARIABLE result
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Error running multiversal.")
        endif()
        if(RETRO_PPC)
            copy_mpw_libraries_ppc_shared(
                "${RETRO_SOURCE_DIR}/ImportLibraries"
                "${RETRO_BINARY_DIR}/libppc")
        endif()
    else()
        build_mpw_cincludes("${RETRO_CINCLUDES_DIR}" "${RETRO_BINARY_DIR}/CIncludes")
        build_mpw_rincludes("${RETRO_RINCLUDES_DIR}" "${RETRO_BINARY_DIR}/RIncludes")
        if(RETRO_68K)
            file(MAKE_DIRECTORY "${RETRO_BINARY_DIR}/lib68k")
            build_mpw_libraries_68k("${RETRO_LIBS_DIR}" "${RETRO_BINARY_DIR}/lib68k")
        elseif(RETRO_PPC)
            file(MAKE_DIRECTORY "${RETRO_BINARY_DIR}/libppc")
            build_mpw_libraries_ppc_shared("${RETRO_SHAREDLIBS_DIR}"
                "${RETRO_SOURCE_DIR}/ImportLibraries"
                "${RETRO_BINARY_DIR}/libppc")
            build_mpw_libraries_ppc_static("${RETRO_LIBS_DIR}"
                "${RETRO_BINARY_DIR}/libppc")
        endif()
    endif()
endfunction()
