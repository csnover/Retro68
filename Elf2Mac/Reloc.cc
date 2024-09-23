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
static inline void EmitPalmReloc(std::ostream &out, uint32_t &lastAddr, uint32_t relocAddr)
{
    int32_t delta = relocAddr - lastAddr;
    assert((delta & 1) == 0);
    delta /= 2;

    // The top two bits are control bits, and the top third bit is a sign bit
    if (delta >= (INT8_MIN >> 2) && delta <= (INT8_MAX >> 2))
        byte(out, 0x80 | (delta & (UINT8_MAX >> 2)));
    else if (delta >= (INT16_MIN >> 2) || delta <= (INT16_MAX >> 2))
        word(out, 0x4000 | (delta & (UINT16_MAX >> 2)));
    else
    {
        assert((relocAddr & 1) == 0);
        assert(relocAddr < (UINT32_MAX >> 3));
        longword(out, (relocAddr / 2) & (UINT32_MAX >> 2));
    }

    lastAddr = relocAddr;
}

static void EmitPalmDataRelocs(std::ostream &out, const Relocations &relocs, Elf32_Addr dataBelowA5, Elf32_Addr bssBelowA5)
{
    longword(out, relocs[RelocData].size() + relocs[RelocBss].size());

    // In case the relative order of .data and .bss changes later, try to avoid
    // having to remember every single place that may have an ordered dependency
    // by just being proactive and always emitting them in the appropriate
    // order.
    RelocBase first = RelocBss, second = RelocData;
    if (bssBelowA5 < dataBelowA5)
    {
        std::swap(first, second);
        std::swap(bssBelowA5, dataBelowA5);
    }

    // Palm OS relocations on the data section are done relative to %a5, not to
    // the start of the data, so the offsets have to be adjusted accordingly
    // by dataBelowA5 and bssBelowA5. Code sections are relocated from the
    // start of their data so do not need this.

    uint32_t offset = 0;
    for (auto reloc : relocs[first])
        EmitPalmReloc(out, offset, reloc - bssBelowA5);

    for (auto reloc : relocs[second])
        EmitPalmReloc(out, offset, reloc - dataBelowA5);
}

static void EmitPalmCodeRelocs(std::ostream &out, const Relocations &relocs, RelocBase which)
{
    uint32_t offset = 0;
    longword(out, relocs[which].size());
    for (auto reloc : relocs[which])
        EmitPalmReloc(out, offset, reloc);
}

std::pair<std::string, size_t> SerializeRelocsPalm(const Relocations &relocs, bool codeSection, Elf32_Addr dataBelowA5, Elf32_Addr bssBelowA5)
{
    std::ostringstream out;

    EmitPalmDataRelocs(out, relocs, dataBelowA5, bssBelowA5);

    auto dataRelocsSize = out.tellp();

    EmitPalmCodeRelocs(out, relocs, RelocCode1);

    if (codeSection)
        EmitPalmCodeRelocs(out, relocs, RelocCode);
    else
        assert(relocs[RelocCode].empty());

    return { out.str(), dataRelocsSize };
}
#endif
