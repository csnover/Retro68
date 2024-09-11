#ifndef RESOURCEFILE_H
#define RESOURCEFILE_H

#include <memory>
#include <iosfwd>
#include <string>

#include "ResType.h"
#include "ResourceFork.h"

class ResourceFile
{
public:
    enum class Format
    {
        autodetect,
#ifdef __APPLE__
        real = 1,
#endif
#ifdef PALMOS
        prc = 2,
#endif
        macbin = 3,
        diskimage,
        basilisk,
        applesingle,
        underscore_appledouble,
        percent_appledouble
    };

    bool read(std::string path, Format f = Format::autodetect);
    bool write(std::string path, Format f = Format::autodetect);
    bool read(std::istream& in, Format f);
    bool write(std::ostream& in, Format f);
    static bool hasPlainDataFork(Format f);
    bool hasPlainDataFork();
    Format getFormat() { return format; }

    static bool isSingleFork(Format f);

#ifdef PALMOS
    std::string name;
    int attributes = 1, version = 1;
    std::vector<char> appInfo, sortInfo;
#endif
    ResType type;
    ResType creator;
    Resources resources;
    std::string data;

private:
    bool assign(std::string path, Format f = Format::autodetect);
    bool read();
    bool write();

    std::string pathstring;
    std::string filename;
    Format format = Format::autodetect;
};

#endif // RESOURCEFILE_H
