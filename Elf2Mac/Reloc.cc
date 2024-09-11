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

#include <cassert>
#include <sstream>

#include "BinaryIO.h"

std::string SerializeRelocs(const std::vector<RuntimeReloc> &relocs)
{
    std::ostringstream out;

    for(int relative = 0; relative <= 1; relative++)
    {
        uint32_t offset = -1;

        for(const auto &r : relocs)
        {
            if(r.relative == (bool)relative)
            {
                uint32_t delta = r.offset - offset;
                offset = r.offset;

                uint32_t base = (uint32_t) r.base;

                uint32_t encoded = (delta << 2) | base;

                while(encoded >= 128)
                {
                    byte(out, (encoded & 0x7F) | 0x80);
                    encoded >>= 7;
                }
                byte(out, encoded);
            }
        }

        byte(out, 0);
    }

    return out.str();
}

#ifdef PALMOS
static void EmitPalmReloc(std::ostream &out, uint32_t &count, uint32_t &offset, uint32_t relocAddr)
{
    ++count;
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

std::string SerializeRelocsPalm(const std::vector<RuntimeReloc> &relocs, bool codeSegment)
{
    std::ostringstream data, code1, codeN;
    uint32_t dataCount = 0, dataOffset = 0;
    uint32_t code1Count = 0, code1Offset = 0;
    uint32_t codeNCount = 0, codeNOffset = 0;

    for (const auto &r : relocs)
    {
        // TODO: Not sure what to do with these; they would apply only to
        // code segments, but code1 is normally relocated by the OS and there
        // is no pcrel there
        assert(!r.relative);

        switch (r.base)
        {
            case RelocBase::data:
            case RelocBase::bss:
            case RelocBase::jumptable:
                EmitPalmReloc(data, dataCount, dataOffset, r.offset);
                break;
            case RelocBase::code:
                if (codeSegment)
                {
                    EmitPalmReloc(codeN, codeNCount, codeNOffset, r.offset);
                    break;
                }
                // fall through
            case RelocBase::code1:
                EmitPalmReloc(code1, code1Count, code1Offset, r.offset);
                break;
        }
    }

    std::ostringstream out;
    longword(out, dataCount);
    out << data.str();
    if (codeSegment || code1Count != 0)
    {
        longword(out, code1Count);
        out << code1.str();
    }
    if (codeSegment)
    {
        longword(out, codeNCount);
        out << codeN.str();
    }
    return out.str();
}
#endif
