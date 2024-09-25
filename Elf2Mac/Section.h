#ifndef SECTION_H
#define SECTION_H

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include <BinaryIO.h>
#include <libelf.h>

// A convenience wrapper for a single ELF section.
template <typename T>
struct SSec
{
    Elf_Scn *section = nullptr;
    Elf32_Shdr *header = nullptr;
    T *data = nullptr;

    SSec() = default;

    SSec(Elf_Scn *scn)
        : section(scn)
        , header(elf32_getshdr(scn))
        , data(static_cast<T *>(elf_getdata(scn, nullptr)->d_buf))
    {
    }

    template <typename U = T>
    inline std::enable_if_t<std::is_same_v<U, uint8_t>, std::string_view>
    view() const
    {
        return std::string_view(
            reinterpret_cast<char *>(data),
            header ? header->sh_size : 0
        );
    }

    template <typename U = T>
    inline std::enable_if_t<std::is_same_v<U, uint8_t>, bool>
    inRange(Elf32_Addr vaddr, Elf32_Word size) const
    {
        // Using signed values because the base address of the data section is
        // negative
        return header
            && Elf32_Sword(vaddr) >= Elf32_Sword(header->sh_addr)
            && Elf32_Sword(vaddr + size) <= Elf32_Sword(header->sh_addr + header->sh_size);
    }

    template <typename U = T>
    inline std::enable_if_t<std::is_same_v<U, uint8_t>, uint16_t>
    getU16(Elf32_Addr vaddr) const
    {
        uint8_t *p = data + vaddr - header->sh_addr;
        return p[0] << 8 | p[1];
    }

    template <typename U = T>
    inline std::enable_if_t<std::is_same_v<U, uint8_t>, uint16_t>
    getU16(Elf32_Addr vaddr, uint16_t defaultValue) const
    {
        return inRange(vaddr, sizeof(uint16_t)) ? getU16(vaddr) : defaultValue;
    }

    template <typename U = T>
    inline std::enable_if_t<std::is_same_v<U, uint8_t>, void>
    setU16(Elf32_Addr vaddr, uint16_t value) const
    {
        word(data + vaddr - header->sh_addr, value);
    }

    template <typename U = T>
    inline std::enable_if_t<std::is_same_v<U, uint8_t>, void>
    setU32(Elf32_Addr vaddr, uint32_t value) const
    {
        longword(data + vaddr - header->sh_addr, value);
    }

    inline Elf32_Section index() const
    {
        return elf_ndxscn(section);
    }

    inline size_t size() const
    {
        return header ? header->sh_size / std::max<Elf32_Word>(header->sh_entsize, 1) : 0;
    }

    inline const T *operator[] (int index) const
    {
        if (!data || index < 0 || index >= size())
            return nullptr;
        return data + index;
    }

    inline T *operator[] (int index)
    {
        if (!data || size_t(index) >= size())
            return nullptr;
        return data + index;
    }

    explicit inline operator bool () const
    {
        return section != nullptr;
    }
};

#endif
