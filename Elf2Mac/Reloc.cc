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

#include "Reloc.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>

#include "BinaryIO.h"

// TODO: Change relocation format in libretro to be more efficient and avoid
// this extra work
struct RelocIterator
{
    using T = std::optional<std::pair<RelocBase, Elf32_Addr>>;

    inline RelocIterator(const Relocations &relocs)
    {
        int relocBase = RelocBaseFirst;
        for (const auto &group : relocs)
        {
            if (!group.empty())
            {
                state[groupCount] = group.data();
                base[groupCount] = RelocBase(relocBase);
                end[groupCount++] = group.data() + group.size();
            }
            ++relocBase;
        }
    }

    inline T next()
    {
        size_t nextIndex = groupCount;
        Elf32_Addr nextValue = UINT32_MAX;
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            auto candidate = *state[groupIndex];
            if (candidate < nextValue)
            {
                nextIndex = groupIndex;
                nextValue = candidate;
            }
        }

        if (nextIndex == groupCount)
            return {};

        if (++state[nextIndex] == end[nextIndex])
        {
            state[nextIndex] = state[--groupCount];
            base[nextIndex] = base[groupCount];
            end[nextIndex] = end[groupCount];
        }

        return { { base[nextIndex], nextValue } };
    }

    size_t groupCount = 0;
    std::array<const Elf32_Addr *, RelocBaseCount> state;
    std::array<const Elf32_Addr *, RelocBaseCount> end;
    std::array<RelocBase, RelocBaseCount> base;
};

std::string SerializeRelocs(const Relocations &relocs)
{
    std::ostringstream out;

    RelocIterator it(relocs);

    Elf32_Addr offset = -1;
    for (RelocIterator::T r; (r = it.next()); )
    {
        Elf32_Addr delta = r->second - offset;
        offset = r->second;

        Elf32_Addr encoded = (delta << 2) | int(r->first);

        while (encoded >= 0x80)
        {
            byte(out, (encoded & 0x7F) | 0x80);
            encoded >>= 7;
        }

        byte(out, encoded);
    }

    byte(out, 0);

    // TODO: Remove PCREL code from libretro, which expects a second loop for
    // relative relocations, which should never happen now
    byte(out, 0);

    return out.str();
}

#ifdef PALMOS
static void EmitPalmReloc(std::ostream &out, uint32_t &offset, uint32_t relocAddr)
{
    uint32_t delta = relocAddr - offset;
    assert((delta % 2) == 0);
    delta /= 2;
    if (delta >= 0x4000)
        longword(out, delta);
    else if (delta >= 0x80)
        word(out, 0x4000 | delta);
    else
        byte(out, 0x80 | delta);
}

std::string SerializeRelocsPalm(const Relocations &relocs, bool codeSection, Elf32_Addr dataAddr, Elf32_Addr bssAddr)
{
    std::ostringstream out;

    uint32_t offset = 0;

    // Palm OS relocations on the data section are relative to %a5, not to
    // the start of the data, so the offsets have to be adjusted accordingly
    longword(out, relocs[RelocData].size() + relocs[RelocBss].size());
    for (auto reloc : relocs[RelocData])
        EmitPalmReloc(out, offset, reloc + dataAddr);
    assert(relocs[RelocBss].empty() || relocs[RelocBss].front() > offset);
    for (auto reloc : relocs[RelocBss])
        EmitPalmReloc(out, offset, reloc + bssAddr);

    offset = 0;
    longword(out, relocs[RelocCode1].size());
    for (auto reloc : relocs[RelocCode1])
        EmitPalmReloc(out, offset, reloc);

    if (codeSection)
    {
        offset = 0;
        longword(out, relocs[RelocCode].size());
        for (auto reloc : relocs[RelocCode])
            EmitPalmReloc(out, offset, reloc);
    }

    return out.str();
}
#endif
