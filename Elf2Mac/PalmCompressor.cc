// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: © Retro68 contributors

#include "PalmCompressor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <utility>

#include <BinaryIO.h>

enum Op {
    // Run of uncompressed literals given in the next N bytes
    Literal     = 0x80,
    // Run of zeros
    ZeroRun     = 0x40,
    // Run of a single value given in the next byte
    ValueRun    = 0x20,
    // Run of 0xff
    FFRun       = 0x10,
    /* 3 and 4 are compression for Mac OS jump table entries, but Palm OS
        never uses those, so are useless and thus omitted */
    // Run of 8 byte pattern with value given in the next 3 bytes
    Pat0000FXXX = 2,
    // Run of 8 byte pattern with value given in the next 2 bytes
    Pat0000FFXX = 1,
    // Termination marker
    End         = 0,
};

static void EmitLiteral(std::ostringstream &out, const char *data, size_t len)
{
    while (len != 0)
    {
        uint8_t runSize = std::min<size_t>(Literal, len);
        out.put(Literal | (runSize - 1));
        out.write(data, runSize);
        data += runSize;
        len -= runSize;
    }
}

static void EmitPattern(std::ostringstream &out, const char *data)
{
    uint8_t op;
    size_t len;
    if (data[1] == '\xff')
    {
        op = Pat0000FFXX;
        len = 2;
    }
    else
    {
        op = Pat0000FXXX;
        len = 3;
    }
    data += (4 - len);
    out.put(op);
    out.write(data, len);
}

static size_t EmitRun(std::ostringstream &out, char c, size_t len)
{
    uint8_t op;
    if (c == '\0')
        op = ZeroRun;
    else if (c == '\xff' && len <= FFRun)
        op = FFRun;
    else
        op = ValueRun;

    while (len > 1)
    {
        uint8_t runSize = std::min<size_t>(op, len);
        out.put(char(op | (runSize - 1)));
        if (op == ValueRun)
            out.put(c);
        len -= runSize;
    }

    return len;
}

static std::pair<char, size_t> RunLen(const char *start, const char *end)
{
    std::pair<char, size_t> run;
    run.first = *start++;
    run.second = 1;
    while (start != end && *start++ == run.first)
        ++run.second;
    return run;
}

static bool ShouldEmitPattern(char c, size_t runLen, const char *start, const char *end)
{
    size_t len = end - start;
    if (c != '\0' || runLen != 4 || len < 4 || start[0] != '\xff')
        return false;

    // 00 00 00 00 FF 00 00 00 ZRun+FRun+ZRun < Pat (3 < 4)
    if (start[1] == '\0' && start[2] == '\0' && start[3] == '\0')
        return false;

    // 00 00 00 00 FF FF FF FF ZRun+FRun < Pat (2 < 3)
    if (start[1] == '\xff' && start[2] == '\xff' && start[3] == '\xff')
        return false;

    // 00 00 00 00 FF AA AA AA AA BB ZRun+FRun+CRun+Lit < Pat+Lit (6 < 7)
    if (len > 4 && start[1] == start[2] && start[2] == start[3] && start[3] == start[4])
        return false;

    // Other sequences should all be equivalent or worse than pattern?
    return true;
}

static void CompressRange(std::ostringstream &out, const char *base, const char *in, const char *end, uint32_t belowA5)
{
    const char* literal = in;
    size_t literalLen = 0;

    longword(out, (in - base) - belowA5);

    while (in != end)
    {
        auto [c, len] = RunLen(in, end);
        in += len;
        if (len > 1)
        {
            EmitLiteral(out, literal, literalLen);

            size_t trailingLiteralLen;
            if (ShouldEmitPattern(c, len, in, end))
            {
                EmitPattern(out, in);
                in += 4;
                trailingLiteralLen = 0;
            }
            else
                trailingLiteralLen = EmitRun(out, c, len);

            literal = in - trailingLiteralLen;
            literalLen = trailingLiteralLen;
        }
        else
            ++literalLen;
    }

    EmitLiteral(out, literal, literalLen);

    out.put(End);
}

using FatPtr = std::pair<const char *, size_t>;
static std::array<FatPtr, 2> FindLongestZeroRuns(const char *start, const char *end)
{
    // Input needs to be sorted by length
    std::array<FatPtr, 2> best { FatPtr { end, 0 }, FatPtr { end, 0 } };
    FatPtr next { nullptr, 0 };

    auto update = [&]() {
        if (next.second > best[0].second)
        {
            best[1] = best[0];
            best[0] = next;
        }
        else if (next.second > best[1].second)
            best[1] = next;
    };

    while (start != end)
    {
        if (*start == '\0')
        {
            if (next.first == nullptr)
                next.first = start;
            ++next.second;
        }
        else
        {
            update();
            next.first = nullptr;
            next.second = 0;
        }

        ++start;
    }

    update();

    // Output needs to be sorted by pointer position
    if (best[0].first > best[1].first)
        std::swap(best[0], best[1]);

    return best;
}

std::string CompressPalmData(std::string &&input)
{
    std::ostringstream out;

    auto base = input.data();
    auto in = base;
    auto belowA5 = input.size();
    auto end = in + belowA5;

    while (in != end && *in == '\0')
        ++in;

    while (in != end && end[-1] == '\0')
        --end;

    // For whatever reason this format requires exactly two skips no matter
    // what
    auto [skip1, skip2] = FindLongestZeroRuns(in, end);

    // Decompression starts from offset 4. This field should be populated later
    // with the offset of the code 1 relocation table (though it is not clear
    // that anything actually uses this value).
    longword(out, 0);

    CompressRange(out, base, in, skip1.first, belowA5);
    CompressRange(out, base, skip1.first + skip1.second, skip2.first, belowA5);
    CompressRange(out, base, skip2.first + skip2.second, end, belowA5);

    return out.str();
}
