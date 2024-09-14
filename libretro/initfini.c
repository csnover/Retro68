#include <assert.h>
#include <stdint.h>

/*
   struct object is an internal data structure in libgcc.
   Comments in unwind-dw2-fde.h imply that it will not
   increase in size.
 */
struct object { long space[8]; };

extern void __register_frame_info(const void *, struct object *)
    __attribute__ ((weak));
extern void *__deregister_frame_info(const void *)
    __attribute__ ((weak));

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

// This frame is for
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
