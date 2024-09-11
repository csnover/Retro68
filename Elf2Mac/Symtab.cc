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

#include "Symtab.h"

#include <libelf.h>
#include <assert.h>
#include <utility>

#include "Object.h"

void Symtab::Load(Elf_Scn *scn, const char *strtab)
{
    const auto *shdr = elf32_getshdr(scn);
    m_symbols = static_cast<Elf32_Sym *>(elf_getdata(scn, nullptr)->d_buf);
    m_count = shdr->sh_size / shdr->sh_entsize;

    for (auto index = 0U; index < m_count; ++index)
    {
        const auto &symbol = m_symbols[index];

        if (symbol.st_shndx != SHN_UNDEF && symbol.st_shndx < SHN_LORESERVE)
            m_symbolsByAddress[{symbol.st_shndx, symbol.st_value}] = index;

        if (symbol.st_name)
            m_symbolsByName[{strtab + symbol.st_name}] = index;
    }
}

const Elf32_Sym *Symtab::GetSym(int index) const
{
    Elf32_Sym *symbol = nullptr;
    if (index >= 0 && index < int(m_count))
        symbol = m_symbols + index;
    return symbol;
}

int Symtab::FindSym(Elf32_Section sectionIndex, Elf32_Off offset) const
{
    int index = -1;
    Address key { sectionIndex, offset };
    if (auto p = m_symbolsByAddress.find(key); p != m_symbolsByAddress.end())
        index = p->second;
    return index;
}

int Symtab::FindSym(const std::string &name) const
{
    int index = -1;
    if (auto p = m_symbolsByName.find(name); p != m_symbolsByName.end())
        index = p->second;
    return index;
}
