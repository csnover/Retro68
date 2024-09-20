/*
    Copyright 2015 Wolfgang Thaller.

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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <reent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#ifdef __palmos__
#include <Core/System/Chars.h>
#include <Core/System/DebugMgr.h>
#include <Core/System/ErrorBase.h>
#include <Core/System/FileStream.h>
#include <Core/System/MemoryMgr.h>
#include <Core/System/TimeMgr.h>
#include <Core/UI/Event.h>
#else
#include <MacMemory.h>
#include <Processes.h>
#include <Files.h>
#include <TextUtils.h>
#include <Errors.h>
#endif

void *_sbrk_r(struct _reent *reent, ptrdiff_t increment)
{
    (void)reent;
#ifdef __palmos__
    DbgSrcBreak();
    MemPtr p = MemPtrNew(increment);
    if(p)
        MemSet(p, increment, 0);
#else
    Debugger();
    Ptr p = NewPtrClear(increment);
#endif
    return p;
}

void _exit(int status)
{
    (void)status;
#ifdef __palmos__
    // prc-tools just raised an error when someone tried to call exit. This
    // implementation tries to do the right thing, taken from the Palm OS
    // Programmer’s Companion, Volume I, p163: “Opening the Launcher
    // Programmatically”
    EventType e;
    MemSet(&e, sizeof(e), 0);
    e.eType = keyDownEvent;
    e.data.keyDown.chr = launchChr;
    e.data.keyDown.modifiers = commandKeyMask;
    EvtAddEventToQueue(&e);
    for(;;) EvtGetEvent(&e, evtWaitForever);
#else
    //if(status != 0)
    //    Debugger();
    ExitToShell();
    for(;;)
        ;
#endif
}

#ifndef __palmos__
ssize_t _consolewrite(int fd, const void *buf, size_t count);
ssize_t _consoleread(int fd, void *buf, size_t count);

const int kMacRefNumOffset = 10;
#endif

ssize_t _write_r(struct _reent *reent, int fd, const void *buf, size_t count)
{
    (void)reent;
#ifdef __palmos__
    return FileWrite((FileHand)fd, buf, 1, count, NULL);
#else
    long cnt = count;
    if(fd >= kMacRefNumOffset)
    {
        FSWrite(fd - kMacRefNumOffset, &cnt, buf);
        return cnt;
    }
    else
        return _consolewrite(fd,buf,count);
#endif
}

ssize_t _read_r(struct _reent *reent, int fd, void *buf, size_t count)
{
#ifdef __palmos__
    Err err;
    long cnt = FileRead((FileHand)fd, buf, 1, count, &err);
    if (err != errNone)
        reent->_errno = EIO;
    return cnt;
#else
    (void)reent;
    long cnt = count;
    if(fd >= kMacRefNumOffset)
    {
        FSRead(fd - kMacRefNumOffset, &cnt, buf);
        return cnt;
    }
    else
        return _consoleread(fd,buf,count);
#endif
}

int _open_r(struct _reent *reent, const char* name, int flags, int mode)
{
#ifdef __palmos__
    UInt32 openMode;
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:
            openMode = fileModeReadOnly;
            break;
        case O_RDWR:
        case O_WRONLY:
            // TODO: Emulate O_CREAT
            if(flags & O_APPEND)
                openMode = fileModeAppend;
            else if(flags & O_TRUNC)
                openMode = fileModeReadWrite;
            else
                openMode = fileModeUpdate;
            break;
    }
    if(flags & O_EXCL)
        openMode |= fileModeExclusive;
    Err err;
    FileHand fp = FileOpen(0, name, 0, 0, mode, &err);
    switch(err)
    {
        case errNone: return (int)fp;
        case fileErrNotFound: reent->_errno = EACCES; break;
        case fileErrMemError: reent->_errno = ENOMEM; break;
        default: reent->_errno = EINVAL; break;
    }
    return -1;
#else
    Str255 pname;
#if TARGET_API_MAC_CARBON
    // Carbon has the new, sane version.
    c2pstrcpy(pname,name);
#else
    // It is also available in various glue code libraries and
    // in some versions of InterfaceLib, but it's confusing.
    // Using the inplace variant, c2pstr, isn't much better than
    // doing things by hand:
    strncpy(&pname[1],name,255);
    pname[0] = strlen(name);
#endif
    short ref;

    SInt8 permission;
    switch(flags & O_ACCMODE)
    {
        case O_RDONLY:
            permission = fsRdPerm;
            break;
        case O_WRONLY:
            permission = fsWrPerm;
            break;
        case O_RDWR:
            permission = fsRdWrPerm;
            break;
    }

    if(flags & O_CREAT)
    {
        HCreate(0,0,pname,'????','TEXT');
    }

    OSErr err = HOpenDF(0,0,pname,fsRdWrPerm,&ref);
    if(err == paramErr)
        err = HOpen(0,0,pname,fsRdWrPerm,&ref);

    (void)reent;
    if(err)
        return -1;    // TODO: errno

    if(flags & O_TRUNC)
    {
        SetEOF(ref, 0);
    }

    return ref + kMacRefNumOffset;
#endif
}

int _close_r(struct _reent *reent, int fd)
{
#ifdef __palmos__
    (void)fd;
    switch(FileClose((FileHand)fd))
    {
        case errNone: return 0;
        case fileErrInvalidDescriptor: reent->_errno = EBADF; break;
        default: reent->_errno = EIO; break;
    }
    return -1;
#else
    if(fd >= kMacRefNumOffset) {
        short refNum = fd - kMacRefNumOffset;
        short vRefNum;
        OSErr err = GetVRefNum(refNum, &vRefNum);
        FSClose(refNum);
        if (err == noErr)
            FlushVol(NULL, vRefNum);
    }
#endif
    return 0;
}

int _fstat_r(struct _reent *reent, int fd, struct stat *buf)
{
    (void)reent;
    (void)fd;
    (void)buf;
    return -1;
}

extern int _stat_r(struct _reent *reent, const char *fn, struct stat *buf)
{
    (void)reent;
    (void)fn;
    (void)buf;
    return -1;
}

off_t _lseek_r(struct _reent *reent, int fd, off_t offset, int whence)
{
#ifdef __palmos__
    FileOriginEnum origin = fileOriginBeginning;
    switch(whence)
    {
        case SEEK_CUR: origin = fileOriginCurrent; break;
        case SEEK_END: origin = fileOriginEnd; break;
    }
    Err err;
    off_t offs;
    if((err = FileSeek((FileHand)fd, offset, origin)) == errNone)
        offs = FileTell((FileHand)fd, NULL, &err);
    else
        offs = (off_t) -1;

    if(err != errNone)
        reent->_errno = EINVAL;
    return offs;
#else
    (void)reent;
    if(fd >= kMacRefNumOffset)
    {
        short posMode;
        switch(whence)
        {
            case SEEK_SET:
                posMode = fsFromStart;
                break;
            case SEEK_CUR:
                posMode = fsFromMark;
                break;
            case SEEK_END:
                posMode = fsFromLEOF;
                break;
        }

        short ref = fd - kMacRefNumOffset;
        SetFPos(ref, posMode, offset);
        long finalPos;
        GetFPos(ref, &finalPos);
        return finalPos;
    }
    return (off_t) -1;
#endif
}

int _kill_r(struct _reent *reent, pid_t pid, int sig)
{
    (void)reent;
    (void)sig;
    if(pid == 42)
        _exit(42);
    else
        return -1;
}

pid_t _getpid_r(struct _reent *reent)
{
    (void)reent;
    return 42;
}

int _fork_r(struct _reent *reent)
{
    (void)reent;
    return -1;
}

int _execve_r(struct _reent *reent, const char *fn, char *const * argv, char *const *envp)
{
    (void)reent;
    (void)fn;
    (void)argv;
    (void)envp;
    return -1;
}

int _fcntl_r(struct _reent *reent, int fd, int cmd, int arg)
{
    (void)reent;
    (void)fd;
    (void)cmd;
    (void)arg;
    return -1;
}

int _isatty_r(struct _reent *reent, int fd)
{
#ifdef __palmos__
    (void)fd;
    reent->_errno = ENOTTY;
    return 0;
#else
    (void)reent;
    return fd < kMacRefNumOffset;
#endif
}

int _link_r(struct _reent *reent, const char *from, const char *to)
{
    (void)from;
    (void)to;
    reent->_errno = EPERM;
    return -1;
}

int _mkdir_r(struct _reent *reent, const char *fn, int mode)
{
    (void)reent;
    (void)fn;
    (void)mode;
    return -1;
}

int _rename_r(struct _reent *reent, const char *from, const char *to)
{
    (void)reent;
    (void)from;
    (void)to;
    return -1;
}

int _unlink_r(struct _reent *reent, const char *fn)
{
#ifdef __palmos__
    switch (FileDelete(0, fn))
    {
        case errNone: return 0;
        case fileErrNotFound: reent->_errno = ENOENT; break;
        default: reent->_errno = EIO; break;
    }
#else
    (void)reent;
    (void)fn;
#endif
    return -1;
}

_CLOCK_T_ _times_r(struct _reent *reent, struct tms *buf)
{
    (void)buf;
    reent->_errno = EACCES;
    return  -1;
}

int _wait_r(struct _reent *reent, int *wstatus)
{
    (void)wstatus;
    reent->_errno = ECHILD;
    return -1;                    /* Always fails */
}

int _gettimeofday_r(struct _reent *reent, struct timeval *tp, void *__tz)
{
    (void)reent;
    (void)__tz;
    /* Classic MacOS's GetDateTime function returns an integer.
     * TickCount() has a slightly higher resolution, but is independent of the real-time clock.
     */
    unsigned long secs, ticks;
#ifdef __palmos__
    secs = TimGetSeconds();
    ticks = TimGetTicks();
#else
    GetDateTime(&secs);
    ticks = TickCount();
#endif

    static unsigned long savedTicks = 0, savedSecs = 0;
    
    if(!savedSecs)
    {
        savedTicks = ticks;
        savedSecs = secs;
    }
    else
    {
        unsigned long elapsedTicks = ticks - savedTicks;
        unsigned long elapsedSecs = secs - savedSecs;
        unsigned long expectedTicks = elapsedSecs * 60 + elapsedSecs * 3 / 20;
        
        if(expectedTicks > elapsedTicks)
            savedTicks = ticks;
        else
            savedTicks += expectedTicks;
        savedSecs = secs;
    }

    if(tp)
    {
        const int epochDifferenceInYears = 1970 - 1904;
        const int epochDifferenceInDays =
            365 * epochDifferenceInYears
            + (epochDifferenceInYears + 3)/ 4;  // round up for leap years

        tp->tv_sec = secs - 86400 * epochDifferenceInDays;
        tp->tv_usec = (ticks - savedTicks) * 20000000 / 2003;
    }

    return 0;
}
