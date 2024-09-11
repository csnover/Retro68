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

#include "SegmentMap.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static const char scriptStart[] = R"ld(/* ld script for Elf2Mac */
ENTRY( @entryPoint@ )
SECTIONS
{
)ld";

static const char textSection[] = R"ld(/* ld script for Elf2Mac */
    .text :    {
        _stext = . ;
        PROVIDE(_rsrc_start = .);
        *(.rsrcheader)
        . = ALIGN (2);

        /* The entry point. */
        _entry_trampoline = .; /* record current address for displacement */
        // TODO: This could also use the preinit section on Mac
        SHORT(DEFINED(__break_on_entry) ? 0xA9FF /* Debugger() */ : 0x4e71 /* nop */);
        LONG(0x61000002); /* bsr *+2                ; push pc to stack */
        SHORT(0x0697);    /* addi.l #ENTRY, (a7)    ; displace pc to entry point */
        LONG(@entryPoint@ - _entry_trampoline - 6 /* sizeof(addi.l) */); /* #ENTRY */
        PROVIDE(_start = .); /* fallback entry point to a safe spot - needed for libretro bootstrap when there is a custom entry point */
        Retro68InitMultisegApp = .; /* override this for the single-segment case */
        SHORT(0x4e75); /* rts                       ; jump to entry point */

        *(.relocvars)
        */libretrocrt.a:start.c.obj(.text*)
        */libretrocrt.a:relocate.c.obj(.text*)
        */libretrocrt.a:*(.text*)
        */libInterface.a:*(.text*)
        *(.text*)

        *(.stub)
        *(.gnu.linkonce.t*)
        *(.glue_7t)
        *(.glue_7)
        *(.jcr)
        . = ALIGN (4) ;
        __preinit_section = . ;
        KEEP (*(.preinit))
        __preinit_section_end = . ;
        __init_section = . ;
        KEEP (*(.init))
        __init_section_end = . ;
        __fini_section = . ;
        KEEP (*(.fini))
        __fini_section_end = . ;

        __EH_FRAME_BEGIN__ = .;
        KEEP(*(.eh_frame))
        LONG(0);

        KEEP(*(.gcc_except_table))
        KEEP(*(.gcc_except_table.*))

        /* NOTE: Elf2Mac expects the sections to be contiguous,
                 so include the alignment before the end of this section.
        */
        . = ALIGN(0x4) ;
        _etext = . ;
    }
)ld";

static const char textCode1[] = R"ld(
        _stext = .;
        FILL(0x4E71);
        PROVIDE(_rsrc_start = .);
        . = ALIGN (2);
        _entry_trampoline = .;
        SHORT(DEFINED(__break_on_entry) ? 0xA9FF : 0x4e71);
        LONG(0x61000002);    /* bsr *+2 */
        SHORT(0x0697); /* addi.l #_, (a7) */
        LONG(@entryPoint@ - _entry_trampoline - 6);
        PROVIDE(_start = .);  /* fallback entry point to a safe spot - needed for libretro bootstrap */
        SHORT(0x4e75); /* rts */

        FILL(0);
        *(.relocvars)
        FILL(0x4E71);
)ld";

static const char scriptEnd[] = R"ld(
    .data : {
        _sdata = . ;
        *(.got.plt)
        *(.got)
        FILL(0) ;
        . = ALIGN(0x20) ;
        LONG(-1)
        . = ALIGN(0x20) ;
        *(.rodata)
        *(.rodata1)
        *(.rodata.*)
        *(.gnu.linkonce.r*)
        *(.data)
        *(.data1)
        *(.data.*)
        *(.gnu.linkonce.d*)

        . = ALIGN(4) ;
        __CTOR_LIST__ = .;
        KEEP (*(.ctors))
        KEEP (*(SORT(.ctors.*)))
        __CTOR_END__ = .;
        LONG(0);

        . = ALIGN(0x4);
        __DTOR_LIST__ = .;
        KEEP (*(.dtors))
        KEEP (*(SORT(.dtors.*)))
        __DTOR_END__ = .;
        LONG(0);

        . = ALIGN(0x4);
        _edata = . ;
    }
    .bss ALIGN(0x4) : {
        _sbss = .;
        *(.dynsbss)
        *(.sbss)
        *(.sbss.*)
        *(.scommon)
        *(.dynbss)
        *(.bss)
        *(.bss.*)
        *(.bss*)
        *(.gnu.linkonce.b*)
        *(COMMON)
        . = ALIGN(0x10) ;
        _ebss = . ;
    }


    /* **** Debugging information sections.
     * Keep them for now, they are discarded by Elf2Mac. */

    /DISCARD/ : { *(.note.GNU-stack) }
    /* Stabs debugging sections.    */
    .stab 0 : { *(.stab) }
    .stabstr 0 : { *(.stabstr) }
    .stab.excl 0 : { *(.stab.excl) }
    .stab.exclstr 0 : { *(.stab.exclstr) }
    .stab.index 0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment 0 : { *(.comment) }
    /* DWARF debug sections.
      Symbols in the DWARF debugging sections are relative to the beginning
      of the section so we begin them at 0.  */
    /* DWARF 1 */
    .debug 0 : { *(.debug) }
    .line 0 : { *(.line) }
    /* GNU DWARF 1 extensions */
    .debug_srcinfo 0 : { *(.debug_srcinfo) }
    .debug_sfnames 0 : { *(.debug_sfnames) }
    /* DWARF 1.1 and DWARF 2 */
    .debug_aranges 0 : { *(.debug_aranges) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    /* DWARF 2 */
    .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_line 0 : { *(.debug_line) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_str 0 : { *(.debug_str) }
    .debug_loc 0 : { *(.debug_loc) }
    .debug_macinfo 0 : { *(.debug_macinfo) }
    /* SGI/MIPS DWARF 2 extensions */
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .debug_funcnames 0 : { *(.debug_funcnames) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames 0 : { *(.debug_varnames) }

    /DISCARD/ : { *(*) }
}

)ld";

SegmentMap::SegmentMap()
{
    m_segments.emplace_back(1, "Runtime",
                          "*/libretrocrt.a:start.c.obj",
                          "*/libretrocrt.a:relocate.c.obj",
                          "*/libretrocrt.a:MultiSegApp.c.obj",
                          "*/libretrocrt.a:LoadSeg.s.obj",
                          "*/libretrocrt.a:*",
                          "*/libInterface.a:*",
                          "*/libgcc.a:*",
                          "*/libc.a:*"
                          );
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
    m_segments.emplace_back(1, "Runtime",
                          "*/libretrocrt.a:start.c.obj",
                          "*/libretrocrt.a:relocate.c.obj",
                          "*/libretrocrt.a:MultiSegApp.c.obj",
                          "*/libretrocrt.a:LoadSeg.s.obj",
                          "*/libretrocrt.a:*",
                          "*/libInterface.a:*",
                          "*/libgcc.a:*",
                          "*/libc.a:*"
                          );

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

static void StrTpl(std::ostream &out, const char *tpl, size_t tplSize, const char *var, size_t varSize, const char *value)
{
    const char *last = tpl;
    const char *end = tpl + tplSize;
    while ((tpl = std::strstr(tpl, var)) != nullptr)
    {
        out.write(last, tpl - last);
        out << value;
        tpl += varSize - 1;
        last = tpl;
    }
    out.write(last, end - last);
}

template <size_t TplN, size_t VarN>
static void StrTpl(std::ostream &out, const char (&tpl)[TplN], const char (&var)[VarN], const char *value)
{
    StrTpl(out, tpl, TplN, var, VarN, value);
}

void CreateFlatLdScript(std::ostream &out, const char *entryPoint, bool stripMacsbug)
{
    out << "_MULTISEG_APP = 0;\n";
    StrTpl(out, scriptStart, "@entryPoint@", entryPoint);
    if(stripMacsbug)
        out <<
            "\t.strippedmacsbugnames 0 (NOLOAD) : { *(.text.*.macsbug) }\n"
            "\t. = 0;\n";
    StrTpl(out, textSection, "@entryPoint@", entryPoint);
    out << scriptEnd;
}


void SegmentInfo::WriteFilters(std::ostream &out, const std::string &section) const
{
    for(auto &filter : filters)
    {
        out << "        " << filter << "(" << section << ")\n";
        out << "        " << filter << "(" << section << ".*)\n";
    }
}

void SegmentInfo::WriteFiltersKeep(std::ostream &out, const std::string &section) const
{
    for(auto &filter : filters)
    {
        out << "\t\tKEEP(" << filter << "(" << section << "))\n";
        out << "\t\tKEEP(" << filter << "(" << section << ".*))\n";
    }
}

void SegmentInfo::CreateLdScript(std::ostream &out, const char *entryPoint) const
{
    char zeroPaddedId[6];
    std::sprintf(zeroPaddedId, "%05hu", id);

    out <<
        "\t.code" << zeroPaddedId << " : {\n"
        "\t\tFILL(0x4E71);\n";

    if(id == 1)
        StrTpl(out, textCode1, "@entryPoint@", entryPoint);

    WriteFilters(out, ".text");

    if(id == 1)
    {
        out << R"ld(
        . = ALIGN (4) ;
        __init_section = .;
        KEEP (*(.init))
        __init_section_end = .;
        __fini_section = .;
        KEEP (*(.fini))
        __fini_section_end = .;
)ld";
    }
    else if(id == 2)
        out << "\t\t*(.gnu.linkonce.t*)\n";

    out << "\t\t. = ALIGN (4);\n";    // this is important, for some reason.
    if(id == 1)
        out << "\t\t__EH_FRAME_BEGIN__" << " = .;\n";
    else
        out << "\t\t__EH_FRAME_BEGIN__" << zeroPaddedId << " = .;\n";
    WriteFiltersKeep(out, ".eh_frame");
    out << "\t\tLONG(0);\n";
    WriteFiltersKeep(out, ".gcc_except_table");

    if(id == 1)
    {
        out << R"ld(
        . = ALIGN(0x4) ;
        _etext = . ;
)ld";
    }
    else
    {
        StrTpl(out, R"ld(
        . = ALIGN(0x4);
        FILL(0);
        . += 32;
        LONG(__EH_FRAME_BEGIN__@N@ - .);
)ld", "@N@", zeroPaddedId);
    }

    out << "\t}\n";

}

void SegmentMap::CreateLdScript(std::ostream &out, const char *entryPoint, bool stripMacsbug) const
{
    out << "_MULTISEG_APP = 1;\n";

    StrTpl(out, scriptStart, "@entryPoint@", entryPoint);

    if(stripMacsbug)
        out <<
            "\t.strippedmacsbugnames 0 (NOLOAD) : { *(.text.*.macsbug) }\n"
            "\t. = 0;\n";

    for(auto &seg : m_segments)
        seg.CreateLdScript(out, entryPoint);

    out << scriptEnd;
}
