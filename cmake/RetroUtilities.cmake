# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: © Retro68 contributors

#[=[ Useful utility functions. #]=]

#[[
Recursively search within a directory to find a file with the given filename.
Returns the absolute path of the directory containing the file.
#]]
function(find_any_path var basedir #[[ files... ]])
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
Return a list of all CMake executable targets starting from the given directory.
#]]
# SPDX-SnippetBegin
# SPDX-License-Identifier: CC-BY-SA-3.0
# https://stackoverflow.com/a/60232044/252087
function(get_exe_targets _result _dir)
    get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        get_exe_targets(${_result} "${_subdir}")
    endforeach()

    get_directory_property(_sub_targets DIRECTORY "${_dir}" BUILDSYSTEM_TARGETS)
    set(_filtered)
    foreach(_sub_target IN LISTS _sub_targets)
        get_target_property(_target_type "${_sub_target}" TYPE)
        if(_target_type STREQUAL "EXECUTABLE")
            list(APPEND _filtered ${_sub_target})
        endif()
    endforeach()
    set(${_result} ${${_result}} ${_filtered} PARENT_SCOPE)
endfunction()
# SPDX-SnippetEnd
