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

#include "Object.h"
#ifdef PALMOS
#include "PalmCompressor.h"
#endif
#include "SegmentMap.h"
#include "Symtab.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <libelf.h>

#include <BinaryIO.h>
#include <ResourceFile.h>
#include <ResourceFork.h>

using namespace std::literals::string_literals;

Object::Object(const std::string &input, bool palmos, uint32_t stackSize)
    : m_codeOsType(palmos ? "code" : "CODE")
    , m_dataOsType(palmos ? "data" : "DATA")
    , m_applOsType(palmos ? "appl" : "APPL")
    , m_jtHeaderSize(palmos ? 0 : 0x20)
    , m_jtEntrySize(palmos ? 6 : 8)
    , m_jtFirstIndex(palmos ? 0 : 2)
    , m_multiCodeOutputBase(palmos ? 12 : 40)
    , m_stackSize(stackSize)
#ifdef PALMOS
    , m_outputFormat(palmos ? ResourceFile::Format::prc : ResourceFile::Format::autodetect)
#else
    , m_outputFormat(ResourceFile::Format::autodetect)
#endif
{
    if(elf_version(EV_CURRENT) == EV_NONE)
        throw std::runtime_error("ELF library initialization failed: "s + elf_errmsg(-1));

    m_fd = open(input.c_str(), O_RDONLY, 0);
    if(m_fd < 0)
        throw std::runtime_error("Opening "s + input.c_str() + " failed: " + std::strerror(errno));

    m_elf = elf_begin(m_fd, ELF_C_READ, NULL);
    if(m_elf == nullptr)
        throw std::runtime_error("Reading ELF failed: "s + elf_errmsg(-1));

    loadSections();
}

Object::~Object()
{
    if (m_elf)
        elf_end(m_elf);
    if (m_fd >= 0)
        close(m_fd);
}

template <typename T>
Object::SSec<T>::SSec(Elf_Scn *scn)
    : section(scn)
    , header(elf32_getshdr(scn))
    , data(static_cast<T *>(elf_getdata(scn, nullptr)->d_buf))
{
}

void Object::loadSections()
{
    size_t shstrtabIndex;
    if(elf_getshdrstrndx(m_elf, &shstrtabIndex) != 0)
        throw std::runtime_error("ELF error finding .shstrtab: "s + elf_errmsg(-1));

    m_shstrtab = static_cast<const char *>(
        elf_getdata(elf_getscn(m_elf, shstrtabIndex), nullptr)->d_buf);

    // Creating symtab indexes requires strtab, which may not be present until
    // later, depending on the order of the sections in the ELF file
    Elf_Scn *symtabScn = nullptr;

    for(Elf_Scn *scn = nullptr; (scn = elf_nextscn(m_elf, scn)) != nullptr; )
    {
        const Elf32_Shdr &shdr = *elf32_getshdr(scn);
        const char *name = m_shstrtab + shdr.sh_name;

        switch(shdr.sh_type)
        {
        case SHT_STRTAB:
            if(std::strcmp(name, ".strtab") == 0)
                m_strtab = static_cast<const char *>(elf_getdata(scn, nullptr)->d_buf);
            break;
        case SHT_SYMTAB:
            symtabScn = scn;
            break;
        case SHT_RELA:
            if(shdr.sh_flags & SHF_INFO_LINK)
                m_rela.emplace_back(scn);
            break;
        case SHT_PROGBITS:
            if(shdr.sh_flags & SHF_ALLOC)
            {
                if(std::strcmp(name, ".data") == 0)
                    m_data = scn;
                else
                {
                    // The output sections in the linker script have to be
                    // sorted according to input match order because that
                    // is how GNU ld works, but the final output should be
                    // sorted by resource IDs which are given in the output
                    // section name
                    auto pos = std::lower_bound(m_code.begin(), m_code.end(), name,
                        [&](SSec<uint8_t> &section, const char *newName) {
                            const char *ownName = m_shstrtab + section.header->sh_name;
                            return std::strcmp(ownName, newName) < 0;
                        });
                    m_code.insert(pos, scn);
                }
            }
            break;
        case SHT_NOBITS:
            m_bss = scn;
            break;
        }
    }

    m_symtab.Load(symtabScn, m_strtab);
}

void Object::emitFlatCode(std::ostream &out, Elf32_Addr newBase)
{
    std::vector<RuntimeReloc> relocs;

    for(auto &section : m_rela)
    {
        auto sectionRelocs = relocateSection(section, newBase, 1, false);
        relocs.insert(relocs.cend(), sectionRelocs.begin(), sectionRelocs.end());
        newBase += section.header->sh_size;
    }

    for (auto &code : m_code)
        out << code.view();

    if (m_data)
        out << m_data.view();

    out << SerializeRelocs(relocs);
}

void Object::FlatCode(const std::string &filename)
{
    std::ofstream out(filename);
    emitFlatCode(out, 0);
}

static inline char hexchar(char c)
{
	// Code golf to eliminate branching for no good reason
	// For 0-9, low nibble is 0 to 9 and bit 6 is clear
	// For A-F or a-f, low nibble is 1 to 6 and bit 6 is set; add 9 by shifting
    // bit 6 to add 1 + 8 to get correct value
	return ((c & 0xf) + (c >> 6) + ((c >> 3) & 0x8));
}

static std::string fromhex(const char *hex)
{
    std::string bin;
    for(auto p = hex, end = hex + std::strlen(hex); p != end; ++p)
    {
        if(std::isspace(*p))
            continue;

        char c = hexchar(*p++);
        bin.push_back((c << 4) | hexchar(*p));
    }
    return bin;
}

void Object::SingleSegmentApp(const std::string &filename)
{
    ResourceFile file;
    Resources& rsrc = file.resources;

    rsrc.addResource(Resource(m_codeOsType, 0, fromhex(
        "00000028 00000000 00000008 00000020"
        "0000 3F3C 0001 A9F0"
    )));

    {
        std::ostringstream code1;
        emitFlatCode(code1, secOutputBase(1));
        rsrc.addResource(Resource(m_codeOsType, 1, code1.str()));
    }

    if (isPalm())
        emitPref(rsrc);

    finalizeFile(filename, file);
}

static const Elf32_Sym *FindExceptionInfoStart(const Symtab &symtab, uint16_t codeID)
{
    char marker[] = "__EH_FRAME_BEGIN__\0\0\0\0\0";
    if(codeID > 1)
        std::sprintf(marker + sizeof(marker - 6), "%05hu", codeID);
    auto index = symtab.FindSym(marker);

    const Elf32_Sym *start = nullptr;
    if(index != -1)
        start = symtab.GetSym(index);
    return start;
}

/*
    ld behaves differently depending on whether debug info is present.
    If debug info is present, .eh_frame sections will contain references
    to other code segments, if no debug info is generated (or it is
    stripped at link time), then these pointers are set to 0 during linking.

    In most cases, this has to do with weak symbols; the instance of the
    symbol that is removed gets a null ptr (with R_68K_NONE relocation) in
    the .eh_frame section if there is no debug info, but gets remapped to
    the surviving instance if there is debug info.
    It also happens with some section symbols, and I *hope* this is related.

    This makes no sense to me, but the reason is probably buried somewhere
    within a 900-line function of C code within a 15000 line source file
    in GNU bfd.

    I *hope* that the correct behavior is to just clear those pointers.
*/
bool Object::isOffsetInEhFrame(uint16_t codeID, Elf32_Addr vaddr) const
{
    auto ehStart = FindExceptionInfoStart(m_symtab, codeID);

    if(m_verbose)
    {
        if (ehStart)
        {
            auto header = elf32_getshdr(elf_getscn(m_elf, ehStart->st_shndx));
            auto codeSize = header->sh_size;
            auto exceptionSize = header->sh_addr + codeSize - ehStart->st_value;
            auto percent = 100.0 * exceptionSize / codeSize;

            std::cout
                << m_codeOsType << "," << codeID
                << " has " << exceptionSize
                << " bytes of exception info (" << percent << "%)"
                "\n";
        }
        else
        {
            std::cout
                << "exception info marker not found for"
                << m_codeOsType << "," << codeID
                << "\n";
        }
    }

    if (!ehStart || vaddr < ehStart->st_value)
        return false;

    if (std::strcmp(m_strtab + ehStart->st_name, "__gxx_personality_v0") == 0)
        return false;

    return true;
}

uint32_t Object::getU32BE(const SSec<uint8_t> &section, Elf32_Addr vaddr)
{
    const uint8_t *p = section.data + vaddr - section.header->sh_addr;
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

void Object::setU32BE(SSec<uint8_t> &section, Elf32_Addr vaddr, uint32_t value)
{
    uint8_t *p = section.data + vaddr - section.header->sh_addr;
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

Elf32_Addr Object::jumpTableAddress(const Elf32_Sym *symbol, uint16_t codeID)
{
    auto &jumpTable = m_jumpTables[symbol->st_shndx];
    size_t jtIndex;
    if (auto it = jumpTable.find(symbol->st_value); it != jumpTable.end())
        jtIndex = it->second;
    else
    {
        const auto *target = elf32_getshdr(elf_getscn(m_elf, symbol->st_shndx));
        jtIndex = m_jumpTable.size();
        jumpTable.emplace(symbol->st_value, jtIndex);
        m_jumpTable.push_back(symbol->st_value - target->sh_addr + secOutputBase(codeID));
    }

    return m_jtHeaderSize + jtIndex * m_jtEntrySize;
}

static bool IsValidSection(Elf32_Section index)
{
    return index != SHN_UNDEF && index < SHN_LORESERVE;
}

std::vector<RuntimeReloc> Object::relocateSection(SSec<Elf32_Rela> &relaSection, Elf32_Addr newBase, uint16_t codeID, bool multiSegment)
{
    std::vector<RuntimeReloc> outRelocs;

    auto *rela = relaSection.data;
    const auto *end = rela + relaSection.size();

    const auto sourceIndex = relaSection.header->sh_info;
    SSec<uint8_t> source { elf_getscn(m_elf, sourceIndex) };

    auto base = source.header->sh_addr;
    auto baseDelta = newBase - base;
    auto maxOffset = base + source.header->sh_size - sizeof(uint32_t);

    for (; rela != end; ++rela)
    {
        auto relaType = ELF32_R_TYPE(rela->r_info);
        if (relaType != R_68K_32 && relaType != R_68K_PC32)
            continue;

        if(rela->r_offset < base || rela->r_offset > maxOffset)
        {
            if (m_verbose)
            {
                // FIXME: There are sometimes relocations beyond the end of the sections
                //        in LD output for some reason. That's bad. Let's ignore it.
                std::cerr
                    << "Relocation out of range in "
                    << m_shstrtab + source.header->sh_name << "; "
                    << std::hex
                    << "0x" << base << " >= "
                    << "0x" << rela->r_offset << " >= "
                    << "0x" << maxOffset
                    << std::dec
                    << "\n";
            }

            continue;
        }

        const auto *targetSymbol = m_symtab.GetSym(ELF32_R_SYM(rela->r_info));
        if(!targetSymbol || !IsValidSection(targetSymbol->st_shndx))
            continue;

        if ((source.header->sh_flags & SHT_PROGBITS)
            && targetSymbol->st_shndx != sourceIndex
            && isOffsetInEhFrame(codeID, rela->r_offset))
        {
            // Case 1:
            //  references from .eh_frame, with the exception of __gcc_personality_v0.
            //  Should be direct references within the code segment.
            if (m_verbose)
            {
                const auto *target = elf32_getshdr(elf_getscn(m_elf, targetSymbol->st_shndx));
                std::cerr
                    << "Warning: clearing cross-segment reference from .eh_frame:\n"
                    << m_strtab + targetSymbol->st_name
                    << " (" << m_shstrtab + source.header->sh_name
                    << "->" << m_shstrtab + target->sh_name
                    << ")\n"
                    << std::hex
                    << "0x" << int(targetSymbol->st_info)
                    << " 0x" << int(targetSymbol->st_other)
                    << std::dec
                    << "\n";
            }

            setU32BE(source, rela->r_offset, 0);
            rela->r_info = 0;
        }
        else if (multiSegment
            && ELF32_ST_TYPE(targetSymbol->st_info) == STT_FUNC
            && targetSymbol->st_shndx != sourceIndex)
        {
            // Case 2: References to code that can go through the jump table.
            //  If we need an addend, that's a problem and we abort.
            auto &addend = rela->r_addend;
            if(addend != 0)
            {
                auto candidateIndex = m_symtab.FindSym(targetSymbol->st_shndx,
                    targetSymbol->st_value + addend);
                if(candidateIndex != -1)
                {
                    targetSymbol = m_symtab.GetSym(candidateIndex);
                    if(!IsValidSection(targetSymbol->st_shndx))
                        continue;
                    rela->r_info = ELF32_R_INFO(candidateIndex, ELF32_R_TYPE(rela->r_info));
                    addend = 0;
                }
            }

            if (addend == 0)
                setU32BE(source, rela->r_offset, jumpTableAddress(targetSymbol, codeID));
            else
            {
                const auto *target = elf32_getshdr(elf_getscn(m_elf, targetSymbol->st_shndx));
                std::cerr
                    << std::hex
                    << "Invalid ref from " << m_shstrtab + source.header->sh_name
                    << ":0x" << rela->r_offset - base
                    << " to " << m_shstrtab + target->sh_name
                    << "(" << (m_strtab + targetSymbol->st_name) << ")"
                    << "+0x" << rela->r_offset
                    << std::dec
                    << "\n";
                throw std::runtime_error("Relocation failure");
            }
        }
        else
            setU32BE(source, rela->r_offset, getU32BE(source, rela->r_offset) + baseDelta);

        uint32_t offset = rela->r_offset;
        if(multiSegment)
            offset -= source.header->sh_addr;

        if(relaType == R_68K_PC32 && targetSymbol->st_shndx == sourceIndex)
            continue;

        // TODO: determine relocBase
        RelocBase relocBase;

        if(relaType == R_68K_32)
            outRelocs.emplace_back(relocBase, offset);
        else if(relaType == R_68K_PC32 && targetSymbol->st_shndx != sourceIndex)
            outRelocs.emplace_back(relocBase, offset, true);
    }

    return outRelocs;
}

void Object::emitRes0(Resources &rsrc, uint32_t belowA5)
{
    uint32_t dataBase = -belowA5;
    uint32_t bssBase = -m_bss.header->sh_size;

    std::ostringstream code0;
    std::ostringstream data0;

    auto jtDataSize = m_jtEntrySize * (m_jumpTable.size() + m_jtFirstIndex);
    // TODO: Not totally clear what the fixed value should be on Palm OS.
    // The loader will always shove a pointer to SysAppInfoType at A5BASE
    // (same place where Mac OS shoves pointer to QDGlobals).
    // prc-tools thinks that its data resource should be
    // `128 + 33 * (data_size / 32)`, but these are all magic numbers and it
    // seems like it uses some of A5 for its own relocation junk.
    auto aboveA5 = m_jtHeaderSize + jtDataSize;

    longword(code0, aboveA5);
    longword(code0, belowA5);

    // The rest of code0 is ignored by Palm OS, but keep the rest the same
    // for compatibility with Palm OS disassemblers, utilities, etc.
    longword(code0, isPalm() ? 8 : jtDataSize);
    longword(code0, m_jtHeaderSize);

    // Jump table entry for default entrypoint
    // [function offset].w [move.w #resource id,-(sp)] [LoadSeg].w
    code0 << fromhex("0000 3F3C 0001 A9F0");

    if (!isPalm())
        // flag entry to switch to “new format” 32-bit jump table entries
        code0 << fromhex("0000 FFFF 0000 0000");

    uint16_t codeID = 0; // TODO: Must emit by each individual jump table
    for (auto offset : m_jumpTable)
    {
        enum { kLoadSeg = 0xa9f0, kJmpAbsL = 0x4ef9 };

        if (isPalm())
        {
            word(data0, kJmpAbsL);
            longword(data0, offset);
        }
        else
        {
            word(code0, codeID);
            word(code0, kLoadSeg);
            longword(code0, offset);
        }
    }

    if(m_verbose)
    {
        std::cout
            << m_codeOsType << " 0: " << code0.str().size() << " bytes\n"
            << "above A5: " << aboveA5 << " bytes\n"
            << "below A5: " << belowA5 << " bytes\n"
            << ".data: " << m_data.header->sh_size << " bytes at A5-0x"
            << std::hex << belowA5 << std::dec << "\n"
            << ".bss: " << m_bss.header->sh_size << " bytes at A5-0x"
            << std::hex << m_bss.header->sh_size << std::dec << "\n";
    }

    rsrc.addResource(Resource(m_codeOsType, 0, code0.str()));

        std::vector<RuntimeReloc> dataRelocs; // TODO
        std::vector<RuntimeReloc> code1Relocs; // TODO

    if (isPalm())
    {
        data0 << SerializeRelocsPalm(dataRelocs, false);
        data0 << SerializeRelocsPalm(code1Relocs, false);
        rsrc.addResource(Resource(m_dataOsType, 0, data0.str()));
    }
    else
    {
        rsrc.addResource(Resource(m_dataOsType, 0, m_data.view().data()));
        rsrc.addResource(Resource("RELA", 0, SerializeRelocs(dataRelocs)));
        rsrc.addResource(Resource("RELA", 1, SerializeRelocs(code1Relocs)));
    }
}

void Object::MultiSegmentApp(const std::string &filename, const SegmentMap &segmentMap)
{
    ResourceFile file;
    Resources& rsrc = file.resources;

    uint32_t belowA5 = m_data.header->sh_size + m_bss.header->sh_size;
    emitRes0(rsrc, belowA5);

    uint16_t codeID = 1;
    for (auto &section : m_code)
    {
        const auto &jumpTable = m_jumpTables[elf_ndxscn(section.section)];
        auto firstIndex = jumpTable.at(0);

        std::ostringstream code;
        if(codeID == 1)
        {
            word(code, 0);
            word(code, 1);
        }
        else if (isPalm())
        {
            size_t a5JtOffset = (m_jtEntrySize * firstIndex) - belowA5;
            word(code, a5JtOffset);
            word(code, jumpTable.size());
            longword(code, a5JtOffset);
            longword(code, section.header->sh_addr + section.header->sh_size);
        }
        else
        {
            word(code, 0xFFFF);
            word(code, 0);
            longword(code, 0);
            longword(code, 0);
            longword(code, m_jtEntrySize * firstIndex);
            longword(code, jumpTable.size());
            longword(code, 0);    // reloc info for A5
            longword(code, 0);    // assumed address for A5
            longword(code, 0);    // reloc info for code
            longword(code, 0);    // assumed address for start of code resource
            longword(code, 0);
        }

        code << section.view();
        std::vector<RuntimeReloc> codeRelocs; // TODO

        if (isPalm() && codeID != 1)
            code << SerializeRelocs(codeRelocs);
        else if (!isPalm())
            rsrc.addResource(Resource(ResType("RELA"), codeID, SerializeRelocs(codeRelocs)));

        if(m_verbose)
        {
            std::cout
                << m_codeOsType << " " << codeID << ": "
                << code.str().size() << " bytes\n";
        }

        auto segmentName = segmentMap.GetSegmentName(codeID);
        rsrc.addResource(Resource(m_codeOsType, codeID, code.str(), std::move(segmentName)));

        ++codeID;
    }

    if (isPalm())
        emitPref(rsrc);

    finalizeFile(filename, file);
}

void Object::finalizeFile(const std::string &filename, ResourceFile &file)
{
    file.creator = ResType("????");
    file.type = m_applOsType;
    file.data = "Built using Retro68.";
    file.write(filename, m_outputFormat);
}

#ifdef PALMOS
std::string Object::compressData(std::string &&input)
{
    if (!isPalm())
        return input;

    return CompressPalmData(std::move(input));
}

void Object::emitPref(Resources &rsrc)
{
    // 0xd00 is the default value used by Palm OS SysAppLaunch if pref 0 is
    // missing
    if (m_stackSize == 0 || m_stackSize == 0xd00)
        return;

    std::ostringstream pref0;
    word(pref0, 30); // AMX task priority
    longword(pref0, m_stackSize); // Stack size
    longword(pref0, 0x1000); // Minimum heap free heap
    rsrc.addResource(Resource(ResType("pref"), 0, pref0.str()));
}
#endif
