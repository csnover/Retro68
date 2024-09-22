/*
     Copyright 2017 Wolfgang Thaller.

     This file is part of Retro68.

     Retro68 is free software: you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation, either version 3 of the License, or
     (at your option) any later version.

     Retro68 is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with Retro68.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef RELOC_H
#define RELOC_H

#include <libelf.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum RelocBase
{
    // The first RelocBase type.
    RelocBaseFirst = 0,

    // A relocation to the current section.
    RelocCode = 0,

    // A relocation to the data section.
    RelocData,

    // A relocation to the bss section.
    RelocBss,

    // A relocation to the code 1 section from another section.
    RelocCode1,

    // Number of RelocBase types.
    RelocBaseCount
};

using Relocations = std::array<std::vector<Elf32_Addr>, RelocBaseCount>;

std::string SerializeRelocs(const Relocations &relocs);
#ifdef PALMOS
std::string SerializeRelocsPalm(const Relocations &relocs, bool codeSection, Elf32_Addr dataBelowA5, Elf32_Addr bssBelowA5);
#endif

#endif // RELOC_H
