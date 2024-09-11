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

#ifndef SYMTAB_H
#define SYMTAB_H

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <libelf.h>

class Symtab
{
public:
    using Address = std::pair<Elf32_Section, Elf32_Off>;

    void Load(Elf_Scn *scn, const char *strtab);

    const Elf32_Sym *GetSym(int index) const;
    int FindSym(Elf32_Section sectionIndex, Elf32_Off offset) const;
    int FindSym(const std::string &name) const;

private:
    Elf32_Sym *m_symbols = nullptr;
    Elf32_Word m_count = 0;
    std::map<Address, int> m_symbolsByAddress;
    std::unordered_map<std::string, int> m_symbolsByName;
};


#endif // SYMTAB_H
