project(pilrc VERSION 3.2.91)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(GNUInstallDirs)

add_library(pilrc-common OBJECT
    bitmap.c
    bitmap.h
    font.c
    font.h
    lex.c
    lex.h
    pilrc.c
    pilrc.h
    plex.c
    plex.h
    restype.c
    restype.h
    std.h
    util.c
    util.h
)

target_compile_options(pilrc-common PUBLIC
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wpointer-arith
    -Wcast-qual
    -Wcast-align
    -Wsign-compare
)

target_include_directories(pilrc-common PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/headers")
configure_file(version.h.in headers/version.h @ONLY)

add_executable(pilrc main.c)
target_link_libraries(pilrc PRIVATE pilrc-common)

find_package(GTK2 QUIET COMPONENTS gtk)
if(TARGET GTK2::gtk)
    add_executable(pilrcui pilrc.rc xwin.c)
    target_link_libraries(pilrcui PRIVATE pilrc-common GTK2::gtk)
    install(TARGETS pilrcui)
else()
    message(STATUS "GTK not found. Not building PilRC UI")
endif()

install(
    DIRECTORY contrib ppmquant example
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}"
)
install(
    DIRECTORY doc/
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
