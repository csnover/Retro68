find_package(Boost COMPONENTS filesystem program_options)

add_executable(LaunchAPPL)
target_sources(LaunchAPPL PRIVATE
    LaunchAPPL.cc
    MakeExecutable.cc

    LaunchMethod.h LaunchMethod.cc
    Launcher.h Launcher.cc

    Utilities.h Utilities.cc

    Executor.h Executor.cc
    SSH.h SSH.cc
    StreamBasedLauncher.h StreamBasedLauncher.cc
    Serial.h Serial.cc
    TCP.h TCP.cc
    SharedFile.h SharedFile.cc)

if(APPLE)
    target_sources(LaunchAPPL PRIVATE
        Classic.h Classic.cc
        Carbon.h Carbon.cc)
    find_library(APPLICATIONSERVICES_FW ApplicationServices)
    target_link_libraries(LaunchAPPL PRIVATE ${APPLICATIONSERVICES_FW})
endif()

target_compile_definitions(LaunchAPPL PRIVATE RETRO68_PREFIX="${CMAKE_INSTALL_PREFIX}")
target_link_libraries(LaunchAPPL PRIVATE
    ResourceFiles
    LaunchAPPLCommon
    Boost::headers
    Boost::filesystem
    Boost::program_options)

find_path(HFS_INCLUDE_DIR NAMES hfs.h)
find_library(HFS_LIBRARY NAMES hfs)
if(HFS_LIBRARY_FOUND)
    target_compile_definitions(LaunchAPPL PRIVATE RETRO_HAVE_HFSUTILS)
    target_sources(LaunchAPPL PRIVATE MiniVMac.h MiniVMac.cc)
    target_link_libraries(LaunchAPPL PRIVATE ${HFS_LIBRARY})
    target_include_directories(LaunchAPPL PRIVATE ${HFS_INCLUDE_DIR})
else()
    message(STATUS "hfsutils not found. Building LaunchAPPL without MiniVMac support")
endif()

install(TARGETS LaunchAPPL)
