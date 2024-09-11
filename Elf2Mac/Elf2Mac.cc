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

#include "Object.h"
#include "SegmentMap.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std::literals::string_literals;

static void RealLD(const std::string &realLdPath, const std::vector<std::string> &args)
{
    std::vector<const char *> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(realLdPath.c_str());
    for(const auto &s : args)
        argv.push_back(s.c_str());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if(pid < 0)
        throw std::runtime_error("unable to fork: "s + strerror(errno));
    else if(pid == 0)
    {
        execvp(argv[0], const_cast<char* const *> (argv.data()));
        throw std::runtime_error("exec failed: "s + strerror(errno));
    }
    else
    {
        int wstatus;
        int result = 0;
        do
        {
            result = waitpid(pid, &wstatus, 0);
        } while(result == -1 && errno == EINTR);

        if(WIFEXITED(wstatus))
        {
            int exitcode = WEXITSTATUS(wstatus);
            if(exitcode != 0)
                throw std::system_error(exitcode, std::system_category());
        }
        else
            throw std::runtime_error("ld process did not exit properly");
    }
}

static inline bool flag(char **p, const char *name)
{
    return std::strcmp(*p, name) == 0;
}

static inline char *nextArg(char **&p, const char *err)
{
    ++p;
    if(*p == nullptr)
        throw std::runtime_error(err);
    return *p;
}

static inline bool startsWith(char **p, const char *prefix)
{
    return std::strstr(*p, prefix) == *p;
}

int main(int argc, char *argv[])
{
    std::string realLdPath;
    if (const char *path = getenv("RETRO68_REAL_LD"); path && path[0] != '\0')
        realLdPath = path;
    else
        realLdPath = argv[0] + ".real"s;
    const char *outputFile = "a.out";
    const char *entryPoint = "_start";
    uint32_t stackSize = 4096;
    bool elf2mac = false;
    bool flatoutput = false;
    bool segments = true;
    bool stripMacsbug = false;
    bool saveLdScript = false;
    bool palmos = std::strstr(argv[0], "palmos") != nullptr;

    SegmentMap segmentMap;

    std::vector<std::string> ldArgs;
    for(auto p = argv + 1; *p != nullptr; ++p)
    {
        if(flag(p, "--elf2mac-real-ld"))
            realLdPath = nextArg(p, "--elf2mac-real-ld missing argument");
        else if(flag(p, "-o"))
            outputFile = nextArg(p, "-o missing argument");
        else if(startsWith(p, "-o"))
            outputFile = (*p) + 2;
        else if(flag(p, "-elf2mac") || flag(p, "--elf2mac"))
            elf2mac = true;
        else if(flag(p, "-e"))
            entryPoint = nextArg(p, "-e missing argument");
        else if(startsWith(p, "-e"))
            entryPoint = (*p) + 2;
        else if(flag(p, "--mac-flat"))
        {
            elf2mac = true;
            flatoutput = true;
            segments = false;
        }
        else if(flag(p, "--mac-single"))
        {
            elf2mac = true;
            flatoutput = false;
            segments = false;
        }
        else if(flag(p, "--mac-segments"))
        {
            elf2mac = true;
            segmentMap = SegmentMap(nextArg(p, "--mac-segments missing argument"));
        }
        else if(flag(p, "--mac-strip-macsbug"))
            stripMacsbug = true;
        else if(flag(p, "--mac-keep-ldscript"))
            saveLdScript = true;
        else if (flag(p, "--palmos"))
            palmos = true;
        else if (flag(p, "--stack"))
            stackSize = std::atoi(nextArg(p, "--stack missing argument"));
        else
            ldArgs.push_back(*p);
    }

    if(flatoutput && segments)
        throw std::runtime_error("--mac-segments can't be used with --mac-flat");

#ifndef PALMOS
    if(palmos)
        throw std::runtime_error("Not compiled with Palm OS support");
#endif

    if(elf2mac)
    {
        char tmpfile[] = "/tmp/elf2macldXXXXXX";
        int fd = mkstemp(tmpfile);
        if(fd < 0)
            throw std::runtime_error("can't create temp file: "s + strerror(errno));

        if(saveLdScript)
            std::cerr << "Ld Script at: " << tmpfile << std::endl;
        else
            unlink(tmpfile);

        {
            std::ofstream out(tmpfile);
            if(segments)
                segmentMap.CreateLdScript(out, entryPoint, stripMacsbug);
            else
                CreateFlatLdScript(out, entryPoint, stripMacsbug);
        }

        std::string inputFile { outputFile + ".gdb"s };

        ldArgs.push_back("--no-warn-rwx-segments");
        ldArgs.push_back("-o");
        ldArgs.push_back(inputFile);
        ldArgs.push_back("-T");
        ldArgs.push_back(tmpfile);

        RealLD(realLdPath, ldArgs);

        Object theObject(inputFile, palmos, stackSize);

        if(flatoutput)
            theObject.FlatCode(outputFile);
        else if(segments)
            theObject.MultiSegmentApp(outputFile, segmentMap);
        else
            theObject.SingleSegmentApp(outputFile);
    }
    else
    {
        for (auto p = argv + 1; *p != nullptr; ++p)
            ldArgs.push_back(*p);
        RealLD(realLdPath, ldArgs);
    }

    return 0;
}
