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
#include <unordered_set>
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
                    m_code.emplace_back(scn);

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

    // The output sections in the linker script have to be sorted according to
    // input match order because that is how GNU ld works, but the final output
    // should be sorted by resource IDs which are given in the output section
    // name
    std::sort(m_code.begin(), m_code.end(),
        [&](const SSec<uint8_t> &a, const SSec<uint8_t> &b) {
            const char *aName = m_shstrtab + a.header->sh_name;
            const char *bName = m_shstrtab + b.header->sh_name;
            return std::strcmp(aName, bName) < 0;
    });

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
            SerializeRelocsPalm(out, combined, false);
        else
#endif
            out << SerializeRelocs(combined);
    }
    else
    {
        const auto &relocs = m_relocations[m_code.front().index()];
#ifdef PALMOS
        if (isPalm())
            SerializeRelocsPalm(out, relocs, false);
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

static const uint8_t *RelaSizes() {
    static const uint8_t SIZES[R_68K_NUM] = {
        /* R_68K_NONE */ 0,
        /* R_68K_32   */ 4,
        /* R_68K_16   */ 0,
        /* R_68K_8    */ 0,
        /* R_68K_PC32 */ 4,
        /* R_68K_PC16 */ 2,
        /* R_68K_PC8  */ 0,
        0
    };
    return SIZES;
}

// From M68000 Family Programmer’s Reference Manual
enum {
    // Effective address field
    kEAPC        = 0b0'111'010,     // (d16,%pc)
    kEAImmL      = 0b0'111'001,     // (xxx).L
    kEAToSP      = 0b0'010'111,     // (%sp)
    kEAA5        = 0b0'101'000 | 5, // (d16,%a5)
    kEAPCIndex   = 0b0'111'011,     // (bd,%pc,Xn)
    kEAMask      = 0b0'111'111,

    // Branch displacement field
    kBcc16       = 0,
    kBcc32       = 0xff,

    // Extension word for 68020+ 32-bit PC-relative instructions.
    // See Figure 2-2 in the reference manual.
    kExtPC32     = 0b0'1'0'1'11'0'000,

    kOpAddiL     = 0b0'000'011'010'000'000,
    kOpAddiL_SP  = kOpAddiL | kEAToSP,

    kOpBra       = 0b0'110'0000'00'000'000,
    kOpBraL      = kOpBra | kBcc32,
    kOpBraW      = kOpBra | kBcc16,

    kOpBsr       = 0b0'110'0001'00'000'000,
    kOpBsrL      = kOpBsr | kBcc32,
    kOpBsrW      = kOpBsr | kBcc16,

    kOpJmp       = 0b0'100'111'011'000'000,
    kOpJmpA5     = kOpJmp | kEAA5,
    kOpJmpI32    = kOpJmp | kEAImmL,

    kOpJsr       = 0b0'100'111'010'000'000,
    kOpJsrA5     = kOpJsr | kEAA5,
    kOpJsrI32    = kOpJsr | kEAImmL,

    kOpLea       = 0b0'100'000'111'000'000,
    kOpLeaA5     = kOpLea | kEAA5,
    kOpLeaPC16   = kOpLea | kEAPC,
    kOpLeaPC32   = kOpLea | kEAPCIndex,
    kOpLeaI32    = kOpLea | kEAImmL,
    kLeaRegMask  = 0b0'000'111'000'000'000,

    kOpPea       = 0b0'100'100'001'000'000,
    kOpPeaA5     = kOpPea | kEAA5,
    kOpPeaPC16   = kOpPea | kEAPC,
    kOpPeaPC32   = kOpPea | kEAPCIndex,
    kOpPeaI32    = kOpPea | kEAImmL,

    kOpRts       = 0b0'100'111'001'110'101
};

void Object::convertPCOpToDirectOp(SSec<uint8_t> &source, const Elf32_Rela *rela, const Elf32_Sym *targetSymbol) const
{
    auto op = source.getU16(rela->r_offset - 2, 0);
    if (ELF32_R_TYPE(rela->r_info) == R_68K_PC32
        && op == kExtPC32
        && (source.getU16(rela->r_offset - 4, 0) & kEAMask) == kEAPCIndex)
    {
        // pea (bd,%pc) -> pea #xxx
        // or
        // lea (bd,%pc),%An -> lea #xxx,%An

        op = source.getU16(rela->r_offset - 4, 0);
        // There is no longer an extension word for this instruction
        // after conversion, so shift the operator forward.
        source.setU16(rela->r_offset - 4, kNoOp);
        source.setU16(rela->r_offset - 2, (op & ~kEAMask) | kEAImmL);
    }
    else if (op == kOpBsrL)
        // bsr.l #xxx -> jsr.l #xxx
        source.setU16(rela->r_offset - 2, kOpJsrI32);
    else if (op == kOpBraL)
        // bra.l #xxx -> jmp.l #xxx
        source.setU16(rela->r_offset - 2, kOpJmpI32);
    else
    {
        std::ostringstream msg;
        msg << "Unknown PC-relative operator 0x" << std::hex << op;
        warnReloc(std::cerr, msg.str().c_str(), rela, source.header, targetSymbol);
    }
}

Object::XrefKind Object::getXrefKind(uint16_t codeID, const SSec<uint8_t> &source,
    const Elf32_Rela *rela, const Elf32_Sym *target) const
{
    if (!target || !rela)
        return XrefKind::Invalid;

    auto relaSize = RelaSizes()[ELF32_R_TYPE(rela->r_info)];
    if (relaSize == 0)
        return XrefKind::InvalidUnsupported;

    // A relocation with an odd address suggests that there is an alignment
    // issue somewhere that needs to be fixed, since this would normally
    // cause a bus error. The Palm OS relocation format also mandates word
    // alignment, so it is not possible to relocate an unaligned xref at all
    // there without using a custom extended relocation format.
    if (rela->r_offset & 1)
        return XrefKind::InvalidUnaligned;

    // In the past, GNU ld was reportedly pointing relocations beyond the end of
    // a section. This was probably a bug in old Elf2Mac VMA handling, but there
    // is no harm in checking just in case.
    if (!source.inRange(rela->r_offset, relaSize))
        return XrefKind::InvalidRange;

    // References to weak symbols that do not exist can just be ignored. This
    // can happen with e.g. __cxa_pure_virtual.
    if (ELF32_ST_BIND(target->st_info) == STB_WEAK
        && target->st_value == 0
        && rela->r_addend == 0)
        return XrefKind::Weak;

    auto targetIndex = target->st_shndx;
    auto sourceIndex = source.index();

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
    if (targetIndex != sourceIndex && isOffsetInEhFrame(codeID, rela->r_offset, target))
        return XrefKind::InvalidEhFrame;

    auto isPC16 = ELF32_R_TYPE(rela->r_info) == R_68K_PC16;
    auto isPC = isPC16 || ELF32_R_TYPE(rela->r_info) == R_68K_PC32;

    // Intra-section xrefs are always valid since the only limit on xrefs is
    // whether or not the target section is actually loaded, and a section
    // referencing itself is obviously loaded. PC-relative xrefs only need an
    // addend.
    if (targetIndex == sourceIndex)
        return isPC ? XrefKind::IntraPC : XrefKind::Direct;

    // Inter-section xrefs to data are always valid because the data section is
    // always loaded. A PC16 xref needs to be converted to an A5-relative xref
    // since there is not enough room to use a direct relocation.
    if (targetIndex == m_data.index() || targetIndex == m_bss.index())
        return isPC16 ? XrefKind::IndirectData : XrefKind::Direct;

    // Inter-section xrefs to code 1 are always valid for the same reason that
    // data xrefs are always valid. A PC16 xref needs to be converted to use
    // the jump table since there is not enough room to use a direct relocation.
    if (targetIndex == m_code.front().index())
        return isPC16 ? XrefKind::Indirect : XrefKind::Direct;

    // Other inter-section code xrefs must always go through the jump table
    // because the target section may not be loaded. The jump table will call
    // _LoadSeg first if needed.
    if (ELF32_ST_TYPE(target->st_info) == STT_FUNC)
        return XrefKind::Indirect;

    // The compiler sometimes gives references to functions as a section +
    // addend instead of referring directly to a symbol. Since xrefs to the data
    // section were already handled unconditionally earlier, assume that an
    // STT_SECTION xref is an inter-section code xref. As an extra sanity check,
    // verify the addend is not zero, since that would point to the code
    // resource header.
    if (ELF32_ST_TYPE(target->st_info) == STT_SECTION && rela->r_addend != 0)
        return XrefKind::Indirect;

    // The target section is not guaranteed to be loaded and there is no way to
    // pass through a jump table, so this xref is not possible. The only known
    // way this could happen is if some data did not make it to the data section
    // and is the target of an inter-section reference.
    return XrefKind::Invalid;
}

void Object::processRelocations()
{
    for (auto &rela : m_rela)
        processRelocation(rela);
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

void Object::warnReloc(std::ostream &out, const char *msg, const Elf32_Rela *rela,
    const Elf32_Shdr *sourceHeader, const Elf32_Sym *targetSymbol) const
{
    auto info = collectDebugInfo(sourceHeader, targetSymbol);
    out << msg
        << std::hex
        << " type " << ELF32_R_TYPE(rela->r_info)
        << " at " << info.sourceName
        << "+0x" << rela->r_offset
        << " to " << info.targetName << "(" << info.symbolName << ")"
        << "+0x" << info.symbolValue
        << " (addend 0x" << rela->r_addend
        << std::dec;

    if (targetSymbol)
    {
        out
            << ", type " << ELF32_ST_TYPE(targetSymbol->st_info)
            << ", bind " << ELF32_ST_BIND(targetSymbol->st_info)
            << ", vis " << ELF32_ST_VISIBILITY(targetSymbol->st_other);
    }

    out << ")" << std::endl;
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

    // Code ID is calculated here and passed around for efficiency, since it
    // does a string scan, so is a little slow
    auto codeID = getCodeID(sourceIndex);

    for (; rela != end; ++rela)
    {
        auto relaType = ELF32_R_TYPE(rela->r_info);
        if (relaType >= R_68K_NUM)
            throw new std::runtime_error("Out of range r_type " + std::to_string(relaType));
        auto relaSize = RelaSizes()[relaType];

        const auto *targetSymbol = m_symtab[ELF32_R_SYM(rela->r_info)];
        Elf32_Section targetSection = targetSymbol ? targetSymbol->st_shndx : SHN_UNDEF;

        switch (getXrefKind(codeID, source, rela, targetSymbol))
        {
            case XrefKind::IntraPC:
            {
                // Intra-section PC-relative code or data refs. This is the
                // simplest xref since it only needs a compile-time fixup.

                assert(relaType != R_68K_32);

                if (m_verbose)
                    warnReloc(std::cout, "Intra-PC ref", rela, source.header, targetSymbol);

                auto targetAddr = targetSymbol->st_value + rela->r_addend - rela->r_offset;
                if (relaType == R_68K_PC16)
                {
                    if (int(targetAddr) >= INT16_MIN && int(targetAddr) <= INT16_MAX)
                        source.setU16(rela->r_offset, targetAddr);
                    else
                        // This should never happen since it would mean the
                        // compiler emitted some garbage it knew was impossible
                        warnReloc(std::cerr, "Out-of-range intra-section PC16 ref",
                            rela, source.header, targetSymbol);
                }
                else
                    source.setU32(rela->r_offset, targetAddr);
            }
            break;
            case XrefKind::Direct:
            {
                // Direct code and data refs. These are refs to the same section
                // or refs to other sections that are always loaded (data and
                // code 1).

                assert(relaType != R_68K_PC16);

                if (m_verbose)
                    warnReloc(std::cout, "Direct ref", rela, source.header, targetSymbol);

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
                    // This should never happen
                    auto info = collectDebugInfo(source.header, targetSymbol);
                    throw std::runtime_error(
                        "Impossible Direct relocation between "s
                        + info.sourceName + " and "
                        + info.targetName + "(" + info.symbolName + ")");
                }

                // PC-relative inter-section references must be converted to
                // direct references since that is the only kind of runtime
                // relocation supported without a custom relocation format.
                if (relaType == R_68K_PC32)
                    convertPCOpToDirectOp(source, rela, targetSymbol);

                auto targetAddr = targetSymbol->st_value + rela->r_addend;
                source.setU32(rela->r_offset, targetAddr);

                auto &table = m_relocations[sourceIndex][relocBase];
                assert((table.empty() || table.back() < rela->r_offset)
                    && "Out-of-order relocation");
                table.push_back(rela->r_offset);
            }
            break;
            case XrefKind::Indirect:
            {
                // References to code that can go through the jump table.

                if (m_verbose)
                    warnReloc(std::cout, "Creating jump table entry", rela, source.header, targetSymbol);

                auto targetAddr = targetSymbol->st_value + rela->r_addend;
                auto &targetJumpTable = m_jumpTables[targetSection];
                // It is necessary to do a second pass to insert the correct
                // addend to the source section since it can only be calculated
                // after all of the target xrefs are known, since the jump table
                // for each target section must be contiguous. The second pass
                // will also correct the operator.
                targetJumpTable[targetAddr].push_back({ sourceIndex, rela->r_offset });

                // TODO: If the relocation is 32-bit and the source is not code
                // 1 (since it is near mode on all platforms and never gets any
                // runtime relocation on at least Palm OS), then it should be
                // possible to use a direct relocation, and this should be added
                // to the relocation table
            }
            break;
            case XrefKind::IndirectData:
            {
                // PC-relative references to data that can go through A5. This
                // should only happen when the compiler is not run with
                // `-msep-data`. Using `-msep-data` is probably better since the
                // compiler can (at least in theory) switch more easily to using
                // 32-bit displacement if it needs to to avoid out-of-range
                // data.

                assert(relaType == R_68K_PC16);

                if (m_verbose)
                    warnReloc(std::cout, "Indirect data ref", rela, source.header, targetSymbol);

                auto op = source.getU16(rela->r_offset - 2, 0);
                if ((op & kEAMask) != kEAPC)
                    throw std::runtime_error("PC-relative operator expected");

                // NOTE: This work will need to be deferred until after jump
                // tables are built if the jump tables are ever moved into
                // belowA5, since in that case we will not be able to
                // calculate the size of belowA5 yet.
                auto belowA5 = m_data.size() + m_bss.size();
                auto targetAddr = targetSymbol->st_value + rela->r_addend - belowA5;

                // If the target is out of range, it can only be fixed by
                // adding more stuff to the code section, which is too much
                // work for now, since it would require adjusting all of the
                // symbol and relocation addresses in the section after the
                // point where the code was changed. Try `-msep-data`.
                if (int(targetAddr) < INT16_MIN || int(targetAddr) > INT16_MAX)
                    throw std::runtime_error("Target out-of-range");

                source.setU16(rela->r_offset - 2, (op & ~kEAMask) | kEAA5);
                source.setU16(rela->r_offset, targetAddr);
            }
            break;
            case XrefKind::Invalid:
                if (m_verbose)
                    warnReloc(std::cerr, "Invalid ref", rela, source.header, targetSymbol);
            break;
            case XrefKind::InvalidEhFrame:
                // References from .eh_frame, with the exception of
                // __gcc_personality_v0. Should be direct references within the
                // code segment.

                if (m_verbose)
                    warnReloc(std::cerr, "Clearing .eh_frame ref", rela, source.header, targetSymbol);

                // TODO: Why?
                source.setU32(rela->r_offset, 0);
            break;
            case XrefKind::InvalidUnaligned:
                warnReloc(std::cerr, "Unaligned ref", rela, source.header, targetSymbol);
            break;
            case XrefKind::InvalidUnsupported:
                warnReloc(std::cerr, "Unsupported ref", rela, source.header, targetSymbol);
            break;
            case XrefKind::InvalidRange:
            {
                auto base = source.header->sh_addr;
                auto maxOffset = base + source.size() - relaSize;
                std::ostringstream msg;
                msg << std::hex << "Relocation out of range (" << "0x" << base
                    << " >= 0x" << rela->r_offset
                    << " >= 0x" << maxOffset << ")";
                warnReloc(std::cerr, msg.str().c_str(), rela, source.header, targetSymbol);
            }
            break;
            case XrefKind::Weak:
                if (m_verbose)
                    warnReloc(std::cout, "Ignoring weak symbol reference", rela,
                        source.header, targetSymbol);
            break;
        }
    }
}

std::pair<size_t, std::string> Object::processJumpTables()
{
    auto jtIndex = m_jtFirstIndex;
    // Use signed size since it is possible in the future that jump table might
    // end up being offset negatively if someone needs to support making it
    // bigger than the 32k limit and it is just easier to have the correct
    // checks in place already (the compiler would complain if the type were
    // unsigned).
    ssize_t a5JTOffset = m_jtHeaderSize + jtIndex * m_jtEntrySize;

    // Source data relocation tables that received late entries and need to be
    // sorted
    std::unordered_set<Elf32_Section> unsortedRelocs;

    std::ostringstream jumpTable;
    for (const auto &[targetSection, sectionTable] : m_jumpTables)
    {
        SSec<uint8_t> target { elf_getscn(m_elf, targetSection) };
        auto codeID = getCodeID(targetSection);
        if (codeID == 0)
            // This should never happen
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

            for (auto [sourceIndex, offset] : sourceAddrs)
            {
                SSec<uint8_t> source { elf_getscn(m_elf, sourceIndex) };

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
                // bra.w (d16,%pc)  | "             | addi.l d32,(%sp) | nop nop nop
                //                  |               | rts              | nop
                // bsr.l d32        | jsr (d16,%a5) | pea (14,%pc)     | pea (14,%pc)  ; 16+20 cycles
                // bsr.w (d16,%pc)  | "             | pea (4,%pc)      | jmp (d16,%a5) ; jsr+nop would be
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
                    // This is a check for the operand of pea (4,%pc) or lea
                    // (4,%pc),%an. The operator will be checked later
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
                else if (op == kOpBraL || op == kOpJmpI32 || op == kOpBraW)
                {
                    source.setU16(offset - 2, kOpJmpA5);
                    source.setU16(offset, a5JTOffset);
                    if (op != kOpBraW)
                        source.setU16(offset + 2, kNoOp);
                }
                else if (op == kOpBsrL || op == kOpJsrI32 || op == kOpBsrW)
                {
                    source.setU16(offset - 2, kOpJsrA5);
                    source.setU16(offset, a5JTOffset);
                    if (op != kOpBsrW)
                        source.setU16(offset + 2, kNoOp);
                }
                else if (sourceIndex == m_data.index()
                    || isOffsetInEhFrame(getCodeID(sourceIndex), offset, nullptr))
                {
                    // Assume this is a vtable or similar, rewrite the offset to
                    // point to the corresponding jump table entry, and give it
                    // a relocation
                    source.setU32(offset, a5JTOffset);
                    m_relocations[sourceIndex][RelocData].push_back(offset);
                    unsortedRelocs.insert(sourceIndex);
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

    for (auto sourceIndex : unsortedRelocs)
    {
        auto &table = m_relocations[sourceIndex][RelocData];
        std::sort(table.begin(), table.end());
    }

    return { jtIndex, jumpTable.str() };
}

void Object::emitRes0(Resources &rsrc)
{
    auto belowA5 = m_data.size() + m_bss.size();
    auto [jtNumEntries, jumpTable] = processJumpTables();
    auto jtSize = jtNumEntries * m_jtEntrySize;
    auto aboveA5 = m_jtHeaderSize + jtSize;

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
                << std::hex << -m_data.header->sh_addr << std::dec << "\n";

        if (m_bss)
            std::cout
                << ".bss: " << m_bss.size() << " bytes at A5-0x"
                << std::hex << -m_bss.header->sh_addr << std::dec << "\n";
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
            data0 += CompressPalmData(combined, -m_data.header->sh_addr);

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

        std::ostringstream relocs;
        auto dataRelocsSize = SerializeRelocsPalm(relocs, m_relocations[m_data.index()], false);

        longword(reinterpret_cast<uint8_t *>(data0.data()),
            data0.size() + dataRelocsSize);

        data0 += relocs.str();

        rsrc.addResource(Resource(m_dataOsType, 0, std::move(data0)));
    }
    else
#endif
    {
        rsrc.addResource(Resource(m_dataOsType, 0, std::string(m_data.view())));
        rsrc.addResource(Resource("RELA", 0, SerializeRelocs(m_relocations[m_data.index()])));
    }
}

void Object::MultiSegmentApp(const std::string &filename, const SegmentMap &segmentMap)
{
    ResourceFile file;
    Resources& rsrc = file.resources;

    emitRes0(rsrc);

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
            {
                std::ostringstream relocs;
                SerializeRelocsPalm(relocs, m_relocations[section.index()], true);
                code += relocs.str();
            }

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
