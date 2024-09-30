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
