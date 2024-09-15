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

#include <cstdint>
#include <string>
#include <vector>

enum class RelocBase
{
    // A relocation to the current section.
    code = 0,
    // A relocation to the data section.
    data,
    // A relocation to the bss section.
    bss,
    // A relocation to the jump table.
    jumptable,
    // A relocation to the code 1 section.
    code1
};

using Relocations = std::unordered_map<RelocBase, std::vector<Elf32_Addr>>;

std::string SerializeRelocs(const Relocations &relocs);
#ifdef PALMOS
std::string SerializeRelocsPalm(const Relocations &relocs, bool codeSegment);
#endif

#endif // RELOC_H
