#   Copyright 2017 Wolfgang Thaller.
#
#   This file is part of Retro68.
#
#   Retro68 is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   Retro68 is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with Retro68.  If not, see <http://www.gnu.org/licenses/>.

include(GNUInstallDirs)

add_executable(Elf2Mac
    Elf2Mac.cc
    Object.cc
    Object.h
    Reloc.cc
    Reloc.h
    Section.h
    SegmentMap.cc
    SegmentMap.h
)

if(RETRO_PALMOS)
    target_sources(Elf2Mac PRIVATE
        PalmCompressor.h
        PalmCompressor.cc)
endif()

target_link_libraries(Elf2Mac PRIVATE ResourceFiles ELF)
target_include_directories(Elf2Mac PRIVATE ${CMAKE_INSTALL_PREFIX}/include)

install(TARGETS Elf2Mac)
install(CODE "
    set(RETRO_SUPPORTED_ABIS \"${RETRO_SUPPORTED_ABIS}\")
    set(install_bindir ${CMAKE_INSTALL_BINDIR})")
install(CODE [[
    foreach(retro_abi IN LISTS RETRO_SUPPORTED_ABIS)
        set(exe_path "${CMAKE_INSTALL_PREFIX}/${install_bindir}")
        set(sysroot_exe_path "${CMAKE_INSTALL_PREFIX}/${retro_abi}/${install_bindir}")
        set(e2m_path "${exe_path}/$<TARGET_FILE_NAME:Elf2Mac>")
        set(ld_path "${exe_path}/${retro_abi}-ld")
        set(sysroot_ld_path "${sysroot_exe_path}/ld")
        if (EXISTS "${ld_path}" AND NOT IS_SYMLINK "${ld_path}")
            message(STATUS "Linking Elf2Mac: ${ld_path}")
            file(RELATIVE_PATH e2m_rel "${exe_path}" "${e2m_path}")
            file(RENAME "${ld_path}" "${ld_path}.real")
            file(CREATE_LINK "${e2m_rel}" "${ld_path}" SYMBOLIC)
            endif()
        if (EXISTS "${sysroot_ld_path}" AND NOT IS_SYMLINK "${sysroot_ld_path}")
            message(STATUS "Linking Elf2Mac: ${sysroot_ld_path}")
            file(RELATIVE_PATH e2m_rel "${sysroot_exe_path}" "${e2m_path}")
            file(RENAME "${sysroot_ld_path}" "${sysroot_ld_path}.real")
            file(CREATE_LINK "${e2m_rel}" "${sysroot_ld_path}" SYMBOLIC)
        endif()
    endforeach()
]])
