/*
 *  Pilot startup code for use with gcc.  This code was written 
 *  by Kresten Krab Thorup, and is in the public domain.
 *  It is *not* under the GPL or the GLPL, you can freely link it
 *  into your programs.
 *
 *  Modified 19971111 by Ian Goldberg <iang@cs.berkeley.edu>
 *  Modified 19981104 by John Marshall  <jmarshall@acm.org>
 */

#include <PalmTypes.h>
#include <DebugMgr.h>
#include <FeatureMgr.h>
#include <SystemMgr.h>

static void StartDebug(UInt16 flags)
{
    if ((flags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagSubCall)) == 0)
        return;

    UInt32 feature = 0;
    FtrGet('gdbS', 0, &feature);
    if (feature == 0x12BEEF34)
        DbgSrcBreak();
}

static void *hook __attribute__ ((section ("preinit"), unused)) = StartDebug;
