#    Copyright 2015 Wolfgang Thaller.
#
#    This file is part of Retro68.
#
#    Retro68 is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    Retro68 is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    Under Section 7 of GPL version 3, you are granted additional
#    permissions described in the GCC Runtime Library Exception, version
#    3.1, as published by the Free Software Foundation.
#
#    You should have received a copy of the GNU General Public License and
#    a copy of the GCC Runtime Library Exception along with this program;
#    see the files COPYING and COPYING.RUNTIME respectively.  If not, see
#    <http://www.gnu.org/licenses/>.

include(GNUInstallDirs)

add_library(retrocrt)
target_sources(retrocrt PRIVATE
    malloc.c
    syscalls.c
    consolehooks.c
)
target_compile_options(retrocrt PRIVATE -ffunction-sections)
target_link_libraries(retrocrt PRIVATE libinterface)
install(TARGETS retrocrt)

if(RETRO_68K OR RETRO_PALMOS)
    include(GNUInstallDirs)

    target_sources(retrocrt PRIVATE
        crtstuff.c
        Retro68Runtime.h
    )

    target_compile_options(retrocrt PRIVATE -mpcrel)

    install(FILES Retro68Runtime.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    if(RETRO_PALMOS)
        target_sources(retrocrt PRIVATE
            palmstart.c
        )
    else()
        enable_language(ASM)
        target_sources(retrocrt PRIVATE
            LoadSeg.s
            MultiSegApp.c
            PoorMansDebugging.h
            qdglobals.c
            relocate.c
            Retro68.r
            Retro68APPL.r
            start.c
        )
        install(FILES Retro68.r Retro68APPL.r
            DESTINATION "${CMAKE_INSTALL_DATADIR}/Retro68/RIncludes")
    endif()
else()
    target_sources(retrocrt PRIVATE
        ppcstart.c
        ppcfpsave.s
    )

    if(RETRO_PPC)
        target_sources(retrocrt PRIVATE
            qdglobals.c
            RetroPPCAPPL.r)
        install(FILES RetroPPCAPPL.r DESTINATION "${CMAKE_INSTALL_DATADIR}/Retro68/RIncludes")
    else()
        # different library name for Carbon
        # (Carbon shares the powerpc-apple-macos/ directory with Classic PPC)
        set_target_properties(retrocrt PROPERTIES OUTPUT_NAME retrocrt-carbon)

        target_sources(retrocrt PRIVATE RetroCarbonAPPL.r)
        install(FILES RetroCarbonAPPL.r DESTINATION "${CMAKE_INSTALL_DATADIR}/Retro68/RIncludes")
    endif()
endif()
