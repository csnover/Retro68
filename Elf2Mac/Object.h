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

#ifndef OBJECT_H
#define OBJECT_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <ResourceFile.h>
#include <libelf.h>

#include "Reloc.h"
#include "Section.h"

class SegmentMap;

class Object
{
public:
    Object(const std::string &inputFilename, bool palmos,
        const char *creator, uint32_t stackSize, bool verbose);
    ~Object();

    // Emits the object as a data resource.
    // The emitted data is a concatenation of all .text sections, followed by
    // the .data section, followed by the Retro68 relocation table.
    void FlatCode(const std::string &outputFilename);

    // Emits the object as a multi-segment resource file.
    void MultiSegmentApp(const std::string &outputFilename,
        const SegmentMap &segmentMap);

    // Emits the object as a single-segment resource file.
    // The emitted data is the same as `FlatCode`, except it it is all put into
    // code resource 1.
    void SingleSegmentApp(const std::string &outputFilename);

private:
    // Loads sections into memory from the ELF file.
    void loadSections();

    // Finalizes a resource file.
    void finalizeFile(const std::string &filename, ResourceFile &file);

    // Emits the object as unstructured code and data.
    void emitFlatCode(std::ostream& out);

    // Processes relocations from the ELF executable into intermediate jump
    // table and relocation data suitable for use when emitting resources.
    void processRelocations();

    // Processes a single relocation table from the ELF executable.
    void processRelocation(const SSec<Elf32_Rela> &relaSection);

    // Possible kinds of cross-references.
    enum class XrefKind
    {
        // A cross-reference that can be relocated using PC-relative addressing.
        IntraPC,
        // A cross-reference that can be relocated using direct addressing.
        Direct,
        // A cross-reference that can be relocated using a jump table.
        Indirect,
        // A cross-reference that can be relocated using the A5 register.
        IndirectData,
        // A cross-reference that cannot be relocated.
        Invalid,
        // A cross-reference to another section inside .eh_frame.
        InvalidEhFrame,
        // A cross-reference that is not aligned to a word boundary.
        InvalidUnaligned,
        // An cross-reference with an unsupported relocation record type.
        InvalidUnsupported,
        // A cross-reference with an out-of-bounds offset value.
        InvalidRange,
        // A cross-reference to a weak symbol that is not present in the object.
        Weak
    };

    // Converts an instruction for the given relocation record from a
    // PC-relative instruction to a direct addressing instruction.
    void convertPCOpToDirectOp(SSec<uint8_t> &source, const Elf32_Rela *rela,
        const Elf32_Sym *targetSymbol) const;

    // Determines which kind of cross-reference is required to go from the given
    // source section to the given target symbol using the given type of
    // relocation.
    XrefKind getXrefKind(uint16_t codeID, const SSec<uint8_t> &source,
        const Elf32_Rela *rela, const Elf32_Sym *target) const;

    // Finds the symbol corresponding to the exception handling frame for the
    // given code resource.
    const Elf32_Sym *findExceptionInfoStart(uint16_t codeID) const;

    // Returns whether the given `vaddr` is within an exception handling
    // frame.
    bool isOffsetInEhFrame(uint16_t codeID, Elf32_Addr vaddr,
        const Elf32_Sym *target) const;

    // Returns the code resource ID for the given section.
    uint16_t getCodeID(Elf32_Section index) const;

    // Useful information for printing debug messages.
    struct DebugInfo
    {
        // The name of the source section.
        const char *sourceName;
        // The name of the target section.
        const char *targetName;
        // The name of the target symbol.
        const char *symbolName;
        // The value of the target symbol.
        int symbolValue;
    };

    // Emits information to the given output stream about a relocation.
    void warnReloc(std::ostream &out, const char *msg, const Elf32_Rela *rela,
        const Elf32_Shdr *sourceHeader, const Elf32_Sym *targetSymbol) const;

    // Returns information used for emitting debugging messages.
    DebugInfo collectDebugInfo(const Elf32_Shdr *sourceHeader,
        const Elf32_Sym *targetSymbol) const;

    // Returns true if the object is being compiled for Palm OS.
    inline bool isPalm() const {
#ifdef PALMOS
        return m_outputFormat == ResourceFile::Format::prc;
#else
        return false;
#endif
    }

    // Builds the main jump table and fixes up all references to the table.
    std::pair<size_t, std::string> processJumpTables();

    // Emits the code 0 and data 0 resources.
    std::pair<size_t, size_t> emitRes0(Resources &out);

#ifdef PALMOS
    // Emits the Palm OS pref 0 resource.
    void emitPref(Resources &out);
#endif

    // A fully qualified ELF data address.
    using Address = std::tuple<Elf32_Section, Elf32_Addr>;

    // A reverse map from target address to source xref.
    using JumpTable = std::map<Elf32_Addr, std::vector<Address>>;

    // A mapping from an ELF section index to its jump table.
    using SectionJumpTables = std::unordered_map<Elf32_Section, JumpTable>;

    // Target section jump tables. Populated by `processRelocations`.
    SectionJumpTables m_jumpTables;

    // Source section relocation offsets. Populated by `processRelocations`.
    std::unordered_map<Elf32_Section, Relocations> m_relocations;

    // The .symtab section.
    SSec<Elf32_Sym> m_symtab;
    // All .text sections.
    std::vector<SSec<uint8_t>> m_code;
    // All .rela sections.
    std::vector<SSec<Elf32_Rela>> m_rela;
    // The .data section.
    SSec<uint8_t> m_data;
    // The .bss section.
    SSec<uint8_t> m_bss;
    // Section header string table.
    const char *m_shstrtab = nullptr;
    // String table.
    const char *m_strtab = nullptr;
    // Input ELF object.
    Elf *m_elf = nullptr;

    // A cache of exception handling frame symbols.
    mutable std::unordered_map<int, const Elf32_Sym *> m_ehFrameCache;

    // OSTypes to use when emitting resources.
    ResType m_codeOsType, m_dataOsType, m_applOsType, m_creator;
    // Jump table and code resource data sizes.
    size_t m_jtHeaderSize, m_jtEntrySize, m_jtFirstIndex;
    // The preferred runtime stack size.
    uint32_t m_stackSize;
    // Input file descriptor for ELF object.
    int m_fd = -1;
    // Output resource file format.
    ResourceFile::Format m_outputFormat;
    // If true, log more stuff.
    bool m_verbose = false;
};

#endif // OBJECT_H
