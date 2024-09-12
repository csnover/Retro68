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
    code = 0,
    data,
    bss,
    jumptable,
    code1
};

struct RuntimeReloc
{
    RuntimeReloc(RelocBase base_, uint32_t offset_, bool relative_ = false)
        : base(base_)
        , offset(offset_)
        , relative(relative_) {}

    RelocBase base;
    uint32_t offset;
    bool relative;
};

std::string SerializeRelocs(const std::vector<RuntimeReloc> &relocs);
#ifdef PALMOS
std::string SerializeRelocsPalm(const std::vector<RuntimeReloc> &relocs, bool codeSegment);
#endif

#endif // RELOC_H
