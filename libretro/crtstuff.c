// SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1

#include <assert.h>
#include <stdint.h>

#include "Retro68Runtime.h"

typedef void (*PreinitFunction)(uint16_t);
typedef void (*VoidFunction)(void);

// TODO: Document the origin of this structure.
typedef struct GccInitFini
{
    uint16_t n;
    VoidFunction fn;
} GccInitFini;

_Static_assert(sizeof(GccInitFini) == 6, "sizeof(GccInitFini) != 6");

extern PreinitFunction __preinit_section[], __preinit_section_end[];
extern GccInitFini __init_section[], __init_section_end[];
extern GccInitFini __fini_section[], __fini_section_end[];
extern VoidFunction __CTOR_LIST__[], __CTOR_END__[];
extern VoidFunction __DTOR_LIST__[], __DTOR_END__[];

extern char __EH_FRAME_BEGIN__[];

void Retro68CallPreinit(uint16_t flags)
{
    for (PreinitFunction *p = __preinit_section; p != __preinit_section_end; ++p)
        (*p)(flags);
}

void Retro68CallConstructors()
{
    static struct object object;
    if (__register_frame_info)
        __register_frame_info(__EH_FRAME_BEGIN__, &object);

    for (GccInitFini *p = __init_section; p != __init_section_end; ++p)
        (p->fn)();

    for (VoidFunction *p = __CTOR_LIST__; p != __CTOR_END__; ++p)
        (*p)();
}

void Retro68CallDestructors()
{
    for (VoidFunction *p = __DTOR_LIST__; p != __DTOR_END__; ++p)
        (*p)();

    for (GccInitFini *p = __fini_section; p != __fini_section_end; ++p)
        (p->fn)();

    if (__deregister_frame_info)
        __deregister_frame_info(__EH_FRAME_BEGIN__);
}
