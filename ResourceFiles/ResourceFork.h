#ifndef RESOURCEFORK_H
#define RESOURCEFORK_H

#include <string>
#include <map>
#ifdef PALMOS
#include <vector>
#endif
#include "ResType.h"

class Resource
{
    ResType type;
    short id;
    std::string name;
    std::string data;
    int attr;
public:
    Resource() {}
    Resource(ResType type, int id, std::string &&data, std::string &&name = "", int attr = 0)
        : type(type), id(id), name(name), data(data), attr(attr) {}

    const std::string& getData() const { return data; }
    inline ResType getType() const { return type; }
    inline int getID() const { return id; }
    inline ResRef getTypeAndID() const { return ResRef(type, id); }
    const std::string &getName() const { return name; }
    int getAttr() const { return attr; }
};

class Fork
{
public:
     virtual void writeFork(std::ostream&) const { }
     virtual ~Fork() {}
};

class Resources : public Fork
{
public:
    std::map<ResRef, Resource> resources;

    Resources() {}
    Resources(std::istream& in);
    void writeFork(std::ostream& out) const;
    void addResource(Resource res) { resources[res.getTypeAndID()] = res; }
    void addResources(const Resources& res);

    unsigned countResources() const { return resources.size(); }

#ifdef PALMOS
    enum {
        // Actually 0x4e but there is always a 0u16 at the end of the record
        // list
        PrcHeaderSize = 0x50,

        PrcEntrySize = 10
    };
    static constexpr class prc_t {} prc = {};
    Resources(std::istream& in, prc_t);
    void writeFork(std::ostream& out, int dataOffset, const std::vector<char>& appInfo, const std::vector<char>& sortInfo) const;
#endif
};

#endif // RESOURCEFORK_H
