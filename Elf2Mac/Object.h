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

#include <cstdlib>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <ResourceFile.h>
#include <libelf.h>

#include "Reloc.h"
#include "Symtab.h"

class SegmentMap;

class Object
{
public:
    Object(const std::string &inputFilename, bool palmos, uint32_t stackSize);
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
    // A convenience wrapper for a single ELF section.
    template <typename T>
    struct SSec
    {
        Elf_Scn *section = nullptr;
        Elf32_Shdr *header = nullptr;
        T *data = nullptr;

        SSec() = default;
        SSec(Elf_Scn *scn);

        template <typename U = T>
        inline std::enable_if_t<std::is_same_v<U, uint8_t>, std::string_view>
        view() const {
            return std::string_view(
                reinterpret_cast<char *>(data),
                header ? header->sh_size : 0
            );
        }

        inline size_t size() const {
            return header ? header->sh_size / header->sh_entsize : 0;
        }

        explicit inline operator bool () const { return section != nullptr; }
    };

    // Gets a 32-bit big-endian value from the given section at the
    // given virtual address.
    static uint32_t getU32BE(const SSec<uint8_t> &section, Elf32_Addr vaddr);

    // Sets a 32-bit big-endian value in the given section at the given
    // virtual address.
    static void setU32BE(SSec<uint8_t> &section, Elf32_Addr vaddr, uint32_t value);

    // Loads sections into memory from the ELF file.
    void loadSections();

    // Finalizes a resource file.
    void finalizeFile(const std::string &filename, ResourceFile &file);

    // Emits the object as unstructured code and data.
    void emitFlatCode(std::ostream& out, Elf32_Addr newBase);

    // Processes a relocation section, rebasing offsets in the pointed-to
    // section to the given `newBase` and replacing inter-section function
    // references with indirect jumps through the jump table, if applicable.
    std::vector<RuntimeReloc> relocateSection(SSec<Elf32_Rela> &relaSection, Elf32_Addr newBase, uint16_t codeID, bool multiSegment);

    // Returns whether the given `vaddr` is within an exception handling
    // frame.
    bool isOffsetInEhFrame(uint16_t codeID, Elf32_Addr vaddr) const;

    // Adds the given symbol to the jump table for its owner section, if
    // one does not already exist, and returns the offset of the jump table
    // entry relative to the jump table displacement.
    Elf32_Addr jumpTableAddress(const Elf32_Sym *symbol, uint16_t codeID);

    // Returns the appropriate base address for a code section.
    inline size_t secOutputBase(int codeID) const {
        return codeID == 1 ? 4 : m_multiCodeOutputBase;
    }

    inline bool isPalm() const {
#ifdef PALMOS
        return m_outputFormat == ResourceFile::Format::prc;
#else
        return false;
#endif
    }

    // Emits the code0 and data0 resources.
    void emitRes0(Resources &out, uint32_t belowA5);

#ifdef PALMOS
    // Applies the Palm OS data segment compression algorithm to the given
    // input data.
    std::string compressData(std::string &&input);

    // Emits the Palm OS pref0 resource.
    void emitPref(Resources &out);
#endif

    // A mapping from a symbol address to an index in m_jumpTable.
    using JumpTable = std::map<Elf32_Addr, size_t>;

    // A mapping from an ELF section index to a jump table map.
    using SectionJumpTables = std::unordered_map<Elf32_Section, JumpTable>;

    // Symbol table with lookup indexes.
    Symtab m_symtab;
    // All .text segments.
    std::vector<SSec<uint8_t>> m_code;
    // All .rela segments.
    std::vector<SSec<Elf32_Rela>> m_rela;
    // The .data segment.
    SSec<uint8_t> m_data;
    // The .bss segment.
    SSec<uint8_t> m_bss;
    // Jump table. Used only for multi-segment apps.
    // Each entry in the jump table contains the rebased target address of a
    // cross-referenced symbol within its output resource.
    std::vector<Elf32_Addr> m_jumpTable;
    // Per-section jump table references.
    SectionJumpTables m_jumpTables;
    // Section header string table.
    const char *m_shstrtab = nullptr;
    // String table.
    const char *m_strtab = nullptr;
    // Input ELF object.
    Elf *m_elf = nullptr;
    // OSTypes to use when emitting resources.
    ResType m_codeOsType, m_dataOsType, m_applOsType;
    // Jump table and code resource data sizes.
    size_t m_jtHeaderSize, m_jtEntrySize, m_jtFirstIndex, m_multiCodeOutputBase;
    // The preferred runtime stack size.
    uint32_t m_stackSize;
    // Input file descriptor for ELF object.
    int m_fd = -1;
    // Output resource file format.
    ResourceFile::Format m_outputFormat;
    // If true, log more stuff.
    bool m_verbose = false;
    // If true, emit in Palm OS flavour.
    bool m_emitPalm;
};


#endif // OBJECT_H
