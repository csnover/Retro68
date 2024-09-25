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

/**
 * Parts of the generated script are copied from common parts of scripts at
 * <PREFIX>/<triple>/lib/ldscripts/m68kelf.*.
 */

#include "SegmentMap.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iosfwd>
#include <sstream>
#include <string>

#define NOP "0x4e71"

// SPDX-SnippetBegin
// SPDX-License-Identifier: CC-BY-SA-3.0
// https://stackoverflow.com/a/9600752/252087
class Block : public std::streambuf
{
public:
    Block(std::ostream &out)
        : m_rdbuf(out.rdbuf())
        , m_out(out)
    {
        m_out << "{\n";
        m_out.rdbuf(this);
    }

    virtual ~Block()
    {
        m_out.rdbuf(m_rdbuf);
        m_out << "}\n";
    }

protected:
    virtual int overflow(int ch) override
    {
        if (m_indent && ch != '\n')
            m_rdbuf->sputn("    ", 4);
        m_indent = (ch == '\n');
        return m_rdbuf->sputc(ch);
    }

private:
    std::streambuf *m_rdbuf;
    std::ostream &m_out;
    bool m_indent = true;
};
// SPDX-SnippetEnd

// TODO: Main code should go to code 1, not just Retro68’s stuff. From Inside
// Macintosh Mac OS Runtime Architectures: “Put your main event loop into the
// main segment (that is, the segment that contains the main entry point).”
static std::initializer_list<std::string> RUNTIME_OBJECTS = {
    "*/libretrocrt.a:start.c.obj",
    "*/libretrocrt.a:palmstart.c.obj",
    "*/libretrocrt.a:crtstuff.c.obj",
    "*/libretrocrt.a:relocate.c.obj",
    "*/libretrocrt.a:MultiSegApp.c.obj",
    "*/libretrocrt.a:LoadSeg.s.obj",
    "*/libretrocrt.a:*",
    "*/libInterface.a:*",
    "*/libgcc.a:*",
    "*/libc.a:*"
};

SegmentMap::SegmentMap()
{
    m_segments.emplace_back(1, "Runtime", RUNTIME_OBJECTS);
    m_segments.emplace_back(5, "libstdc++ locale",
                          "*/libstdc++.a:locale.o",
                          "*/libstdc++.a:locale_faces.o",
                          "*/libstdc++.a:locale_init.o");
    m_segments.emplace_back(7, "libstdc++ locale-inst",
                          "*/libstdc++.a:locale-inst.o");
    m_segments.emplace_back(8, "libstdc++ wlocale-inst",
                          "*/libstdc++.a:wlocale-inst.o");
    m_segments.emplace_back(6, "libstdc++ cp-demangle",
                          "*/libstdc++.a:cp-demangle.o");
    m_segments.emplace_back(3, "libstdc++",
                          "*/libstdc++.a:*");
    m_segments.emplace_back(4, "RetroConsole",
                          "*/libRetroConsole.a:*");

    m_segments.emplace_back(2, "Main",
                          "*");
}

SegmentMap::SegmentMap(const std::string &filename)
{
    m_segments.emplace_back(1, "Runtime", RUNTIME_OBJECTS);

    std::ifstream in(filename);
    int id = -1;
    int nextID = 3;
    while(in)
    {
        std::string s;
        in >> std::ws;
        std::getline(in, s);

        if(!in)
            break;

        if(s[0] == '#')
            continue;

        auto isSegment = s.size() >= 7 && std::equal(s.begin(), s.begin() + 7, "SEGMENT",
            [](char a, char b) {
                return std::toupper(a) == b;
            });
        if (isSegment && s.size() > 7)
            isSegment = std::isspace(s[7]);

        if(isSegment)
        {
            auto p = s.begin() + 7;
            while(p != s.end() && std::isspace(*p))
                ++p;

            std::string name { p, s.end() };
            id = nextID++;

            m_segments.emplace_back(id, std::move(name));
        }
        else
        {
            if(id < 0)
                throw std::runtime_error("missing SEGMENT directive.\n");

            m_segments.back().filters.push_back(s);
        }
    }

    m_segments.emplace_back(2, "Main", "*");
}

// TODO: This could probably use preinit_section for Mac, to match how it works
// for Palm OS
static inline void EnterDebugger(std::ostream &out)
{
#   define Debugger "0xa9ff"
    out << "SHORT(DEFINED(__break_on_entry) ? " Debugger " : " NOP ");\n";
#   undef Debugger
}

static inline void PushPCToStack(std::ostream &out)
{
    // pea (4,%pc)
    out << "LONG(0x487A0004);\n";
}

static inline void DisplaceStackPCTo(std::ostream &out, const char *entryPoint)
{
    // addi.l #entryPoint, (sp)
    out
        << "SHORT(0x0697);\n"
        << "LONG(" << entryPoint << " - _entry_trampoline - 8);\n";
}

static inline void JumpToEntrypoint(std::ostream &out)
{
    // rts
    out << "SHORT(0x4e75);\n";
}

static void WriteTrampoline(std::ostream &out, const char *entryPoint, bool isMultiseg)
{
    out << "_entry_trampoline = .;\n";

    EnterDebugger(out);
    PushPCToStack(out);
    DisplaceStackPCTo(out, entryPoint);

    // fallback entry point to a safe spot - needed for libretro bootstrap
    out << "PROVIDE(_start = .);\n";

    if (isMultiseg)
        // override this for the single-segment case
        out << "Retro68InitMultisegApp = .;\n";

    JumpToEntrypoint(out);
}

static void WriteInitFini(std::ostream &out)
{
    out <<
        ". = ALIGN(4);\n"
        "__preinit_section = .;\n"
        "KEEP(*(.preinit))\n"
        "__preinit_section_end = .;\n"
        "__init_section = .;\n"
        "KEEP(*(.init))\n"
        "__init_section_end = .;\n"
        "__fini_section = .;\n"
        "KEEP(*(.fini))\n"
        "__fini_section_end = .;\n";
}

static Block StartSections(std::ostream &out, const char *entryPoint, bool stripMacsbug, bool isMultiseg)
{
    out <<
        "/* ld script for Elf2Mac */\n"
        "_MULTISEG_APP = " << int(isMultiseg) << ";\n"
        "ENTRY(" << entryPoint << ")\n";

    auto indent = Block(out << "SECTIONS\n");

    if (stripMacsbug)
        out <<
            ".strippedmacsbugnames 0 (INFO) : { *(.text.*.macsbug) }\n"
            ". = 0;\n";

    return indent;
}

static void WriteDataSection(std::ostream &out)
{
    // Alignments within the data sections use `ALIGN(., n)` because these
    // sections have a negative base address starting from the end of the
    // data. `ALIGN` rounds up, so `ALIGN(n)` (i.e. `ALIGN(ABSOLUTE(.), n)`)
    // will not do the expected thing of extending the section size to an
    // alignment boundary.
    Block section(out << ".data : ALIGN(4) SUBALIGN(4) ");
    out <<
        "_sdata = .;\n"
        "*(.got.plt)\n"
        "*(.got)\n"

        // TODO: Why so much alignment? What is this?
        "FILL(0);\n"
        ". = ALIGN(., 0x20);\n"
        // TODO: What is this?
        "LONG(-1);\n"
        // TODO: Why so much alignment? What is this?
        ". = ALIGN(., 0x20);\n"

        // TODO: Read-only data should probably be kept in the code sections.
        // At least on Palm OS, there are allegedly OS-level resource size
        // limits that will prevent resources from being larger than 32K/64K
        // depending on the OS version.
        "*(.rodata)\n"
        "*(.rodata1)\n"
        "*(.rodata.*)\n"
        "*(.gnu.linkonce.r*)\n"
        "*(.data)\n"
        "*(.data1)\n"
        "*(.data.*)\n"
        "*(.gnu.linkonce.d*)\n"

        ". = ALIGN(., 4);\n"
        "__CTOR_LIST__ = .;\n"
        "KEEP (*(.ctors))\n"
        "KEEP (*(SORT(.ctors.*)))\n"
        "__CTOR_END__ = .;\n"
        "LONG(0);\n"

        ". = ALIGN(., 4);\n"
        "__DTOR_LIST__ = .;\n"
        "KEEP (*(.dtors))\n"
        "KEEP (*(SORT(.dtors.*)))\n"
        "__DTOR_END__ = .;\n"
        "LONG(0);\n"
        ". = ALIGN(., 4);\n"
        "_edata = .;\n";
}

static void WriteBssSection(std::ostream &out)
{
    // See WriteDataSection for more information about alignment.
    // The data section goes below A5, so set its base address here so that
    // Elf2Mac does not need to adjust data offsets itself later.
    Block section(out << ".bss -SIZEOF(.bss)-SIZEOF(.data) : ALIGN(4) SUBALIGN(4) ");
    out <<
        "_sbss = .;\n"
        "*(.dynsbss)\n"
        "*(.sbss)\n"
        "*(.sbss.*)\n"
        "*(.scommon)\n"
        "*(.dynbss)\n"
        "*(.bss)\n"
        "*(.bss.*)\n"
        "*(.bss*)\n"
        "*(.gnu.linkonce.b*)\n"
        "*(COMMON)\n"
        ". = ALIGN(., 4);\n"
        "_ebss = .;\n";
}

static void WriteDebugSections(std::ostream &out)
{
    // These will be discarded by Elf2Mac and will be kept in the intermediate
    // output for GDB/LLDB
    out <<
        "/DISCARD/ : { *(.note.GNU-stack) }\n"

        // Stabs debugging sections.
        ".stab 0 : { *(.stab) }\n"
        ".stabstr 0 : { *(.stabstr) }\n"
        ".stab.excl 0 : { *(.stab.excl) }\n"
        ".stab.exclstr 0 : { *(.stab.exclstr) }\n"
        ".stab.index 0 : { *(.stab.index) }\n"
        ".stab.indexstr 0 : { *(.stab.indexstr) }\n"
        ".comment 0 : { *(.comment) }\n"
        ".gnu.build.attributes : { *(.gnu.build.attributes .gnu.build.attributes.*) }"

        // DWARF debug sections.
        // Symbols in the DWARF debugging sections are relative to the beginning
        // of the section so we begin them at 0.

        // DWARF 1.
        ".debug 0 : { *(.debug) }\n"
        ".line 0 : { *(.line) }\n"

        // GNU DWARF 1 extensions.
        ".debug_srcinfo 0 : { *(.debug_srcinfo) }\n"
        ".debug_sfnames 0 : { *(.debug_sfnames) }\n"

        // DWARF 1.1 and DWARF 2.
        ".debug_aranges 0 : { *(.debug_aranges) }\n"
        ".debug_pubnames 0 : { *(.debug_pubnames) }\n"

        // DWARF 2.
        ".debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }\n"
        ".debug_abbrev 0 : { *(.debug_abbrev) }\n"
        ".debug_line 0 : { *(.debug_line) }\n"
        ".debug_frame 0 : { *(.debug_frame) }\n"
        ".debug_str 0 : { *(.debug_str) }\n"
        ".debug_loc 0 : { *(.debug_loc) }\n"
        ".debug_macinfo 0 : { *(.debug_macinfo) }\n"

        // SGI/MIPS DWARF 2 extensions.
        ".debug_weaknames 0 : { *(.debug_weaknames) }\n"
        ".debug_funcnames 0 : { *(.debug_funcnames) }\n"
        ".debug_typenames 0 : { *(.debug_typenames) }\n"
        ".debug_varnames 0 : { *(.debug_varnames) }\n"

        // DWARF 3.
        ".debug_pubtypes 0 : { *(.debug_pubtypes) }\n"
        ".debug_ranges   0 : { *(.debug_ranges) }\n"

        // DWARF 5.
        ".debug_addr     0 : { *(.debug_addr) }\n"
        ".debug_line_str 0 : { *(.debug_line_str) }\n"
        ".debug_loclists 0 : { *(.debug_loclists) }\n"
        ".debug_macro    0 : { *(.debug_macro) }\n"
        ".debug_names    0 : { *(.debug_names) }\n"
        ".debug_rnglists 0 : { *(.debug_rnglists) }\n"
        ".debug_str_offsets 0 : { *(.debug_str_offsets) }\n"
        ".debug_sup      0 : { *(.debug_sup) }\n"
        ".gnu.attributes 0 : { KEEP(*(.gnu.attributes)) }\n";
}

static void EndSections(std::ostream &out, Block &)
{
    // On Palm OS, the jump table gets appended to the data section and then the
    // whole thing gets compressed. The compression strips leading and trailing
    // NUL bytes, so putting .bss before .data keeps the two sections contiguous
    // and also allows .bss to be eliminated as part of the leading NUL
    // sequence. The order does not matter for Mac OS.
    WriteBssSection(out);
    WriteDataSection(out);
    WriteDebugSections(out);
    // out << "/DISCARD/ : { *(*) }\n";
}

template <bool KEEP>
static void WriteFilters(std::ostream &out, const char *section, const Filters &filters)
{
    static const char* OPEN = KEEP ? "KEEP(" : "";
    static const char* CLOSE = KEEP ? ")" : "";

    for (auto &filter : filters)
    {
        out << OPEN << filter << "(" << section         << CLOSE << ")\n"
            << OPEN << filter << "(" << section << ".*" << CLOSE << ")\n";
    }
}

static void WriteTextStart(std::ostream &out, const char *entryPoint, bool isMultiseg)
{
    out <<
        "_stext = .;\n"
#if 0 // This symbol is used only by code that is disabled in relocate.c
        "PROVIDE(_rsrc_start = .);\n"
#endif

        // This is a Mac OS near model segment header. (It also exists on Palm
        // OS but is ignored.)
        "SHORT(0);\n" // Offset to first entry in jump table
        "SHORT(1);\n" // Number of entries in jump table

        "FILL(" NOP ");\n"
        ". = ALIGN(2);\n";

    WriteTrampoline(out, entryPoint, isMultiseg);

    out <<
        "FILL(0);\n"
        "*(.relocvars)\n"
        "FILL(" NOP ");\n";
}

static void WriteEHFrame(std::ostream &out, const char *id, const Filters &filters)
{
    out << "__EH_FRAME_BEGIN__" << id << " = .;\n";
    WriteFilters<true>(out, ".eh_frame", filters);
    // TODO: Why?
    out << "LONG(0);\n";
    WriteFilters<true>(out, ".gcc_except_table", filters);
}

static void WriteTextEnd(std::ostream &out)
{
    // TODO: This comment seems to be not true except maybe for .data + .bss?
    // Elf2Mac expects the sections to be contiguous, so include the
    // alignment before the end of this section.
    out <<
        ". = ALIGN(4);\n"
        "_etext = .;\n";
}

static void CreateLdScriptSegment(std::ostream &out, const char *entryPoint, const SegmentInfo &segment, bool palmos)
{
    char zeroPaddedId[6];
    std::sprintf(zeroPaddedId, "%05hu", segment.id);

    Block sec(out << ".code" << zeroPaddedId << " 0 : ");

    if(segment.id == 1)
        WriteTextStart(out, entryPoint, true);
    else
    {
        // This is a Mac OS far model segment header (Mac OS) or a CodeWarrior
        // for Palm OS section header (Palm OS).
        auto headerSize = palmos ? 12 : 40;
        out << "FILL(0);\n"

            << "SHORT(0xffff);\n" // Mac OS far model magic number
            << ". += " << headerSize - 2 << ";\n"

            << "FILL(" NOP ");\n";
    }

    WriteFilters<false>(out, ".text", segment.filters);

    if(segment.id == 1)
        WriteInitFini(out);
    else if(segment.id == 2)
        out << "*(.gnu.linkonce.t*)\n";

    // TODO: What is the reason?
    // this is important, for some reason.
    out << ". = ALIGN(4);\n";

    WriteEHFrame(out, segment.id == 1 ? "" : zeroPaddedId, segment.filters);

    if(segment.id == 1)
        WriteTextEnd(out);
    else
        // TODO: Why all this empty space before EH frame?
        // Is this SIZEOF(.eh_frame)?
        out <<
            "FILL(0);\n"
            ". += 0x20;\n"
            "LONG(__EH_FRAME_BEGIN__" << zeroPaddedId << " - .);\n";
}

void SegmentMap::CreateLdScript(std::ostream &out, const char *entryPoint, bool stripMacsbug, bool palmos) const
{
    auto sections = StartSections(out, entryPoint, stripMacsbug, true);

    for(auto &segment : m_segments)
        CreateLdScriptSegment(out, entryPoint, segment, palmos);

    EndSections(out, sections);
}

const std::string &SegmentMap::GetSegmentName(int id) const
{
    for(auto& seg : m_segments)
    {
        if(seg.id == id)
            return seg.name;
    }
    static const auto EMPTY = std::string();
    return EMPTY;
}

void CreateFlatLdScript(std::ostream &out, const char *entryPoint, bool stripMacsbug)
{
    auto sections = StartSections(out, entryPoint, stripMacsbug, false);

    {
        Block section(out << ".text 0 : ");
        WriteTextStart(out, entryPoint, false);

        // This is just to make sure the runtime code comes before the rest
        for (const auto &rt : RUNTIME_OBJECTS)
            out << rt << "(.text*)\n";

        out <<
            "*(.text*)\n"
            "*(.gnu.linkonce.t*)\n";

        WriteInitFini(out);
        WriteEHFrame(out, "", { "*" });
        WriteTextEnd(out);
    }

    // TODO: If everything is just about to get dumped into a single section,
    // why not just skip the middleman and put it all in a single section?
    EndSections(out, sections);
}
