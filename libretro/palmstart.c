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
#include <SoundMgr.h>
#include <SystemMgr.h>
#include "Retro68Runtime.h"

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
            RETRO68_RELOCATE();

        Retro68CallPreinit(mainFlags);

        if (mainFlags & sysAppLaunchFlagNewGlobals)
            Retro68CallConstructors();

        result = PilotMain(mainCmd, mainPBP, mainFlags);

        if (mainFlags & sysAppLaunchFlagNewGlobals)
            Retro68CallDestructors();

        SysAppExit(appInfo, prevGlobals, globalsPtr);

        return result;
    }
}
