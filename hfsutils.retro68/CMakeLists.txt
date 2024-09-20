project(hfsutils VERSION 3.2.6)

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckTypeSize)
include(GNUInstallDirs)

add_compile_options(-Wno-pedantic -Wno-unused-parameter)

add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)

check_include_file(unistd.h HAVE_UNISTD_H)
add_compile_definitions($<$<BOOL:${HAVE_UNISTD_H}>:HAVE_UNISTD_H>)

check_include_file(fcntl.h HAVE_FCNTL_H)
add_compile_definitions($<$<BOOL:${HAVE_FCNTL_H}>:HAVE_FCNTL_H>)

check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
add_compile_definitions($<$<BOOL:${HAVE_SYS_IOCTL_H}>:HAVE_SYS_IOCTL_H>)

check_include_file(termios.h HAVE_TERMIOS_H)
add_compile_definitions($<$<BOOL:${HAVE_TERMIOS_H}>:HAVE_TERMIOS_H>)

check_symbol_exists(mktime time.h HAVE_MKTIME)
add_compile_definitions($<$<BOOL:${HAVE_MKTIME}>:HAVE_MKTIME>)

check_symbol_exists(setreuid unistd.h HAVE_SETREUID)
add_compile_definitions($<$<BOOL:${HAVE_SETREUID}>:HAVE_SETREUID>)

check_symbol_exists(strdup string.h HAVE_STRDUP)
add_compile_definitions($<$<BOOL:${HAVE_STRDUP}>:HAVE_STRDUP>)

check_symbol_exists(strerror string.h HAVE_STRERROR)
add_compile_definitions($<$<BOOL:${HAVE_STRERROR}>:HAVE_STRERROR>)

check_symbol_exists(strstr string.h HAVE_STRSTR)
add_compile_definitions($<$<BOOL:${HAVE_STRSTR}>:HAVE_STRSTR>)

check_symbol_exists(strtol stdlib.h HAVE_STRTOL)
add_compile_definitions($<$<BOOL:${HAVE_STRTOL}>:HAVE_STRTOL>)

set(old_extras "${CMAKE_EXTRA_INCLUDE_FILES}")
set(CMAKE_EXTRA_INCLUDE_FILES time.h)
check_type_size("struct tm" STRUCT_TM LANGUAGE C)
add_compile_definitions($<$<NOT:$<BOOL:${HAVE_STRUCT_TM}>>:TM_IN_SYS_TIME>)
set(CMAKE_EXTRA_INCLUDE_FILES "${old_extras}")

add_library(hfs)
add_library(hfsutils::hfs ALIAS hfs)
target_sources(hfs
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../hfsutils/libhfs/hfs.h>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/hfs.h>

PRIVATE
    ../hfsutils/libhfs/apple.h
    ../hfsutils/libhfs/block.c
    ../hfsutils/libhfs/block.h
    ../hfsutils/libhfs/btree.c
    ../hfsutils/libhfs/btree.h
    ../hfsutils/libhfs/data.c
    ../hfsutils/libhfs/data.h
    ../hfsutils/libhfs/file.c
    ../hfsutils/libhfs/file.h
    ../hfsutils/libhfs/hfs.c
    ../hfsutils/libhfs/libhfs.h
    ../hfsutils/libhfs/low.c
    ../hfsutils/libhfs/low.h
    ../hfsutils/libhfs/medium.c
    ../hfsutils/libhfs/medium.h
    ../hfsutils/libhfs/node.c
    ../hfsutils/libhfs/node.h
    ../hfsutils/libhfs/os.c
    ../hfsutils/libhfs/os.h
    ../hfsutils/libhfs/record.c
    ../hfsutils/libhfs/record.h
    ../hfsutils/libhfs/volume.c
    ../hfsutils/libhfs/volume.h
    ../hfsutils/libhfs/version.c
    ../hfsutils/libhfs/version.h
)
target_include_directories(hfs PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../hfsutils/libhfs>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(rsrc)
add_library(hfsutils::rsrc ALIAS rsrc)
target_sources(rsrc
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../hfsutils/librsrc/rsrc.h>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/rsrc.h>

PRIVATE
    ../hfsutils/librsrc/data.c
    ../hfsutils/librsrc/data.h
    ../hfsutils/librsrc/librsrc.h
    ../hfsutils/librsrc/rsrc.c
    ../hfsutils/librsrc/version.c
    ../hfsutils/librsrc/version.h
)
target_link_libraries(rsrc PRIVATE hfs)
target_include_directories(rsrc PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../hfsutils/librsrc>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(hfsutils OBJECT)
target_sources(hfsutils PRIVATE
    ../hfsutils/binhex.c
    ../hfsutils/binhex.h
    ../hfsutils/charset.c
    ../hfsutils/charset.h
    ../hfsutils/copyin.c
    ../hfsutils/copyin.h
    ../hfsutils/copyout.c
    ../hfsutils/copyout.h
    ../hfsutils/crc.c
    ../hfsutils/crc.h
    ../hfsutils/darray.c
    ../hfsutils/darray.h
    ../hfsutils/dlist.c
    ../hfsutils/dlist.h
    ../hfsutils/dstring.c
    ../hfsutils/dstring.h
    ../hfsutils/glob.c
    ../hfsutils/glob.h
    ../hfsutils/suid.c
    ../hfsutils/suid.h
    ../hfsutils/version.c
    ../hfsutils/version.h
    $<$<NOT:$<BOOL:${HAVE_STRDUP}>>:../hfsutils/strdup.c>
    $<$<NOT:$<BOOL:${HAVE_STRERROR}>>:../hfsutils/strerror.c>
    $<$<NOT:$<BOOL:${HAVE_STRSTR}>>:../hfsutils/strstr.c>
    $<$<NOT:$<BOOL:${HAVE_STRTOL}>>:../hfsutils/strtol.c>
)
target_link_libraries(hfsutils PRIVATE hfs)

add_executable(hfsutil)
target_sources(hfsutil PRIVATE
    ../hfsutils/hattrib.c
    ../hfsutils/hattrib.h
    ../hfsutils/hcd.c
    ../hfsutils/hcd.h
    ../hfsutils/hcopy.c
    ../hfsutils/hcopy.h
    ../hfsutils/hcwd.c
    ../hfsutils/hcwd.h
    ../hfsutils/hdel.c
    ../hfsutils/hdel.h
    ../hfsutils/hformat.c
    ../hfsutils/hformat.h
    ../hfsutils/hfsutil.c
    ../hfsutils/hfsutil.h
    ../hfsutils/hls.c
    ../hfsutils/hls.h
    ../hfsutils/hmkdir.c
    ../hfsutils/hmkdir.h
    ../hfsutils/hmount.c
    ../hfsutils/hmount.h
    ../hfsutils/hpwd.c
    ../hfsutils/hpwd.h
    ../hfsutils/hrename.c
    ../hfsutils/hrename.h
    ../hfsutils/hrmdir.c
    ../hfsutils/hrmdir.h
    ../hfsutils/humount.c
    ../hfsutils/humount.h
    ../hfsutils/hvol.c
    ../hfsutils/hvol.h
)
target_link_libraries(hfsutil PRIVATE hfsutils hfs)

install(TARGETS hfsutil)
install(CODE "set(install_bindir ${CMAKE_INSTALL_BINDIR})")
install(CODE [[
    set(src ${CMAKE_INSTALL_PREFIX}/${install_bindir}/$<TARGET_FILE_NAME:hfsutil>)
    foreach(exe IN ITEMS hattrib hcd hcopy hdel hdir hformat hls hmkdir hmount hpwd hrename hrmdir humount hvol)
        set(dest ${CMAKE_INSTALL_PREFIX}/${install_bindir}/${exe})
        message(STATUS "Linking: ${dest}")
        file(CREATE_LINK "${src}" "${dest}")
    endforeach()
    file(REMOVE "${src}")
]])
install(FILES
    ../hfsutils/doc/man/hattrib.1
    ../hfsutils/doc/man/hcd.1
    ../hfsutils/doc/man/hcopy.1
    ../hfsutils/doc/man/hdel.1
    ../hfsutils/doc/man/hdir.1
    ../hfsutils/doc/man/hformat.1
    ../hfsutils/doc/man/hfsutils.1
    ../hfsutils/doc/man/hls.1
    ../hfsutils/doc/man/hmkdir.1
    ../hfsutils/doc/man/hmount.1
    ../hfsutils/doc/man/hpwd.1
    ../hfsutils/doc/man/hrename.1
    ../hfsutils/doc/man/hrmdir.1
    ../hfsutils/doc/man/humount.1
    ../hfsutils/doc/man/hvol.1
    DESTINATION "${CMAKE_INSTALL_MANDIR}")
install(FILES
    ../hfsutils/doc/charset.txt
    ../hfsutils/doc/libhfs.txt
    ../hfsutils/doc/librsrc.txt
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

install(
    TARGETS hfs rsrc
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
    EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
install(FILES
    ../hfsutils/libhfs/hfs.h
    ../hfsutils/librsrc/rsrc.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
