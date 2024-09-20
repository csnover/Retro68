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

enum { kNoOp = 0x4e71 };

using namespace std::literals::string_literals;

Object::Object(const std::string &input, bool palmos, uint32_t stackSize, bool verbose)
    : m_codeOsType(palmos ? "code" : "CODE")
    , m_dataOsType(palmos ? "data" : "DATA")
    , m_applOsType(palmos ? "appl" : "APPL")
    , m_jtHeaderSize(palmos ? 4 : 0x20)
    , m_jtEntrySize(palmos ? 6 : 8)
    , m_jtFirstIndex(palmos ? 0 : 2)
    , m_stackSize(stackSize)
#ifdef PALMOS
    , m_outputFormat(palmos ? ResourceFile::Format::prc : ResourceFile::Format::autodetect)
#else
    , m_outputFormat(ResourceFile::Format::autodetect)
#endif
    , m_verbose(verbose)
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
    processRelocations();
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
            m_symtab = scn;
            break;
        case SHT_REL:
            // The documented difference between SHT_REL and SHT_RELA is that
            // the addend is stored at the offset in the .text section for
            // SHT_REL instead of in the relocation record, so it would be
            // trivially supportable, but does not need to be if the compiler
            // never creates these kinds of records to begin with
            throw std::runtime_error("SHT_REL not supported");
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
        default:
            if (m_verbose)
            {
                std::cout
                    << "Skipping section " << elf_ndxscn(scn)
                    << " (" << m_shstrtab + shdr.sh_name << ")" << std::endl;
            }
        }
    }

    if (!m_symtab)
        throw std::runtime_error("Could not find .symtab");

    if (!m_strtab)
        throw std::runtime_error("Could not find .strtab");

    if (m_code.empty())
        throw std::runtime_error("No code sections found");

    if (isPalm())
    {
        int expected = 1;
        for (const auto &code : m_code)
        {
            // The code for loading all the resources on Palm OS uses
            // `DmGet1Resource` because `DmFindResourceType` bloats the runtime;
            // it could be changed if this is annoying but there is really no
            // good reason to split up code IDs like this on Palm OS
            if (getCodeID(code.index()) != expected++)
                throw std::runtime_error("Code segment IDs must be contiguous;"
                    " expected "s + std::to_string(expected - 1)
                    + ", got " + std::to_string(getCodeID(code.index())));
        }
    }
}

void Object::emitFlatCode(std::ostream &out)
{
    // Since the ld script already only creates a single .text section, there is
    // no reason to do anything except assert here that nothing funky happened.
    if (m_code.size() != 1)
        throw std::runtime_error("Cannot emit flat code with multiple sections");

    out << m_code.front().view();

    if (m_data)
    {
        out << m_data.view();

        Relocations combined(m_relocations[m_code.front().index()]);

        const auto &dataReloc = m_relocations[m_data.index()];
        int relocBase = RelocBaseFirst;
        for (const auto &relocations : dataReloc)
        {
            if (!relocations.empty())
            {
                auto &group = combined[relocBase];
                auto middle = group.size();
                group.insert(group.end(), relocations.begin(), relocations.end());
                std::inplace_merge(group.begin(), group.begin() + middle, group.end());
            }
            ++relocBase;
        }

#ifdef PALMOS
        if (isPalm())
            out << SerializeRelocsPalm(combined, false,
                m_code.front().size(), m_code.front().size() + m_data.size());
        else
#endif
            out << SerializeRelocs(combined);
    }
    else
    {
        const auto &relocs = m_relocations[m_code.front().index()];
#ifdef PALMOS
        if (isPalm())
            out << SerializeRelocsPalm(relocs, false, 0, 0);
        else
#endif
            out << SerializeRelocs(relocs);
    }
}

static void word(uint8_t *p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static void longword(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

void Object::FlatCode(const std::string &filename)
{
    std::ofstream out(filename);

    // To avoid having to rebase the whole section just because there is a
    // resource header, replace it with some no-ops.
    word(m_code.front().data, kNoOp);
    word(m_code.front().data + 2, kNoOp);

    emitFlatCode(out);
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
        emitFlatCode(code1);
        rsrc.addResource(Resource(m_codeOsType, 1, code1.str()));
    }

#ifdef PALMOS
    if (isPalm())
        emitPref(rsrc);
#endif

    finalizeFile(filename, file);
}

const Elf32_Sym *Object::findExceptionInfoStart(uint16_t codeID) const
{
    if (m_ehFrameCache.empty())
    {
        // Hack to avoid constantly trying to create the cache if there are no
        // EH frames in the symbol table
        m_ehFrameCache[0] = nullptr;

        const auto *symbol = m_symtab.data;
        const auto *end = m_symtab.data + m_symtab.size();
        for (; symbol != end; ++symbol)
        {
            if (!symbol->st_name)
                continue;

            const char *name = m_strtab + symbol->st_name;
            constexpr const char *prefix = "__EH_FRAME_BEGIN__";
            constexpr auto len = std::strlen(prefix);
            if (std::strncmp(name, prefix, len) != 0)
                continue;

            int frameCodeID;
            if (name[len] == '\0')
                frameCodeID = 1;
            else
                frameCodeID = std::atoi(name + len);
            m_ehFrameCache[frameCodeID] = symbol;
        }
    }

    return m_ehFrameCache.at(codeID);
}

bool Object::isOffsetInEhFrame(uint16_t codeID, Elf32_Addr vaddr) const
{
    if (codeID == 0)
        return false;

    auto ehStart = findExceptionInfoStart(codeID);

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
                << std::endl;
        }
        else
        {
            std::cerr
                << "Exception info marker not found for "
                << m_codeOsType << "," << codeID
                << std::endl;
        }
    }

    if (!ehStart || vaddr < ehStart->st_value)
        return false;

    if (std::strcmp(m_strtab + ehStart->st_name, "__gxx_personality_v0") == 0)
        return false;

    return true;
}

uint16_t Object::getU16BE(const SSec<uint8_t> &section, Elf32_Addr vaddr)
{
    uint8_t *p = section.data + vaddr - section.header->sh_addr;
    return p[0] << 8 | p[1];
}

void Object::setU16BE(SSec<uint8_t> &section, Elf32_Addr vaddr, uint32_t value)
{
    word(section.data + vaddr - section.header->sh_addr, value);
}

void Object::setU32BE(SSec<uint8_t> &section, Elf32_Addr vaddr, uint32_t value)
{
    longword(section.data + vaddr - section.header->sh_addr, value);
}

Object::XrefKind Object::getXrefKind(uint16_t codeID, Elf32_Section source,
    const Elf32_Rela *rela, const Elf32_Sym *target) const
{
    if (!target)
        return XrefKind::Invalid;

    auto targetSection = target->st_shndx;

    if (targetSection != source)
    {
        // ld behaves differently depending on whether debug info is present.
        // If debug info is present, .eh_frame sections will contain references
        // to other code segments, if no debug info is generated (or it is
        // stripped at link time), then these pointers are set to 0 during linking.
        //
        // In most cases, this has to do with weak symbols; the instance of the
        // symbol that is removed gets a null ptr (with R_68K_NONE relocation) in
        // the .eh_frame section if there is no debug info, but gets remapped to
        // the surviving instance if there is debug info.
        // It also happens with some section symbols, and I *hope* this is related.
        //
        // This makes no sense to me, but the reason is probably buried somewhere
        // within a 900-line function of C code within a 15000 line source file
        // in GNU bfd.
        //
        // I *hope* that the correct behavior is to just clear those pointers.
        if (isOffsetInEhFrame(codeID, rela->r_offset))
            return XrefKind::InvalidEhFrame;

        // Inter-section PC-relative relocations require extra runtime
        // arithmetic to calculate a delta between the two loaded sections,
        // which is not supported by OS native relocation code. For the moment,
        // mark this kind of relocation as invalid, and reimplement the support
        // later in the runtime if it turns out to be actually required.
        // TODO: PCREL to the same section should just addend and emit nothing.
        else if (ELF32_R_TYPE(rela->r_info) == R_68K_PC32)
            return XrefKind::Invalid;
    }


    // Intra-section, data, and code 1 xrefs are always valid since the only
    // limit on xrefs is whether or not the target section is actually loaded
    // (and whether the operand is wide enough to store the addend, which should
    // be ensured by the compiler).
    if (targetSection == source
        || targetSection == m_data.index()
        || targetSection == m_bss.index()
        || targetSection == m_code.front().index())
        return XrefKind::Direct;

    // Inter-section xrefs to code are always OK because those can be offset to
    // the jump table which will call _LoadSeg if needed.
    if (ELF32_ST_TYPE(target->st_info) == STT_FUNC)
        return XrefKind::Indirect;

    // Target section is not guaranteed to be loaded so xref is not possible. If
    // this is an issue it is maybe possible to move the target symbol to the
    // data segment, but this is not done right now. For Palm OS, maybe it is
    // also possible to extend the relocation table to include relocations for
    // all sections instead of just self, data, and code 1, since all the
    // sections are actually always loaded and so probably the only restriction
    // is that the OG Palm OS compilers were simple and based on Mac OS code so
    // did not do this.
    return XrefKind::Invalid;
}

void Object::processRelocations()
{
    for (auto &rela : m_rela)
        processRelocation(rela);
}

static const uint8_t *RelaSizes() {
    static const uint8_t SIZES[R_68K_NUM] = {
        /* R_68K_NONE */ 0,
        /* R_68K_32   */ 4,
        /* R_68K_16   */ 0,
        /* R_68K_8    */ 0,
        /* R_68K_PC32 */ 4,
        /* R_68K_PC16 */ 0,
        /* R_68K_PC8  */ 0,
        0
    };
    return SIZES;
}

Object::DebugInfo Object::collectDebugInfo(const Elf32_Shdr *sourceHeader, const Elf32_Sym *targetSymbol) const
{
    DebugInfo info;

    const auto *target = targetSymbol
        ? elf32_getshdr(elf_getscn(m_elf, targetSymbol->st_shndx))
        : nullptr;

    info.sourceName = sourceHeader ? m_shstrtab + sourceHeader->sh_name : "??";
    info.targetName = target ? m_shstrtab + target->sh_name : "??";
    if (targetSymbol)
    {
        info.symbolName = m_strtab + targetSymbol->st_name;
        info.symbolValue = targetSymbol->st_value;
    }
    else
    {
        info.symbolName = "??";
        info.symbolValue = -1;
    }

    return info;
}

uint16_t Object::getCodeID(Elf32_Section source) const
{
    uint16_t codeID = 0;
    auto name = elf32_getshdr(elf_getscn(m_elf, source))->sh_name;
    if (name)
        std::sscanf(m_shstrtab + name, ".code%05hu", &codeID);
    return codeID;
}

void Object::processRelocation(const SSec<Elf32_Rela> &relaSection)
{
    auto *rela = relaSection.data;
    const auto *end = rela + relaSection.size();

    auto sourceIndex = relaSection.header->sh_info;
    SSec<uint8_t> source { elf_getscn(m_elf, sourceIndex) };

    // Needing to handle relocations inside .bss would make generating the data
    // resource harder for Palm OS since the jump table gets tacked onto the end
    // of the data section for the compressor. It seems highly unlikely that
    // .bss would ever have relocations since there would be no way to store an
    // addend, so just throw an exception if it ever happens since it is
    // probably a bug.
    if (m_bss && m_bss.section == source.section)
        throw std::runtime_error("Unexpected relocations in .bss");

    auto base = source.header->sh_addr;
    auto max = source.size();
    auto codeID = getCodeID(sourceIndex);

    for (; rela != end; ++rela)
    {
        auto type = ELF32_R_TYPE(rela->r_info);
        if (type >= R_68K_NUM)
            throw new std::runtime_error("out of range r_type " + std::to_string(type));

        auto relaSize = RelaSizes()[type];
        if (relaSize == 0)
        {
            if (m_verbose)
                std::cerr << "Unsupported relocation type " << type << std::endl;
            continue;
        }

        auto maxOffset = max - relaSize;
        if (rela->r_offset < base || rela->r_offset > maxOffset)
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
                    << std::endl;
            }

            continue;
        }

        const auto *targetSymbol = m_symtab[ELF32_R_SYM(rela->r_info)];
        Elf32_Section targetSection = targetSymbol ? targetSymbol->st_shndx : SHN_UNDEF;

        switch (getXrefKind(codeID, sourceIndex, rela, targetSymbol))
        {
            case XrefKind::InvalidEhFrame:
                // Case 1: References from .eh_frame, with the exception of
                // __gcc_personality_v0. Should be direct references within the
                // code segment.
                if (m_verbose)
                {
                    auto info = collectDebugInfo(source.header, targetSymbol);
                    std::cerr
                        << "Clearing cross-segment reference from .eh_frame:\n"
                        << info.symbolName
                        << " (" << info.sourceName << " -> " << info.targetName << ")\n"
                        << std::hex
                        << "0x" << int(targetSymbol->st_info)
                        << " 0x" << int(targetSymbol->st_other)
                        << std::dec
                        << std::endl;
                }

                setU32BE(source, rela->r_offset, 0);
            break;
            case XrefKind::Indirect:
            {
                // Case 2: References to code that can go through the jump table.
                auto targetAddr = targetSymbol->st_value + rela->r_addend;
                auto &targetJumpTable = m_jumpTables[targetSection];
                // It will be necessary to do a second pass to insert the
                // correct addend to the source section since it can only be
                // calculated after all of the target xrefs are known, since the
                // jump table for each target section must be contiguous.
                targetJumpTable[targetAddr].push_back({ sourceIndex, rela->r_offset });
            }
            break;
            case XrefKind::Direct:
            {
                // Case 3: Direct code and data refs. These are either refs
                // to the same section or non-PCREL refs to other sections that
                // are always loaded (data and code 1).

                RelocBase relocBase;
                if (targetSection == m_data.index())
                    relocBase = RelocData;
                else if (targetSection == m_bss.index())
                    relocBase = RelocBss;
                else if (targetSection == sourceIndex)
                    relocBase = RelocCode;
                else if (targetSection == m_code.front().index())
                    relocBase = RelocCode1;
                else
                {
                    auto info = collectDebugInfo(source.header, targetSymbol);
                    throw std::runtime_error(
                        "Impossible relocation between "s
                        + info.sourceName + " and "
                        + info.targetName + "(" + info.symbolName + ")");
                }

                auto targetAddr = targetSymbol->st_value + rela->r_addend;

                setU32BE(source, rela->r_offset, targetAddr);
                auto &table = m_relocations[sourceIndex][relocBase];
                assert(table.empty() || table.back() < rela->r_offset);
                table.push_back(rela->r_offset);
            }
            break;
            case XrefKind::Invalid:
                // Case 4: Code references that don't go through the jump table
                // must remain in the current segment.
                if (m_verbose)
                {
                    auto info = collectDebugInfo(source.header, targetSymbol);
                    std::cerr
                        << std::hex
                        << "Invalid ref type " << type << " from " << info.sourceName
                        << ":0x" << rela->r_offset
                        << " to " << info.targetName << "(" << info.symbolName << ")"
                        << "+0x" << info.symbolValue
                        << " (addend 0x" << rela->r_addend << ")"
                        << std::dec
                        << std::endl;
                }
            break;
        }
    }
}

std::pair<size_t, std::string> Object::processJumpTables(int32_t a5JTOffset)
{
    auto jtIndex = m_jtFirstIndex;
    a5JTOffset += m_jtHeaderSize + jtIndex * m_jtEntrySize;

    std::ostringstream jumpTable;
    for (const auto &[targetSection, sectionTable] : m_jumpTables)
    {
        SSec<uint8_t> target { elf_getscn(m_elf, targetSection) };
        auto codeID = getCodeID(targetSection);
        if (codeID == 0)
            throw std::runtime_error("Cannot create jump table to non-code section");
        else if (codeID == 1)
            // The linker script already populates the code 1 header with the
            // correct values, which are always offset 0, length 1, because
            // there is only one near jump table entry, and then Retro68 does
            // its own relocations. If code 1 was given a far model segment
            // header instead and used the standard Mac OS relocation format so
            // the OS would do the relocating, then this would need to be
            // populated correctly. Palm OS always does its own thing; just like
            // how its code 0 resource is mostly bogus, the code 1 resource
            // header is also bogus and gets ignored by the Palm OS loader.
            ;
        else if (isPalm())
        {
            // Since Retro68 handles extra section relocations itself, this
            // header could be made smaller, but is kept in the same form that
            // CodeWarrior for Palm OS used, for the sake of debugging tools
            // that already understand this format, like IDA.
            word(target.data, a5JTOffset);
            word(target.data + 2, sectionTable.size());
            longword(target.data + 4, a5JTOffset);
            longword(target.data + 8, target.size());
        }
        else
        {
            word(target.data, 0xffff);
            longword(target.data + 12, jtIndex);
            longword(target.data + 16, sectionTable.size());
        }

        for (const auto &[targetAddr, sourceAddrs] : sectionTable)
        {
            // If the jump table entry is >32k away from a5 and the target
            // processor is not m68020+ then there is no way to do the jump
            // without emitting extra code. This is unlikely enough that it is
            // not supported right now.
            if (a5JTOffset < INT16_MIN || a5JTOffset > INT16_MAX)
            {
                std::cerr
                    << std::hex
                    << "Jump table entry $" << a5JTOffset << "(a5)"
                    << " to target "
                    << m_shstrtab + target.header->sh_name
                    << ":0x" << targetAddr
                    << " displacement is too large"
                    << std::dec
                    << std::endl;
            }

            for (const auto &[sourceSection, offset] : sourceAddrs)
            {
                SSec<uint8_t> source { elf_getscn(m_elf, sourceSection) };

                enum {
                    // kPEAOp = 0b0'100'100'001'000000,
                    // kLEAOp = 0b0'100'000'111'000000,
                    // kLEAMask = 0b0'111'000'111'000000,
                    kJSROp = 0b0'100'111'010'000000,
                    kJMPOp = 0b0'100'111'011'000000,
                    kEffAddrMask = 0b111111,
                    kA5Reg       = 5,
                    kD16Mode     = 0b101 << 3
                };

                auto op = getU16BE(source, offset - 2);
                op &= ~kEffAddrMask;
                // TODO: Not sure what kinds of other operators might be trying
                // to displace something that ends up as a xref, but if there
                // are any, they will need to be handled differently. The most
                // likely would be PEA/LEA, which would be a problem for
                // function pointer comparison. Inside Macintosh says that the
                // operator should always be JSR and also that says its linker
                // operates thus:
                //
                // "If you compile and link units with any option that specifies the far model for
                //  code, any JSR instruction that references a jump-table entry is generated with
                //  a 32-bit absolute address. The address of any instruction that makes such a
                //  reference is recorded in compressed form in the A5 relocation information area.
                //  The modified _LoadSeg trap adds the value of A5 to the address fields of the JSR
                //  instruction at load time."
                assert(op == kJSROp || op == kJMPOp);
                op |= kD16Mode | kA5Reg;

                setU16BE(source, offset - 2, op);
                setU16BE(source, offset, a5JTOffset);
                setU16BE(source, offset + 2, kNoOp);
            }

#ifdef PALMOS
            if (isPalm())
            {
                enum { kJmpAbsL = 0x4ef9 };
                word(jumpTable, kJmpAbsL);
                longword(jumpTable, targetAddr);
            }
            else
#endif
            {
                enum { kLoadSeg = 0xa9f0 };
                word(jumpTable, codeID);
                word(jumpTable, kLoadSeg);
                longword(jumpTable, targetAddr);
            }

            a5JTOffset += m_jtEntrySize;
            ++jtIndex;
        }
    }

    return { jtIndex, jumpTable.str() };
}

std::pair<size_t, size_t> Object::emitRes0(Resources &rsrc)
{
    auto belowA5 = m_data.size() + m_bss.size();

    // Because of how the data section compression works on Palm OS, the jump
    // table gets inserted between .data and .bss so actually exists somewhere
    // below A5. This is OK because each section header contains the offset of
    // its own jump table relative to A5, the addends for the jump table are
    // calculated in `processJumpTables` and do not need any runtime relocation,
    // and the addends to `.bss` are… oops.
    // TODO: Probably it is less fine for the addends to BSS which need to be
    // rebased?! At least this can be done easily by walking the relocation
    // chain and touching up all of the existing addends.
    auto jtAddress = isPalm() ? m_data.size() : belowA5;

    auto [jtNumEntries, jumpTable] = processJumpTables(jtAddress);
    auto jtSize = jtNumEntries * m_jtEntrySize;
    auto aboveA5 = m_jtHeaderSize + jtSize;

    std::ostringstream code0;

    longword(code0, aboveA5);
    longword(code0, belowA5);

    // The rest of code 0 is ignored by Palm OS, but keep it for compatibility
    // with disassemblers, utilities, etc.
    longword(code0, isPalm() ? 8 : jtSize);
    longword(code0, isPalm() ? 0x20 : m_jtHeaderSize);

    // Jump table entry for default entrypoint. Palm OS ignores this and always
    // jumps directly to the start of code 1.
    code0 << fromhex(
        "0000" // function offset
        "3F3C" // move.w #resID,-(sp)
        "0001" // resID
        "A9F0" // _LoadSeg
    );

    if (!isPalm())
    {
        // Flag entry to switch to “new format” 32-bit jump table entries.
        code0 << fromhex("0000 FFFF 0000 0000");
        code0 << jumpTable;
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

#ifdef PALMOS
    if (isPalm())
    {
        std::ostringstream data0;
        {
            std::ostringstream combined;
            combined
                << m_data.view().data()
                << jumpTable;
            data0 << CompressPalmData(combined.str());
        }

        data0 << SerializeRelocsPalm(m_relocations[m_data.index()], false,
            -belowA5, aboveA5);
        rsrc.addResource(Resource(m_dataOsType, 0, data0.str()));
    }
    else
#endif
    {
        rsrc.addResource(Resource(m_dataOsType, 0, m_data.view().data()));
        rsrc.addResource(Resource("RELA", 0, SerializeRelocs(m_relocations[m_data.index()])));
    }

    return { belowA5, aboveA5 };
}

void Object::MultiSegmentApp(const std::string &filename, const SegmentMap &segmentMap)
{
    ResourceFile file;
    Resources& rsrc = file.resources;

    auto [ belowA5, aboveA5 ] = emitRes0(rsrc);

    for (auto &section : m_code)
    {
        uint16_t codeID = getCodeID(section.index());
        size_t size = section.size();

        if (isPalm() && codeID != 1)
        {
            std::string code;
            {
                std::ostringstream combined;
                combined << section.view();
                combined << SerializeRelocsPalm(m_relocations[section.index()], true,
                    -belowA5, aboveA5);
                code = combined.str();
            }

            if (m_verbose)
                size = code.size();

            rsrc.addResource(Resource(m_codeOsType, codeID, std::move(code)));
        }
        else
            rsrc.addResource(Resource(m_codeOsType, codeID,
                section.view().data(),
                std::string(segmentMap.GetSegmentName(codeID))));

        if (!isPalm())
            rsrc.addResource(Resource(ResType("RELA"), codeID,
                SerializeRelocs(m_relocations[section.index()])));

        if(m_verbose)
            std::cout << m_codeOsType << " " << codeID << ": " << size << " bytes\n";
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
