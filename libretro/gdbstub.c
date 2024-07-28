/*
 *  Pilot startup code for use with gcc.  This code was written 
 *  by Kresten Krab Thorup, and is in the public domain.
 *  It is *not* under the GPL or the GLPL, you can freely link it
 *  into your programs.
 *
 *  Modified 19971111 by Ian Goldberg <iang@cs.berkeley.edu>
 *  Modified 19981104 by John Marshall  <jmarshall@acm.org>
 */

#include <stdint.h>
#include <PalmTypes.h>
#include <DebugMgr.h>
#include <FeatureMgr.h>
#include <SystemMgr.h>

extern uint8_t _sdata, _sbss;

static void StartDebug(UInt16 flags)
{
    if ((flags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagSubCall)) == 0)
        return;

    UInt32 feature = 0;
    FtrGet('gdbS', 0, &feature);
    if (feature != 0x12BEEF34)
        return;

    // Tell the debugger the location of .text (d0), .bss (d1), .data (d2), and
    // PilotMain (a0), and call DbgBreak (trap 8). These registers are used by
    // the custom PalmOS GCC wire protocol. There may be another one.
    __asm__(
        "\tlea     %0, %%a0\n"
        "\tmove.l  %%a0, %%d2\n"
        "\tlea     %1, %%a0\n"
        "\tmove.l  %%a0, %%d1\n"
        "\tlea     _start(%%pc), %%a0\n"
        "\tmove.l  %%a0, %%d0\n"
        "\tsub.l   #_start, %%d0\n"
        "\tlea     PilotMain(%%pc), %%a0\n"
        "\tmove.l  #0x12BEEF34, %%d3\n"
        "\ttrap    #8"
        :
        : "g" (_sdata), "g" (_sbss)
        : "d0", "d1", "d2", "d3", "a0");
}

static void *hook __attribute__ ((section ("preinit"), unused)) = StartDebug;
