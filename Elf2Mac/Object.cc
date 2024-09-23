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

Object::Object(const std::string &input, bool palmos,
    const char *creator, uint32_t stackSize, bool verbose)
    : m_codeOsType(palmos ? "code" : "CODE")
    , m_dataOsType(palmos ? "data" : "DATA")
    , m_applOsType(palmos ? "appl" : "APPL")
    , m_creator(creator)
    // Palm OS does not have a jump table header, but it does need 4 bytes at
    // A5 for the OS to put a pointer to SysAppInfoType
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
            {
                const auto *target = elf32_getshdr(elf_getscn(m_elf, shdr.sh_info));
                if (!target)
                    throw std::runtime_error("Relocation section "s + name
                        + " points to non-existing target "
                        + std::to_string(shdr.sh_info));

                // Non-alloc sections do not make it to the output, so do not
                // need to be relocated
                if (target->sh_flags & SHF_ALLOC)
                    m_rela.emplace_back(scn);
            }
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

                    // All code sections have a header that needs to be filled
                    // with jump table information even if there are no
                    // relocations to the section
                    m_jumpTables[elf_ndxscn(scn)] = {};
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
                    << " (" << m_shstrtab + shdr.sh_name << ")\n";
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
        {
            // These are normally offsets subtracted from %a5 but in this case
            // they are positive offsets since they are not actually being used
            // through A5. TODO: Treating belowA5 as a positive value is
            // probably unnecessarily confusing. TODO: Combining data like this
            // makes no sense.
            auto dataBelowA5 = -m_code.front().size();
            auto bssBelowA5 = -m_code.front().size() - m_data.size();
            out << SerializeRelocsPalm(combined, false, dataBelowA5, bssBelowA5).first;
        }
        else
#endif
            out << SerializeRelocs(combined);
    }
    else
    {
        const auto &relocs = m_relocations[m_code.front().index()];
#ifdef PALMOS
        if (isPalm())
            // There is no dataBelowA5/bssBelowA5 here because there should be
            // no data to relocate
            out << SerializeRelocsPalm(relocs, false, 0, 0).first;
        else
#endif
            out << SerializeRelocs(relocs);
    }
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

bool Object::isOffsetInEhFrame(uint16_t codeID, Elf32_Addr vaddr,
    const Elf32_Sym *target) const
{
    if (codeID == 0)
        return false;

    if (target && std::strcmp(m_strtab + target->st_name, "__gxx_personality_v0") == 0)
        return false;

    auto ehStart = findExceptionInfoStart(codeID);

    if (!ehStart || vaddr < ehStart->st_value)
        return false;

    return true;
}

Object::XrefKind Object::getXrefKind(uint16_t codeID, Elf32_Section source,
    const Elf32_Rela *rela, const Elf32_Sym *target) const
{
    if (!target)
        return XrefKind::Invalid;

    auto targetSection = target->st_shndx;

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
    if (targetSection != source && isOffsetInEhFrame(codeID, rela->r_offset, target))
        return XrefKind::InvalidEhFrame;

    // Intra-section, data, and code 1 xrefs are always valid since the only
    // limit on xrefs is whether or not the target section is actually loaded
    // (and whether the operand is wide enough to store the addend, which should
    // be ensured by the compiler).
    if (targetSection == source
        || targetSection == m_data.index()
        || targetSection == m_bss.index()
        || targetSection == m_code.front().index())
        return XrefKind::Direct;

    // Inter-section code xrefs are always OK because those can be pointed to
    // the jump table which will call _LoadSeg if needed. Inter-section
    // PC-relative relocations will be converted to absolute relocations by
    // `buildJumpTable`.
    if (ELF32_ST_TYPE(target->st_info) == STT_FUNC)
        return XrefKind::Indirect;

    // Sometimes, references to functions are given as a section + addend
    // instead of referring directly to a symbol. If necessary, it would be
    // possible to look up whether there is a function symbol at the final
    // offset, but for now just assume if it is targeting a code section that
    // it is referring to code. Because of the resource headers, if something
    // is referring to a section with no addend, it is definitely not referring
    // to code.
    if (ELF32_ST_TYPE(target->st_info) == STT_SECTION && rela->r_addend != 0
        && targetSection != m_data.index() && targetSection != m_bss.index())
        return XrefKind::Indirect;

    // References to a weak symbol that does not exist can just be ignored. This
    // can happen with e.g. __cxa_pure_virtual.
    if (ELF32_ST_BIND(target->st_info) == STB_WEAK
        && target->st_value == 0 && rela->r_addend == 0)
        return XrefKind::Weak;

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

    auto codeID = getCodeID(sourceIndex);
    for (; rela != end; ++rela)
    {
        auto type = ELF32_R_TYPE(rela->r_info);
        if (type >= R_68K_NUM)
            throw new std::runtime_error("out of range r_type " + std::to_string(type));

        const auto *targetSymbol = m_symtab[ELF32_R_SYM(rela->r_info)];
        Elf32_Section targetSection = targetSymbol ? targetSymbol->st_shndx : SHN_UNDEF;

        // A relocation with an odd address suggests that there is an alignment
        // issue somewhere that needs to be fixed, since this would cause a bus
        // error.
        if (rela->r_offset & 1)
        {
            auto info = collectDebugInfo(source.header, targetSymbol);
            std::cerr
                << std::hex
                << "Unaligned ref type " << type
                << " at " << info.sourceName
                << "+0x" << rela->r_offset
                << " to " << info.targetName << "(" << info.symbolName << ")"
                << "+0x" << info.symbolValue
                << " (addend 0x" << rela->r_addend << ","
                << std::dec
                << " type "
                << (targetSymbol ? ELF32_ST_TYPE(targetSymbol->st_info) : 0)
                << ")"
                << std::endl;
            continue;
        }

        // TODO: Rewrite pcrel type 5 going to data/bss sections to be
        // a5-relative (this happens when building without -msep-data)
        auto relaSize = RelaSizes()[type];
        if (relaSize == 0 && targetSection != sourceIndex)
        {
            if (m_verbose)
            {
                auto info = collectDebugInfo(source.header, targetSymbol);
                std::cerr
                    << std::hex
                    << "Unsupported ref type " << type << " from " << info.sourceName
                    << "+0x" << rela->r_offset
                    << " to " << info.targetName << "(" << info.symbolName << ")"
                    << "+0x" << info.symbolValue
                    << " (addend 0x" << rela->r_addend << ","
                    << std::dec
                    << " type "
                    << (targetSymbol ? ELF32_ST_TYPE(targetSymbol->st_info) : 0)
                    << ")"
                    << std::endl;
            }
            continue;
        }

        if (!source.inRange(rela->r_offset, relaSize))
        {
            if (m_verbose)
            {
                // FIXME: There are sometimes relocations beyond the end of the sections
                //        in LD output for some reason. That's bad. Let's ignore it.
                auto base = source.header->sh_addr;
                auto max = base + source.size();
                auto maxOffset = max - relaSize;
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
                        << " (" << info.sourceName << " -> " << info.targetName << ") ["
                        << std::hex
                        << "info 0x" << int(targetSymbol->st_info)
                        << " other 0x" << int(targetSymbol->st_other)
                        << std::dec
                        << "]"
                        << std::endl;
                }

                source.setU32(rela->r_offset, 0);
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

                if (m_verbose)
                {
                    auto info = collectDebugInfo(source.header, targetSymbol);
                    std::cerr
                        << std::hex
                        << "Creating jump table entry from " << info.sourceName
                        << "+0x" << rela->r_offset
                        << " to " << info.targetName << "(" << info.symbolName << ")"
                        << "+0x" << info.symbolValue
                        << " (addend 0x" << rela->r_addend << ")"
                        << std::dec
                        << std::endl;
                }
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
                if (type == R_68K_PC32)
                    targetAddr -= rela->r_offset;

                source.setU32(rela->r_offset, targetAddr);
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
                        << "+0x" << rela->r_offset
                        << " to " << info.targetName << "(" << info.symbolName << ")"
                        << "+0x" << info.symbolValue
                        << " (addend 0x" << rela->r_addend << ","
                        << std::dec
                        << " type "
                        << (targetSymbol ? ELF32_ST_TYPE(targetSymbol->st_info) : 0)
                        << ")"
                        << std::endl;
                }
            break;
            case XrefKind::Weak:
                if (m_verbose)
                {
                    auto info = collectDebugInfo(source.header, targetSymbol);
                    std::cout
                        << std::hex
                        << "Ignoring weak symbol reference from "
                        << info.sourceName
                        << "+0x" << rela->r_offset
                        << " to " << info.targetName << "(" << info.symbolName << ")"
                        << "+0x" << info.symbolValue
                        << " (addend 0x" << rela->r_addend << ")\n"
                        << std::dec;
                }
            break;
        }
    }
}

std::pair<size_t, std::string> Object::processJumpTables()
{
    // From M68000 Family Programmer’s Reference Manual
    enum {
        // Effective address field
        kEAPC     = 0b0'111'010, /* (d16,%pc) */
        kEAImmL   = 0b0'111'001, /* (xxx).L */
        kEAToSP   = 0b0'010'111, /* (%sp) */
        kEAA5     = 0b0'101'000 | 5, /* (d16,%a5) */
        kEAMask   = 0b0'111'111,

        kOpAddiL     = 0b0'000'011'010'000'000,
        kOpAddiL_SP  = kOpAddiL | kEAToSP,
        kOpBraL      = 0b0'110'000'011'111'111,
        kOpBsrL      = 0b0'110'000'111'111'111,
        kBccDispMask = 0b0'000'000'011'111'111,
        kOpJmp       = 0b0'100'111'011'000'000,
        kOpJmpA5     = kOpJmp | kEAA5,
        kOpJmpI32    = kOpJmp | kEAImmL,
        kOpJsr       = 0b0'100'111'010'000'000,
        kOpJsrA5     = kOpJmp | kEAA5,
        kOpJsrI32    = kOpJmp | kEAImmL,
        kOpLea       = 0b0'100'000'111'000'000,
        kOpLeaPC16   = kOpLea | kEAPC,
        kLeaRegMask  = 0b0'000'111'000'000'000,
        kOpPea       = 0b0'100'100'001'000'000,
        kOpPeaPC16   = kOpPea | kEAPC,
        kOpRts       = 0b0'100'111'001'110'101,
    };

    auto jtIndex = m_jtFirstIndex;
    // Use signed size since it is possible in the future that jump table might
    // end up being offset negatively if someone needs to support making it
    // bigger than the 32k limit and it is just easier to have the correct
    // checks in place already (the compiler would complain if the type were
    // unsigned).
    ssize_t a5JTOffset = m_jtHeaderSize + jtIndex * m_jtEntrySize;

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
            // processor is not 68020+ then there is no way to do the jump
            // without emitting extra code. This is unlikely enough that it is
            // not supported right now. To support large displacements, use
            // `jsr.l (bd,%a5)` for 68020, `jsr.l (xxx).l` + relocation for
            // 68000 in code 2+, and `move.l %a5,-(%sp); addi.l d32,(%sp); rts`
            // for 68000 in code 1.
            if (a5JTOffset < INT16_MIN || a5JTOffset > INT16_MAX)
            {
                std::cerr
                    << std::hex
                    << "Jump table entry $" << a5JTOffset << "(a5)"
                    << " to target "
                    << m_shstrtab + target.header->sh_name
                    << "+0x" << targetAddr
                    << " displacement is too large"
                    << std::dec
                    << std::endl;
            }

            for (const auto &[sourceSection, offset] : sourceAddrs)
            {
                SSec<uint8_t> source { elf_getscn(m_elf, sourceSection) };

                // There are two potential ways to rewrite jump table
                // relocations.
                //
                // The first way is to jump to (d16,%a5). This method must be
                // used for code 1 on Palm OS because the OS does no relocation
                // on this section. Jumps >±32k from %a5 need (bd,%a5) or
                // pea,addi.l.
                //
                // The second way is to relocate offsets directly into the data
                // section. This is what Apple’s documentation says their linker
                // did for far model code:
                //
                // “If you compile and link units with any option that specifies
                //  the far model for code, any JSR instruction that references
                //  a jump-table entry is generated with a 32-bit absolute
                //  address. The address of any instruction that makes such a
                //  reference is recorded in compressed form in the A5
                //  relocation information area. The modified _LoadSeg trap adds
                //  the value of A5 to the address fields of the JSR instruction
                //  at load time.” - Mac OS Runtime Architectures
                //
                // The correct choice is the fast choice, though currently that
                // is not always what happens, because changing the code size
                // would require adjusting all symbol and relocation offsets for
                // the section, which is harder than inserting no-ops.
                //
                // Depending on target CPU the relocation may be in different
                // instructions:
                //
                // 68020+           | Replacement   | 68000/010        | Replacement
                // -----------------|---------------|------------------|------------
                // bra.l d32        | jmp (d16,%a5) | pea (4,%pc)      | jmp (d16,%a5)
                //                  |               | addi.l d32,(%sp) | nop nop nop
                //                  |               | rts              | nop
                // bsr.l d32        | jsr (d16,%a5) | pea (14,%pc)     | pea (14,%pc)  ; 16+20 cycles
                //                  |               | pea (4,%pc)      | jmp (d16,%a5) ; jsr+nop would be
                //                  |               | addi.l d32,(%sp) | nop nop nop   ; 18+24 cycles
                //                  |               | rts              | nop
                // jmp.l i32        | jmp (d16,%a5) | same             | same
                // jsr.l i32        | jsr (d16,%a5) | same             | same
                // lea (d32,%pc),%% | n/a (error)   | lea (4,%pc),%%   | n/a (error)
                //                  | n/a "         | addi.l d32,%%    | n/a "
                // lea (i32).l,%%   | n/a "         | same             | n/a "
                // pea (d32,%pc)    | n/a "         | pea (4,%pc)      | n/a "
                //                  | n/a "         | addi.l d32,(%sp) | n/a "
                // pea (i32).l      | n/a "         | same             | n/a "

                auto op = source.getU16(offset - 2, 0);

                auto is68000Emu = op == kOpAddiL_SP
                    // operand of pea (4,%pc) or lea (4,%pc),%an
                    && source.getU16(offset - 4, 0) == 4;

                if (is68000Emu
                    && source.getU16(offset + 4, 0) == kOpRts
                    && source.getU16(offset - 6, 0) == kOpPeaPC16)
                {
                    // Emulated bra.l or bsr.l.
                    source.setU16(offset - 6, kOpJmp | kEAA5);
                    source.setU16(offset - 4, a5JTOffset);
                    source.setU16(offset - 2, kNoOp);
                    source.setU16(offset, kNoOp);
                    source.setU16(offset + 2, kNoOp);
                    source.setU16(offset + 4, kNoOp);
                }
                else if (is68000Emu && (source.getU16(offset - 6, 0) == kOpPeaPC16
                    || (source.getU16(offset - 6, 0) & ~kLeaRegMask) == kOpLeaPC16))
                {
                    // Emulated pea/lea of a JT address. This is an error
                    // because the JT entry is not a real function, and the
                    // address of the real function cannot be known from the
                    // source section.
                    // If this is a problem for someone it may be possible
                    // to find all pea/lea of a function referenced through
                    // JT, including intra-segment references that would
                    // normally not require going through JT, and point them
                    // all at the JT, but this seems like an edge case that
                    // does not need support.
                    std::cerr
                        << std::hex
                        << "Jump table entry $" << a5JTOffset << "(a5)"
                        << " to target "
                        << m_shstrtab + target.header->sh_name
                        << "+0x" << targetAddr
                        << " cannot take address of jump table function"
                        << std::dec
                        << std::endl;
                }
                else if (op == kOpBraL || op == kOpJmpI32)
                {
                    source.setU16(offset - 2, kOpJmpA5);
                    source.setU16(offset, a5JTOffset);
                    source.setU16(offset + 2, kNoOp);
                }
                else if (op == kOpBsrL || op == kOpJsrI32)
                {
                    source.setU16(offset - 2, kOpJsrA5);
                    source.setU16(offset, a5JTOffset);
                    source.setU16(offset + 2, kNoOp);
                }
                else if (sourceSection == m_data.index()
                    || isOffsetInEhFrame(getCodeID(sourceSection), offset, nullptr))
                {
                    // Assume this is a vtable or similar, rewrite the offset to
                    // point to the corresponding jump table entry, and give it
                    // a relocation
                    source.setU32(offset, a5JTOffset);
                    m_relocations[sourceSection][RelocData].push_back(offset);
                }
                else
                {
                    std::cerr
                        << std::hex
                        << "Jump table entry $" << a5JTOffset << "(a5)"
                        << " to target "
                        << m_shstrtab + target.header->sh_name
                        << "+0x" << targetAddr
                        << " unknown source operator 0x" << op
                        << std::dec
                        << std::endl;
                }
            }

#ifdef PALMOS
            if (isPalm())
            {
                word(jumpTable, kOpJmpI32);
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
    auto [jtNumEntries, jumpTable] = processJumpTables();
    auto jtSize = jtNumEntries * m_jtEntrySize;
    auto aboveA5 = m_jtHeaderSize + jtSize;
    auto dataBelowA5 = belowA5 - (m_data ? m_data.header->sh_addr : 0);
    auto bssBelowA5 = belowA5 - (m_bss ? m_bss.header->sh_addr : 0);

    std::ostringstream code0;

    longword(code0, aboveA5);
    longword(code0, belowA5);

    // The rest of code 0 is ignored by Palm OS, but keep it for compatibility
    // with disassemblers, utilities, etc.
    longword(code0, isPalm() ? 8 : jtSize);
    longword(code0, isPalm() ? 0x20 : m_jtHeaderSize);

    // Jump table entry for default entrypoint on Mac OS. Palm OS ignores this
    // and always jumps directly to the start of code 1.
    code0 << fromhex(
        "0000" // function offset
        "3F3C" // move.w #resID,-(sp)
        "0001" // resID
        "A9F0" // _LoadSeg
    );

    if (!isPalm())
    {
        // This flag entry switches the Mac OS segment manager to expect “new
        // format” 32-bit jump table entries from here. It is not present in
        // Palm OS code 0 resources.
        code0 << fromhex("0000 FFFF 0000 0000");
        code0 << jumpTable;
    }

    if(m_verbose)
    {
        std::cout
            << m_codeOsType << " 0: " << code0.str().size() << " bytes\n"
            << "above A5: " << aboveA5 << " bytes\n"
            << "below A5: " << belowA5 << " bytes\n";

        if (m_data)
            std::cout
                << ".data: " << m_data.size() << " bytes at A5-0x"
                << std::hex << dataBelowA5 << std::dec << "\n";

        if (m_bss)
            std::cout
                << ".bss: " << m_bss.size() << " bytes at A5-0x"
                << std::hex << bssBelowA5 << std::dec << "\n";
    }

    rsrc.addResource(Resource(m_codeOsType, 0, code0.str()));

#ifdef PALMOS
    if (isPalm())
    {
        std::string data0;

        // Decompression starts from offset 4. This field is supposed to contain
        // the offset of the code 1 relocation table in the data resource, so
        // cannot be populated until the size of the compressed data *and* the
        // data section’s A5 relocation table size is known.
        data0.append(4, '\0');

        {
            std::string combined(m_data.view());

            // This space is reserved for use by Palm OS.
            combined.append(m_jtHeaderSize, '\0');

            combined += jumpTable;
            data0 += CompressPalmData(combined, dataBelowA5);

            if (m_verbose)
            {
                auto inSize = combined.size();
                auto outSize = data0.size();
                std::cout << "Compressed "
                    << inSize << " bytes to "
                    << outSize << " bytes "
                    << "(" << 100.0 * outSize / inSize << "%)\n";
            }
        }

        auto [relocs, dataRelocsSize] = SerializeRelocsPalm(
            m_relocations[m_data.index()], false, dataBelowA5, bssBelowA5);

        longword(reinterpret_cast<uint8_t *>(data0.data()),
            data0.size() + dataRelocsSize);

        data0 += std::move(relocs);

        rsrc.addResource(Resource(m_dataOsType, 0, std::move(data0)));
    }
    else
#endif
    {
        rsrc.addResource(Resource(m_dataOsType, 0, std::string(m_data.view())));
        rsrc.addResource(Resource("RELA", 0, SerializeRelocs(m_relocations[m_data.index()])));
    }

    return { dataBelowA5, bssBelowA5 };
}

void Object::MultiSegmentApp(const std::string &filename, const SegmentMap &segmentMap)
{
    ResourceFile file;
    Resources& rsrc = file.resources;

    auto [ dataBelowA5, bssBelowA5 ] = emitRes0(rsrc);

    for (auto &section : m_code)
    {
        uint16_t codeID = getCodeID(section.index());
        size_t size = section.size();

        if (codeID != 1 && m_jumpTables[section.index()].empty())
        {
            if (m_verbose)
            {
                std::cout
                    << m_codeOsType << " " << codeID
                    << " is never referenced; skipping\n";
            }
            continue;
        }
        else if (codeID != 1 && isPalm())
        {
            std::string code(section.view());
            code += SerializeRelocsPalm(m_relocations[section.index()], true,
                dataBelowA5, bssBelowA5).first;

            if (m_verbose)
                size = code.size();

            rsrc.addResource(Resource(m_codeOsType, codeID, std::move(code)));
        }
        else
            rsrc.addResource(Resource(m_codeOsType, codeID,
                std::string(section.view()),
                std::string(segmentMap.GetSegmentName(codeID))));

        if (!isPalm())
            rsrc.addResource(Resource(ResType("RELA"), codeID,
                SerializeRelocs(m_relocations[section.index()])));

        if(m_verbose)
        {
            auto ehStart = findExceptionInfoStart(codeID);

            if (ehStart)
            {
                auto header = elf32_getshdr(elf_getscn(m_elf, ehStart->st_shndx));
                auto codeSize = header->sh_size;
                auto exceptionSize = header->sh_addr + codeSize - ehStart->st_value;
                auto percent = 100.0 * exceptionSize / codeSize;

                std::cout
                    << m_codeOsType << " " << codeID
                    << " has " << exceptionSize
                    << " bytes of exception info (" << percent << "%)\n";
            }
            else
            {
                std::cerr
                    << "Exception info marker not found for "
                    << m_codeOsType << " " << codeID
                    << std::endl;
            }

            std::cout << m_codeOsType << " " << codeID << ": " << size << " bytes\n";
        }
    }

    if (isPalm())
        emitPref(rsrc);

    finalizeFile(filename, file);
}

void Object::finalizeFile(const std::string &filename, ResourceFile &file)
{
#ifdef PALMOS
    file.name = filename;
#endif
    file.creator = m_creator;
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
