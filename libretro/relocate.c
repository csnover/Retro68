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

    Under Section 7 of GPL version 3, you are granted additional
    permissions described in the GCC Runtime Library Exception, version
    3.1, as published by the Free Software Foundation.

    You should have received a copy of the GNU General Public License and
    a copy of the GCC Runtime Library Exception along with this program;
    see the files COPYING and COPYING.RUNTIME respectively.  If not, see
    <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stdlib.h>

#ifdef __palmos__
#include <SystemMgr.h>
#define DisposePtr MemPtrFree
#define StripAddress
#else
#include <Processes.h>
#include <Sound.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Traps.h>
#include <Resources.h>
#endif

#include "Retro68Runtime.h"
#include "PoorMansDebugging.h"

typedef void (*PreinitFunction)(uint16_t);
typedef void (*voidFunction)(void);

typedef struct GccInitFini {
    uint16_t n;
    voidFunction fn;
} GccInitFini;

_Static_assert(sizeof(GccInitFini) == 6, "sizeof(GccInitFini) != 6");

/*
   Linker-defined addresses in the binary;
 */

// absolute address 0x1 for multiseg applications,
// absolute address NULL (or undefined) for code resources
extern char _MULTISEG_APP[] __attribute__ ((weak));

// section boundaries
extern char _stext[], _etext[], _sdata[], _edata[], _sbss[], _ebss[];

// pre-initialization list
extern PreinitFunction __preinit_section[], __preinit_section_end[];

// constructor list
extern struct GccInitFini __init_section[], __init_section_end[];

// destructor list
extern struct GccInitFini __fini_section[], __fini_section_end[];

#if 0 // The feature that used this is disabled
// address of start of code reource.
// usually equal to _stext, but can be overridden.
extern void _rsrc_start[];
#endif

extern voidFunction __CTOR_LIST__[], __CTOR_END__[], __DTOR_LIST__[], __DTOR_END__[];
extern char __EH_FRAME_BEGIN__[];

#ifndef __palmos__
Retro68RelocState relocState __attribute__ ((section(".relocvars"))) = {
    NULL
    , NULL, false, false
};
#endif

#ifdef __palmos__
#define GET_VIRTUAL_ADDRESS(NAME, SYM)      \
    do {                                    \
        __asm__( "\tlea " #SYM ", %0\n"     \
                 : "=a"(NAME) );            \
    } while(0)
#else
#define GET_VIRTUAL_ADDRESS(NAME, SYM) \
    do {    \
        __asm__( "\tlea " #SYM ", %0\n"    \
                 : "=a"(NAME) );    \
        if(hasStripAddr) \
            NAME = StripAddress(NAME);    \
        else                    \
            NAME = StripAddress24(NAME);    \
    } while(0)
#endif
#define READ_UNALIGNED_LONGWORD(ptr)    \
    (((((((ptr)[0] << 8) | (ptr)[1]) << 8) | (ptr)[2]) << 8) | (ptr)[3])
#define WRITE_UNALIGNED_LONGWORD(ptr, val)    \
    do {    \
        uint32_t _a = (val);    \
        uint8_t *_ptr = (ptr);    \
        _ptr[3] = _a;    \
        _ptr[2] = (_a >>= 8);    \
        _ptr[1] = (_a >>= 8);    \
        _ptr[0] = (_a >>= 8);    \
    } while(0)

void Retro68ApplyRelocations(uint8_t *base, uint32_t size, void *relocations, uint32_t displacements[])
{
    uint8_t *reloc = (uint8_t*) relocations;
    for(int relative = 0; relative <= 1; relative++)
    {
        uint8_t *addrPtr = base - 1;
        while(*reloc)
        {
                // read an uleb128 value
            uint32_t val = 0;
            uint8_t b;
            int i = 0;
            do
            {
                b = *reloc++;
                val |= (b & 0x7F) << i;
                i += 7;
            } while(b & 0x80);

                // ... which consists of an offset and the displacement base index
                // the offset is relative to the previous relocation, or to base-1
            addrPtr += val >> 2;
            uint8_t kind = val & 0x3;

            assert(addrPtr >= base);
            assert(addrPtr <= base + size - 4);

            uint32_t addr = READ_UNALIGNED_LONGWORD(addrPtr);
            addr += displacements[kind];
            if(relative)
                addr -= (uint32_t) addrPtr;
            
            WRITE_UNALIGNED_LONGWORD(addrPtr, addr);
        }

        reloc++;
    }
}

void Retro68Relocate()
{
#ifndef __palmos__
    // memory address to retrieve the ROM type (64K or a later ROM)
    // see for details http://www.mac.linux-m68k.org/devel/macalmanac.php
    short* ROM85      = (short*) 0x028E;
    
    // figure out which trap is supported
    Boolean is128KROM = ((*ROM85) > 0);
    Boolean hasSysEnvirons = false;
    Boolean hasStripAddr = false;
    Boolean hasFlushCodeCache = false;
    if (is128KROM)
    {
        UniversalProcPtr trapSysEnv = GetOSTrapAddress(_SysEnvirons);
        UniversalProcPtr trapStripAddr = GetOSTrapAddress(_StripAddress);
        UniversalProcPtr trapFlushCodeCache = GetOSTrapAddress(0xA0BD);
        UniversalProcPtr trapUnimpl = GetOSTrapAddress(_Unimplemented);

        hasSysEnvirons = (trapSysEnv != trapUnimpl);
        hasStripAddr = (trapStripAddr != trapUnimpl);
        hasFlushCodeCache = (trapFlushCodeCache != trapUnimpl);
    }
#endif

    // Figure out the displacement
    // what is the difference between the addresses in our program code
    // and an address calculated by PC-relative access?
    long displacement;

#ifdef __palmos__
    RETRO68_GET_DISPLACEMENT(displacement);
#else
    if (hasStripAddr)
    {
        RETRO68_GET_DISPLACEMENT_STRIP(displacement);
    }
    else
    {
        RETRO68_GET_DISPLACEMENT_STRIP24(displacement);
    }
#endif

    struct Retro68RelocState *rState = (Retro68RelocState*)
            ((char*)&relocState + displacement);
    // rState now points to the global relocState variable
    // 
    if(displacement == 0)
    {
        if(rState->bssPtr)
        {
            // this is not the first time, no relocations needed.
            // should only happen for code resources
            // that are invoked more than once.

            // Lock the code to be sure.
#ifndef __palmos__
            if(rState->codeHandle)
                HLock(rState->codeHandle);
#endif
            return;
        }
    }

#ifndef __palmos__
    rState->hasStripAddr = hasStripAddr;
    rState->hasFlushCodeCache = hasFlushCodeCache;
#endif

    // Locate the start of the FLT file header inside the code resource
    uint8_t *orig_stext, *orig_etext, *orig_sdata, *orig_edata, *orig_sbss, *orig_ebss;
    
    GET_VIRTUAL_ADDRESS(orig_stext, _stext);
    GET_VIRTUAL_ADDRESS(orig_etext, _etext);
    GET_VIRTUAL_ADDRESS(orig_sdata, _sdata);
    GET_VIRTUAL_ADDRESS(orig_edata, _edata);
    GET_VIRTUAL_ADDRESS(orig_sbss, _sbss);
    GET_VIRTUAL_ADDRESS(orig_ebss, _ebss);
    
    log(orig_stext);
    log(orig_etext);
    log(orig_sdata);
    log(orig_edata);
    log(orig_sbss);
    log(orig_ebss);
    
    uint8_t *base = orig_stext + displacement;

    long bss_displacement = 0;
    long data_displacement = 0;
    long jt_displacement = 0;

    if(_MULTISEG_APP)
    {
#ifdef __palmos__
        uint8_t *a5;
        __asm__("move.l %%a5, %0\n" : "=r"(a5));
#else
        uint8_t * a5 = (uint8_t*) SetCurrentA5();
#endif
        bss_displacement = a5 - orig_ebss;
        data_displacement = a5 - orig_ebss;
        jt_displacement = a5 - (uint8_t*)NULL;
    }
    else
    {
        data_displacement = displacement;
#if 0
        // find the beginning of the current code resource and lock it.
        // this crashes with some implementations of the memory manager
        // if we guess wrong, so let's don't for now.
        // Therefore, all Retro68-compiled code resources have to be locked,
        // or they might get moved as soon as the global variables are
        // allocated below.
        // TODO: figure out a way to reliably determine the offset from the
        //       start of the resource (to pass it from Elf2Mac, probably).

        {
            uint8_t *orig_rsrc_start;
            GET_VIRTUAL_ADDRESS(orig_rsrc_start, _rsrc_start);
            uint8_t *rsrc_start = orig_rsrc_start + displacement;
            
            Handle h = RecoverHandle((Ptr) rsrc_start);        
            if(MemError() == noErr && h)
            {
                // Make sure the code is locked. Only relevant for some code resources.
                HLock(h);
                rState->codeHandle = h;  
            }
        }
#endif    
        // Allocate BSS section (uninitialized/zero-initialized global data)
        if(!rState->bssPtr)
        {
            uint32_t bss_size = orig_ebss - orig_sbss;
#ifdef __palmos__
            rState->bssPtr = MemPtrNew(bss_size);
            if (rState->bssPtr)
                MemSet(rState->bssPtr, bss_size, 0);
#else
            THz zone = ApplicationZone();
            if(!zone || base < (uint8_t*)zone)
                rState->bssPtr = NewPtrSysClear(bss_size);
            else
                rState->bssPtr = NewPtrClear(bss_size);
#endif
            bss_displacement = (uint8_t*)rState->bssPtr - orig_sbss;
        }
    }    
    
    /*
        Relocation records logically consist of
            * the offset of the longword being relocated
            * the displacement base, specified as an index into the following table:
     */
    long displacements[4] = {
            displacement,    // code
            data_displacement,
            bss_displacement,
            jt_displacement
    };
    
    void *reloc;
    Handle RELA = NULL;
    uint32_t relocatableSize;
    if(_MULTISEG_APP)
    {
#ifdef __palmos__
        RELA = DmGetResource('RELA', 1);
        assert(RELA);
        reloc = MemHandleLock(RELA);
#else
        RELA = GetResource('RELA', 1);
        assert(RELA);
        reloc = *RELA;
#endif
        uint32_t text_size = orig_etext - orig_stext;
        relocatableSize = text_size;
    }
    else
    {
        uint32_t text_and_data_size = orig_edata - orig_stext;
        reloc = base + text_and_data_size;
        relocatableSize = text_and_data_size;
    }
    
    typedef typeof(&Retro68ApplyRelocations) ApplyRelocationsPtr;
    ApplyRelocationsPtr RealApplyRelocations;
    RealApplyRelocations = (ApplyRelocationsPtr) ((uint8_t*)&Retro68ApplyRelocations + displacement);
    RealApplyRelocations(base, relocatableSize, reloc, displacements);

    // We're basically done.
#ifdef __palmos__
    if(RELA)
    {
        MemHandleUnlock(RELA);
        DmReleaseResource(RELA);
    }
#else
    if(hasFlushCodeCache)
        FlushCodeCache();
#endif

    // accessing globals and calling functions is OK below here.
    // ... as long as it is in the current segment.

    Retro68InitMultisegApp();
    
    // Now we're set.
    // Someone still needs to invoke Retro68CallConstructors
    // ... but that's the job of _start(). 
}

#ifdef __palmos__
void Retro68CallPreinit(uint16_t flags)
{
    for (PreinitFunction *p = __preinit_section; p < __preinit_section_end; ++p)
        (*p)(flags);
}
#endif

void Retro68CallConstructors()
{
    static struct object object;
    if (__register_frame_info)
        __register_frame_info(__EH_FRAME_BEGIN__, &object);

    for(struct GccInitFini *p = __init_section; p < __init_section_end; ++p)
        (p->fn)();

    for(voidFunction *p = __CTOR_LIST__; p != __CTOR_END__; ++p)
        (*p)();
}

void Retro68CallDestructors()
{
    for(voidFunction *p = __DTOR_LIST__; p != __DTOR_END__; ++p)
        (*p)();

    for(struct GccInitFini *p = __fini_section; p < __fini_section_end; ++p)
        (p->fn)();

    if (__deregister_frame_info)
        __deregister_frame_info(__EH_FRAME_BEGIN__);
}


void Retro68FreeGlobals()
{
    if(relocState.bssPtr != (Ptr) -1)
    {
        DisposePtr(relocState.bssPtr);
        relocState.bssPtr = (Ptr) -1;
    }
}
