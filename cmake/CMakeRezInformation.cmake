# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: https://cmake.org/licensing

if(CMAKE_USER_MAKE_RULES_OVERRIDE)
    include(${CMAKE_USER_MAKE_RULES_OVERRIDE} RESULT_VARIABLE _override)
    set(CMAKE_USER_MAKE_RULES_OVERRIDE "${_override}")
endif()

if(CMAKE_USER_MAKE_RULES_OVERRIDE_Rez)
    include(${CMAKE_USER_MAKE_RULES_OVERRIDE_Rez} RESULT_VARIABLE _override)
    set(CMAKE_USER_MAKE_RULES_OVERRIDE_Rez "${_override}")
endif()

set(CMAKE_Rez_FLAGS_INIT "$ENV{REZFLAGS} ${CMAKE_Rez_FLAGS_INIT}")

# These are the only types of flags that should be passed to the rc
# command, if COMPILE_FLAGS is used on a target this will be used
# to filter out any other flags
set(CMAKE_Rez_FLAG_REGEX "^-(D|define|I|include|a|append|t|type|c|creator|copy|cc|d|debug|data)")

set(CMAKE_INCLUDE_FLAG_Rez "-I ")
if(NOT CMAKE_Rez_COMPILE_OBJECT)
    set(CMAKE_Rez_COMPILE_OBJECT
        "<CMAKE_Rez_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> <SOURCE>")
endif()

set(CMAKE_Rez_INFORMATION_LOADED 1)
