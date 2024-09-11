find_package(Boost COMPONENTS program_options QUIET)

if(NOT TARGET Boost::program_options)
    message(STATUS "Boost::program_options not found. Building LaunchAPPL without client")
    return()
endif()

add_executable(LaunchAPPL)
target_sources(LaunchAPPL PRIVATE
    LaunchAPPL.cc
    MakeExecutable.cc

    LaunchMethod.h LaunchMethod.cc
    Launcher.h Launcher.cc

    Utilities.h Utilities.cc

    Executor.h Executor.cc
    MiniVMac.h MiniVMac.cc
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
    Boost::program_options
    hfsutils::hfs)

install(TARGETS LaunchAPPL)
