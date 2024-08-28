project(POSER VERSION 3.6.0)

include(GNUInstallDirs)

option(PALM_PROFILE "include profiling of Palm applications")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_compile_options(
    -fexceptions
    -Wall
    -Wno-multichar
    -Wno-unknown-pragmas
    -Wno-conversion
)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(JPEG REQUIRED)
find_package(FLTK REQUIRED)
find_package(Perl REQUIRED)
find_package(X11 REQUIRED)

function(poser_compiler_checks)
    include(CheckIncludeFile)
    foreach(header IN ITEMS endian.h machine/endian.h sys/isa_defs.h fcntl.h limits.h strings.h sys/time.h unistd.h)
        string(TOUPPER "have_${header}" var_name)
        string(REGEX REPLACE "[^A-Z0-9]" "_" var_name "${var_name}")
        check_include_file(${header} ${var_name})
        add_compile_definitions($<$<BOOL:${${var_name}}>:${var_name}>)
    endforeach()

    include(CheckStructHasMember)
    foreach(member IN ITEMS "sockaddr;sa_len;sys/socket.h" "sockaddr_in;sin_len;netinet/in.h")
        list(GET member 0 s)
        list(GET member 1 p)
        list(GET member 2 h)
        string(TOUPPER "have_${p}" var_name)
        check_struct_has_member("struct ${s}" ${p} ${h} ${var_name} LANGUAGE C)
        add_compile_definitions($<$<BOOL:${${var_name}}>:${var_name}>)
    endforeach()

    include(CheckTypeSize)
    set(old_extras "${CMAKE_EXTRA_INCLUDE_FILES}")
    set(CMAKE_EXTRA_INCLUDE_FILES sys/socket.h)
    check_type_size(socklen_t TYPE_SOCKLEN_T)
    set(CMAKE_EXTRA_INCLUDE_FILES "${old_extras}")
    unset(old_extras)
    add_compile_definitions($<$<BOOL:${HAVE_TYPE_SOCKLEN_T}>:HAVE_TYPE_SOCKLEN_T>)
    if(NOT HAVE_TYPE_SOCKLEN_T)
        message(STATUS "socklen_t not found")
    endif()

    include(CheckSymbolExists)
    foreach(member IN ITEMS "stdlib.h;mkstemp" "string.h;strdup")
        list(GET member 0 h)
        list(GET member 1 func)
        string(TOUPPER "have_${func}" var_name)
        check_symbol_exists(${func} "${h}" ${var_name})
        add_compile_definitions($<$<BOOL:${${var_name}}>:${var_name}>)
    endforeach()
endfunction()
poser_compiler_checks()

add_executable(pose
    Scripting/Python/GDBRemote.py

    SrcUnix/EmApplicationFltk.cpp
    SrcUnix/EmApplicationFltk.h
    SrcUnix/EmCommonUnix.h
    SrcUnix/EmDirRefUnix.cpp
    SrcUnix/EmDirRefUnix.h
    SrcUnix/EmDlgFltk.cpp
    SrcUnix/EmDlgFltk.h
    SrcUnix/EmDocumentUnix.cpp
    SrcUnix/EmDocumentUnix.h
    SrcUnix/EmFileRefUnix.cpp
    SrcUnix/EmFileRefUnix.h
    SrcUnix/EmMenusFltk.cpp
    SrcUnix/EmMenusFltk.h
    SrcUnix/EmPixMapUnix.cpp
    SrcUnix/EmPixMapUnix.h
    SrcUnix/EmTransportSerialUnix.cpp
    SrcUnix/EmTransportSerialUnix.h
    SrcUnix/EmTransportUSBUnix.cpp
    SrcUnix/EmTransportUSBUnix.h
    SrcUnix/EmWindowFltk.cpp
    SrcUnix/EmWindowFltk.h
    SrcUnix/Platform_Unix.cpp
    SrcUnix/poser.xpm

    SrcShared/Strings.r.h
    SrcShared/Strings.txt
    Tools/Strings2Resource.pl
    ${CMAKE_CURRENT_BINARY_DIR}/generated/ResStrings.cpp

    SrcUnix/EmDlgFltkFactory.fl
    ${CMAKE_CURRENT_BINARY_DIR}/generated/EmDlgFltkFactory.h
    ${CMAKE_CURRENT_BINARY_DIR}/generated/EmDlgFltkFactory.cpp

    SrcShared/ATraps.cpp
    SrcShared/ATraps.h
    SrcShared/Byteswapping.cpp
    SrcShared/Byteswapping.h
    SrcShared/CGremlins.cpp
    SrcShared/CGremlins.h
    SrcShared/CGremlinsStubs.cpp
    SrcShared/CGremlinsStubs.h
    SrcShared/ChunkFile.cpp
    SrcShared/ChunkFile.h
    SrcShared/DebugMgr.cpp
    SrcShared/DebugMgr.h
    SrcShared/EcmIf.h
    SrcShared/EcmObject.h
    SrcShared/EmAction.cpp
    SrcShared/EmAction.h
    SrcShared/EmApplication.cpp
    SrcShared/EmApplication.h
    SrcShared/EmCommands.h
    SrcShared/EmCommon.cpp
    SrcShared/EmCommon.h
    SrcShared/EmDevice.cpp
    SrcShared/EmDevice.h
    SrcShared/EmDirRef.cpp
    SrcShared/EmDirRef.h
    SrcShared/EmDlg.cpp
    SrcShared/EmDlg.h
    SrcShared/EmDocument.cpp
    SrcShared/EmDocument.h
    SrcShared/EmErrCodes.h
    SrcShared/EmEventOutput.cpp
    SrcShared/EmEventOutput.h
    SrcShared/EmEventPlayback.cpp
    SrcShared/EmEventPlayback.h
    SrcShared/EmException.cpp
    SrcShared/EmException.h
    SrcShared/EmExgMgr.cpp
    SrcShared/EmExgMgr.h
    SrcShared/EmFileImport.cpp
    SrcShared/EmFileImport.h
    SrcShared/EmFileRef.cpp
    SrcShared/EmFileRef.h
    SrcShared/EmJPEG.cpp
    SrcShared/EmJPEG.h
    SrcShared/EmLowMem.cpp
    SrcShared/EmLowMem.h
    SrcShared/EmMapFile.cpp
    SrcShared/EmMapFile.h
    SrcShared/EmMenus.cpp
    SrcShared/EmMenus.h
    SrcShared/EmMinimize.cpp
    SrcShared/EmMinimize.h
    SrcShared/EmPalmFunction.cpp
    SrcShared/EmPalmFunction.h
    SrcShared/EmPalmHeap.cpp
    SrcShared/EmPalmHeap.h
    SrcShared/EmPalmOS.cpp
    SrcShared/EmPalmOS.h
    SrcShared/EmPalmStructs.cpp
    SrcShared/EmPalmStructs.h
    SrcShared/EmPalmStructs.i
    SrcShared/EmPixMap.cpp
    SrcShared/EmPixMap.h
    SrcShared/EmPoint.cpp
    SrcShared/EmPoint.h
    SrcShared/EmQuantizer.cpp
    SrcShared/EmQuantizer.h
    SrcShared/EmRect.cpp
    SrcShared/EmRect.h
    SrcShared/EmRefCounted.cpp
    SrcShared/EmRefCounted.h
    SrcShared/EmRegion.cpp
    SrcShared/EmRegion.h
    SrcShared/EmROMReader.cpp
    SrcShared/EmROMReader.h
    SrcShared/EmROMTransfer.cpp
    SrcShared/EmROMTransfer.h
    SrcShared/EmRPC.cpp
    SrcShared/EmRPC.h
    SrcShared/EmScreen.cpp
    SrcShared/EmScreen.h
    SrcShared/EmSession.cpp
    SrcShared/EmSession.h
    SrcShared/EmStream.cpp
    SrcShared/EmStream.h
    SrcShared/EmStreamFile.cpp
    SrcShared/EmStreamFile.h
    SrcShared/EmStructs.h
    SrcShared/EmSubroutine.cpp
    SrcShared/EmSubroutine.h
    SrcShared/EmThreadSafeQueue.cpp
    SrcShared/EmThreadSafeQueue.h
    SrcShared/EmTransport.cpp
    SrcShared/EmTransport.h
    SrcShared/EmTransportSerial.cpp
    SrcShared/EmTransportSerial.h
    SrcShared/EmTransportSocket.cpp
    SrcShared/EmTransportSocket.h
    SrcShared/EmTransportUSB.cpp
    SrcShared/EmTransportUSB.h
    SrcShared/EmTypes.h
    SrcShared/EmWindow.cpp
    SrcShared/EmWindow.h
    SrcShared/ErrorHandling.cpp
    SrcShared/ErrorHandling.h
    SrcShared/GDBRemote.cpp
    SrcShared/GDBRemote.h
    SrcShared/Hordes.cpp
    SrcShared/Hordes.h
    SrcShared/HostControl.cpp
    SrcShared/HostControl.h
    SrcShared/HostControlPrv.h
    SrcShared/LoadApplication.cpp
    SrcShared/LoadApplication.h
    SrcShared/Logging.cpp
    SrcShared/Logging.h
    SrcShared/Marshal.cpp
    SrcShared/Marshal.h
    SrcShared/MetaMemory.cpp
    SrcShared/MetaMemory.h
    SrcShared/Miscellaneous.cpp
    SrcShared/Miscellaneous.h
    SrcShared/Palm.h
    SrcShared/PalmOptErrorCheckLevel.h
    SrcShared/PalmPack.h
    SrcShared/PalmPackPop.h
    SrcShared/Platform.h
    SrcShared/Platform_NetLib.h
    SrcShared/Platform_NetLib_Sck.cpp
    SrcShared/PreferenceMgr.cpp
    SrcShared/PreferenceMgr.h
    SrcShared/Profiling.cpp
    SrcShared/Profiling.h
    SrcShared/ROMStubs.cpp
    SrcShared/ROMStubs.h
    SrcShared/SLP.cpp
    SrcShared/SLP.h
    SrcShared/SessionFile.cpp
    SrcShared/SessionFile.h
    SrcShared/Skins.cpp
    SrcShared/Skins.h
    SrcShared/SocketMessaging.cpp
    SrcShared/SocketMessaging.h
    SrcShared/Startup.cpp
    SrcShared/Startup.h
    SrcShared/StringConversions.cpp
    SrcShared/StringConversions.h
    SrcShared/StringData.cpp
    SrcShared/StringData.h
    SrcShared/SystemPacket.cpp
    SrcShared/SystemPacket.h

    SrcShared/Hardware/EmBankDRAM.cpp
    SrcShared/Hardware/EmBankDRAM.h
    SrcShared/Hardware/EmBankDummy.cpp
    SrcShared/Hardware/EmBankDummy.h
    SrcShared/Hardware/EmBankMapped.cpp
    SrcShared/Hardware/EmBankMapped.h
    SrcShared/Hardware/EmBankROM.cpp
    SrcShared/Hardware/EmBankROM.h
    SrcShared/Hardware/EmBankRegs.cpp
    SrcShared/Hardware/EmBankRegs.h
    SrcShared/Hardware/EmBankSRAM.cpp
    SrcShared/Hardware/EmBankSRAM.h
    SrcShared/Hardware/EmCPU.cpp
    SrcShared/Hardware/EmCPU.h
    SrcShared/Hardware/EmCPU68K.cpp
    SrcShared/Hardware/EmCPU68K.h
    SrcShared/Hardware/EmCPUARM.cpp
    SrcShared/Hardware/EmCPUARM.h
    SrcShared/Hardware/EmHAL.cpp
    SrcShared/Hardware/EmHAL.h
    SrcShared/Hardware/EmMemory.cpp
    SrcShared/Hardware/EmMemory.h
    SrcShared/Hardware/EmRegs.cpp
    SrcShared/Hardware/EmRegs.h
    SrcShared/Hardware/EmRegs328.cpp
    SrcShared/Hardware/EmRegs328.h
    SrcShared/Hardware/EmRegs328PalmIII.h
    SrcShared/Hardware/EmRegs328PalmPilot.cpp
    SrcShared/Hardware/EmRegs328PalmPilot.h
    SrcShared/Hardware/EmRegs328PalmVII.h
    SrcShared/Hardware/EmRegs328Pilot.h
    SrcShared/Hardware/EmRegs328Prv.h
    SrcShared/Hardware/EmRegs328Symbol1700.cpp
    SrcShared/Hardware/EmRegs328Symbol1700.h
    SrcShared/Hardware/EmRegsASICSymbol1700.cpp
    SrcShared/Hardware/EmRegsASICSymbol1700.h
    SrcShared/Hardware/EmRegsEZ.cpp
    SrcShared/Hardware/EmRegsEZ.h
    SrcShared/Hardware/EmRegsEZPalmIIIc.cpp
    SrcShared/Hardware/EmRegsEZPalmIIIc.h
    SrcShared/Hardware/EmRegsEZPalmIIIe.h
    SrcShared/Hardware/EmRegsEZPalmIIIx.h
    SrcShared/Hardware/EmRegsEZPalmM100.cpp
    SrcShared/Hardware/EmRegsEZPalmM100.h
    SrcShared/Hardware/EmRegsEZPalmV.cpp
    SrcShared/Hardware/EmRegsEZPalmV.h
    SrcShared/Hardware/EmRegsEZPalmVIIx.cpp
    SrcShared/Hardware/EmRegsEZPalmVIIx.h
    SrcShared/Hardware/EmRegsEZPalmVII.cpp
    SrcShared/Hardware/EmRegsEZPalmVII.h
    SrcShared/Hardware/EmRegsEZPalmVx.h
    SrcShared/Hardware/EmRegsEZPrv.h
    SrcShared/Hardware/EmRegsEZTemp.cpp
    SrcShared/Hardware/EmRegsEZTemp.h
    SrcShared/Hardware/EmRegsEZTRGpro.cpp
    SrcShared/Hardware/EmRegsEZTRGpro.h
    SrcShared/Hardware/EmRegsEZVisor.cpp
    SrcShared/Hardware/EmRegsEZVisor.h
    SrcShared/Hardware/EmRegsFrameBuffer.cpp
    SrcShared/Hardware/EmRegsFrameBuffer.h
    SrcShared/Hardware/EmRegsMediaQ11xx.cpp
    SrcShared/Hardware/EmRegsMediaQ11xx.h
    SrcShared/Hardware/EmRegsPLDPalmVIIEZ.cpp
    SrcShared/Hardware/EmRegsPLDPalmVIIEZ.h
    SrcShared/Hardware/EmRegsPrv.h
    SrcShared/Hardware/EmRegsSED1375.cpp
    SrcShared/Hardware/EmRegsSED1375.h
    SrcShared/Hardware/EmRegsSED1376.cpp
    SrcShared/Hardware/EmRegsSED1376.h
    SrcShared/Hardware/EmRegsSZ.cpp
    SrcShared/Hardware/EmRegsSZ.h
    SrcShared/Hardware/EmRegsSZPrv.h
    SrcShared/Hardware/EmRegsSZTemp.cpp
    SrcShared/Hardware/EmRegsSZTemp.h
    SrcShared/Hardware/EmRegsUSBISP_1161.cpp
    SrcShared/Hardware/EmRegsUSBISP_1161.h
    SrcShared/Hardware/EmRegsUSBPhilipsPDIUSBD12.cpp
    SrcShared/Hardware/EmRegsUSBPhilipsPDIUSBD12.h
    SrcShared/Hardware/EmRegsUSBVisor.cpp
    SrcShared/Hardware/EmRegsUSBVisor.h
    SrcShared/Hardware/EmRegsVZ.cpp
    SrcShared/Hardware/EmRegsVZ.h
    SrcShared/Hardware/EmRegsVZAlphaSmart.cpp
    SrcShared/Hardware/EmRegsVZAlphaSmart.h
    SrcShared/Hardware/EmRegsVZHandEra330.cpp
    SrcShared/Hardware/EmRegsVZHandEra330.h
    SrcShared/Hardware/EmRegsVZPalmM500.cpp
    SrcShared/Hardware/EmRegsVZPalmM500.h
    SrcShared/Hardware/EmRegsVZPalmM505.cpp
    SrcShared/Hardware/EmRegsVZPalmM505.h
    SrcShared/Hardware/EmRegsVZPrv.h
    SrcShared/Hardware/EmRegsVZTemp.cpp
    SrcShared/Hardware/EmRegsVZTemp.h
    SrcShared/Hardware/EmRegsVZVisorEdge.cpp
    SrcShared/Hardware/EmRegsVZVisorEdge.h
    SrcShared/Hardware/EmRegsVZVisorPlatinum.cpp
    SrcShared/Hardware/EmRegsVZVisorPlatinum.h
    SrcShared/Hardware/EmRegsVZVisorPrism.cpp
    SrcShared/Hardware/EmRegsVZVisorPrism.h
    SrcShared/Hardware/EmSPISlave.cpp
    SrcShared/Hardware/EmSPISlave.h
    SrcShared/Hardware/EmSPISlaveADS784x.cpp
    SrcShared/Hardware/EmSPISlaveADS784x.h
    SrcShared/Hardware/EmSPISlaveSTMicro.cpp
    SrcShared/Hardware/EmSPISlaveSTMicro.h
    SrcShared/Hardware/EmUAEGlue.cpp
    SrcShared/Hardware/EmUAEGlue.h
    SrcShared/Hardware/EmUARTDragonball.cpp
    SrcShared/Hardware/EmUARTDragonball.h

    SrcShared/Patches/EmPatchIf.h
    SrcShared/Patches/EmPatchLoader.cpp
    SrcShared/Patches/EmPatchLoader.h
    SrcShared/Patches/EmPatchMgr.cpp
    SrcShared/Patches/EmPatchMgr.h
    SrcShared/Patches/EmPatchModule.cpp
    SrcShared/Patches/EmPatchModule.h
    SrcShared/Patches/EmPatchModuleHtal.cpp
    SrcShared/Patches/EmPatchModuleHtal.h
    SrcShared/Patches/EmPatchModuleMap.cpp
    SrcShared/Patches/EmPatchModuleMap.h
    SrcShared/Patches/EmPatchModuleMemMgr.cpp
    SrcShared/Patches/EmPatchModuleNetLib.cpp
    SrcShared/Patches/EmPatchModuleNetLib.h
    SrcShared/Patches/EmPatchModuleSys.cpp
    SrcShared/Patches/EmPatchModuleSys.h
    SrcShared/Patches/EmPatchModuleTypes.h
    SrcShared/Patches/EmPatchState.cpp
    SrcShared/Patches/EmPatchState.h

    SrcShared/Hardware/TRG/EmHandEra330Defs.h
    SrcShared/Hardware/TRG/EmHandEraCFBus.h
    SrcShared/Hardware/TRG/EmHandEraSDBus.h
    SrcShared/Hardware/TRG/EmRegs330CPLD.cpp
    SrcShared/Hardware/TRG/EmRegs330CPLD.h
    SrcShared/Hardware/TRG/EmSPISlave330Current.cpp
    SrcShared/Hardware/TRG/EmSPISlave330Current.h
    SrcShared/Hardware/TRG/EmTRG.cpp
    SrcShared/Hardware/TRG/EmTRG.h
    SrcShared/Hardware/TRG/EmTRGATA.cpp
    SrcShared/Hardware/TRG/EmTRGATA.h
    SrcShared/Hardware/TRG/EmTRGCF.cpp
    SrcShared/Hardware/TRG/EmTRGCF.h
    SrcShared/Hardware/TRG/EmTRGCFDefs.h
    SrcShared/Hardware/TRG/EmTRGCFIO.cpp
    SrcShared/Hardware/TRG/EmTRGCFIO.h
    SrcShared/Hardware/TRG/EmTRGCFMem.cpp
    SrcShared/Hardware/TRG/EmTRGCFMem.h
    SrcShared/Hardware/TRG/EmTRGDiskIO.cpp
    SrcShared/Hardware/TRG/EmTRGDiskIO.h
    SrcShared/Hardware/TRG/EmTRGDiskType.cpp
    SrcShared/Hardware/TRG/EmTRGDiskType.h
    SrcShared/Hardware/TRG/EmTRGSD.cpp
    SrcShared/Hardware/TRG/EmTRGSD.h

    SrcShared/UAE/UAE.h
    SrcShared/UAE/compiler.h
    SrcShared/UAE/config.h
    SrcShared/UAE/cpudefs.c
    SrcShared/UAE/cpuemu.c
    SrcShared/UAE/cpustbl.c
    SrcShared/UAE/cputbl.h
    SrcShared/UAE/custom.h
    SrcShared/UAE/machdep_m68k.h
    SrcShared/UAE/machdep_maccess.h
    SrcShared/UAE/memory_cpu.h
    SrcShared/UAE/newcpu.h
    SrcShared/UAE/options.h
    SrcShared/UAE/readcpu.cpp
    SrcShared/UAE/readcpu.h
    SrcShared/UAE/sysconfig.h
    SrcShared/UAE/sysdeps.h
    SrcShared/UAE/target.h

    SrcShared/Palm/Platform/Core/System/Src/Crc.c

    SrcShared/omnithread/posix.cpp
)

target_include_directories(pose PRIVATE
    SrcShared
    SrcShared/Hardware
    SrcShared/Hardware/TRG
    SrcShared/Palm/Device
    SrcShared/Palm/Platform
    SrcShared/Palm/Platform/Core/Hardware/IncsPrv
    SrcShared/Palm/Platform/Core/System/IncsPrv
    SrcShared/Palm/Platform/Incs
    SrcShared/Palm/Platform/Incs/Core
    SrcShared/Palm/Platform/Incs/Core/Hardware
    SrcShared/Palm/Platform/Incs/Core/System
    SrcShared/Palm/Platform/Incs/Core/UI
    SrcShared/Palm/Platform/Incs/Libraries
    SrcShared/omnithread
    SrcShared/Patches
    SrcShared/UAE
    SrcUnix
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(pose PRIVATE
    ELF
    ZLIB::ZLIB
    Threads::Threads
    JPEG::JPEG
    fltk
    fltk_images
    X11::X11
)

set(datadir "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
set(skinsdir "${datadir}/Skins")

target_compile_definitions(pose PRIVATE
    HAS_PROFILING=$<BOOL:${PALM_PROFILE}>
    PLATFORM_UNIX=1
    __PALMOS_TRAPS__=0
    EMULATION_LEVEL=EMULATION_UNIX
    PthreadDraftVersion=10
    PROJECT_NAME="${PROJECT_NAME}"
    PROJECT_VERSION="${PROJECT_VERSION}"
    SKINS_DIR="${skinsdir}"
)

# This file was generated by a script so contains unused variables
set_source_files_properties(SrcShared/UAE/cpuemu.c
    PROPERTIES COMPILE_OPTIONS -Wno-unused-variable
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT generated/ResStrings.cpp generated/Strings.cpp generated/Strings.rc
    COMMAND "${PERL_EXECUTABLE}" -x "${CMAKE_CURRENT_SOURCE_DIR}/SrcShared/Strings.txt"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated"
    DEPENDS SrcShared/Strings.txt Tools/Strings2Resource.pl
)

add_custom_command(
    OUTPUT generated/EmDlgFltkFactory.h generated/EmDlgFltkFactory.cpp
    COMMAND "${FLTK_FLUID_EXECUTABLE}" -c "${CMAKE_CURRENT_SOURCE_DIR}/SrcUnix/EmDlgFltkFactory.fl"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated"
    DEPENDS SrcUnix/EmDlgFltkFactory.fl
)

install(TARGETS pose)
install(DIRECTORY DESTINATION "${skinsdir}")
install(
    DIRECTORY ROMTransfer Scripting
    DESTINATION "${datadir}"
)
install(DIRECTORY Docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
