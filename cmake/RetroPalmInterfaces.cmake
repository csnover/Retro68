# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Builds the SDK interface files for Palm OS. #]=]

function(get_subdirs var dir)
    if(IS_DIRECTORY "${dir}")
        get_filename_component(dir "${dir}" ABSOLUTE)
        file(GLOB files LIST_DIRECTORIES true "${dir}/*")
        foreach(file IN LISTS files)
            get_subdirs(${var} "${file}")
        endforeach()
        set(${var} ${dir} ${${var}} PARENT_SCOPE)
    endif()
endfunction()

function(add_sdk dir)
    if(EXISTS "${dir}/include/PalmTypes.h")
        get_filename_component(dir "${dir}" ABSOLUTE)
        message(STATUS "Found Palm OS SDK: ${dir}")
    else()
        # Earlier SDKs used different SYS_TRAP macro that requires compiler
        # support, and Pilot.h instead of PalmOS.h. It seems unlikely anyone
        # will care about this, so just only support 3.5+ for now.
        message(FATAL_ERROR "Palm OS 3.5 or later SDK not found in '${dir}'")
    endif()

    get_subdirs(dirs "${dir}/include")
    # Doing what palmdev-prep does by lexicographically sorting the list
    list(SORT dirs)

    add_library(libinterface INTERFACE)
    target_include_directories(libinterface SYSTEM INTERFACE ${dirs})

    # TODO: Run palmdev-prep and objconv
endfunction()
