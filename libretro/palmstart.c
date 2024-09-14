/*
 *  Pilot startup code for use with gcc.  This code was written
 *  by Kresten Krab Thorup, and is in the public domain.
 *  It is *not* under the GPL or the GLPL, you can freely link it
 *  into your programs.
 *
 *  Modified 19971111 by Ian Goldberg <iang@cs.berkeley.edu>
 *  Modified 19981104 by John Marshall  <jmarshall@acm.org>
 */

#include <Core/System/DataMgr.h>
#include <Core/System/MemoryMgr.h>
#include <PalmTypes.h>
#include <Core/System/SoundMgr.h>
#include <Core/System/SystemMgr.h>

#include <assert.h>
#include <stdint.h>

#include "Retro68Runtime.h"

static void PrvPatchV10Devices(SysAppInfoType *appInfo);
static void PrvLoadAndRelocate(MemHandle codeH, Boolean init);

UInt32 _start(void)
{
    SysAppInfoType *appInfo;
    void *prevGlobals;
    void *globalsPtr;

    if (SysAppStartup(&appInfo, &prevGlobals, &globalsPtr) != 0)
    {
        SndPlaySystemSound(sndError);
        return -1;
    }
    else
    {
        Int16 mainCmd = appInfo->cmd;
        void *mainPBP = appInfo->cmdPBP;
        UInt16 mainFlags = appInfo->launchFlags;
        UInt32 result;

        if (mainFlags & sysAppLaunchFlagNewGlobals)
        {
            PrvPatchV10Devices(appInfo);
            PrvLoadAndRelocate(appInfo->codeH, true);
        }

        Retro68CallPreinit(mainFlags);

        if (mainFlags & sysAppLaunchFlagNewGlobals)
            Retro68CallConstructors();

        result = PilotMain(mainCmd, mainPBP, mainFlags);

        if (mainFlags & sysAppLaunchFlagNewGlobals)
        {
            Retro68CallDestructors();
            PrvLoadAndRelocate(appInfo->codeH, false);
        }

        SysAppExit(appInfo, prevGlobals, globalsPtr);

        return result;
    }
}

register char *A5World asm ("a5");

typedef struct CodeHeader
{
    UInt16 nearOffset; //< Offset to near jump table in A5 (unused)
    UInt16 numEntries; //< Number of jump table entries
    UInt32 farOffset; //< Offset to far jump table in A5
    UInt32 relocOffset; //< Offset to relocation table in code resource
} CodeHeader;

_Static_assert(sizeof(CodeHeader) == 12, "sizeof(CodeHeader) != 12");

typedef struct JumpTableEntry
{
    UInt16 op;
    uintptr_t imm;
} JumpTableEntry;

_Static_assert(sizeof(JumpTableEntry) == 6, "sizeof(JumpTableEntry) != 6");

// TODO: Do not duplicate relocate.c
static inline UInt32 PrvReadUnaligned(const char *v)
{
    return (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3];
}

static const char *PrvRelocate(const char *relocs, char *codeP, const void *displacement)
{
    UInt32 count = PrvReadUnaligned(relocs);
    relocs += 4;

    for (UInt32 index = 0; index < count; ++index)
    {
        if ((*relocs & 0x80) != 0)
            codeP += (char)(*relocs++ * 2);
        else if ((*relocs & 0x40) != 0)
        {
            codeP += ((short)(((relocs[0] << 8) | relocs[1]) << 2)) >> 1;
            relocs += 2;
        }
        else
        {
            codeP += PrvReadUnaligned(relocs) * 2;
            relocs += 4;
        }

        *((uintptr_t *) codeP) += (uintptr_t) displacement;
    }

    return relocs;
}

// See Elf2Mac/PalmCompressor.cc
static const char *PrvSkipToXREFS(const char *dataP)
{
    enum Op {
        Literal     = 0x80,
        ZeroRun     = 0x40,
        ValueRun    = 0x20,
        FFRun       = 0x10,
        Pat0000FXXX = 2,
        Pat0000FFXX = 1,
        End         = 0,
    };

    dataP += 4;

    for (int i = 0; i < 3; ++i)
    {
        dataP += 4;

        for (;;)
        {
            char c = *dataP++;
            if (c & Literal)
                dataP += c & ~Literal;
            else if (c & ZeroRun)
                ;
            else if (c & ValueRun)
                ++dataP;
            else if (c & FFRun)
                ;
            else if (c == Pat0000FXXX)
                dataP += 3;
            else if (c == Pat0000FFXX)
                dataP += 2;
            else if (c == End)
                break;
        }
    }

    return dataP;
}

// Apparently early Palm OS devices do not perform data relocation themselves
static void PrvPatchV10Devices(SysAppInfoType *appInfo)
{
    if ((appInfo->launchFlags & sysAppLaunchFlagDataRelocated) != 0)
        return;

    char *codeP = (char *) MemHandleLock(appInfo->codeH);

    MemHandle dataH = DmGet1Resource(sysResTAppGData, 0);
    char *dataP = (char *) MemHandleLock(dataH);

    const char *reloc = PrvSkipToXREFS(dataP);
    reloc = PrvRelocate(reloc, A5World, A5World);
            PrvRelocate(reloc, A5World, codeP);

    MemHandleUnlock(appInfo->codeH);
    MemHandleUnlock(dataH);
    DmReleaseResource(dataH);

    appInfo->launchFlags |= sysAppLaunchFlagDataRelocated;
}

// TODO: Must ensure that emitted code have contiguous IDs in linker
// TODO: Must ensure linker only allows xrefs to code 1 and data 0
static void PrvLoadAndRelocate(MemHandle code1H, Boolean init)
{
    MemHandle codeH;
    UInt16 resID = 2;

    // Using a condition instead of two functions reduces the code size overhead
    // by eliminating an extra function frame
    if (init)
    {
        MemPtr code1P = MemHandleLock(code1H);

        for (; (codeH = DmGet1Resource(sysResTAppCode, resID)) != NULL; ++resID)
        {
            char *codeP = (char *) MemHandleLock(codeH);
            const CodeHeader *header = (const CodeHeader *) codeP;

            const char *reloc = codeP + header->relocOffset;
            reloc = PrvRelocate(reloc, codeP, A5World);
            reloc = PrvRelocate(reloc, codeP, code1P);
                    PrvRelocate(reloc, codeP, codeP);

            JumpTableEntry *entry = (JumpTableEntry *) (A5World + header->farOffset);
            JumpTableEntry *end = entry + header->numEntries;
            for (; entry != end; ++entry)
                entry->imm += (uintptr_t) codeP;

            // TODO: EH_FRAME
        }

        MemHandleUnlock(code1H);
    }
    else
    {
        for (; (codeH = DmGet1Resource(sysResTAppCode, resID)) != NULL; ++resID)
            MemHandleUnlock(codeH);
    }
}
