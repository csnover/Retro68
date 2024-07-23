include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_executable(palmdev-prep
    dirutils.c
    palmdev-prep.c
    utils.c
    utils.h
)

set(sdks_dir "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/prc-tools/sdks")
target_compile_definitions(palmdev-prep PRIVATE
    STANDARD_EXEC_PREFIX="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/gcc"
    DATA_PREFIX="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/prc-tools"
    PALMDEV_PREFIX="${sdks_dir}"
    PRC_TOOLS_VERSION="3"
)

install(TARGETS palmdev-prep pilrc)
install(DIRECTORY DESTINATION "${sdks_dir}")
