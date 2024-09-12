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

#ifndef SEGMENTMAP_H
#define SEGMENTMAP_H

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

using Filters = std::vector<std::string>;

struct SegmentInfo
{
    template<typename... Args>
    SegmentInfo(uint16_t id, std::string &&name, Args... args)
        : id(id)
        , name(name)
        , filters { args... }
    {
    }

    uint16_t id;
    std::string name;
    Filters filters;
};

class SegmentMap
{
public:
    SegmentMap();
    SegmentMap(const std::string &filename);

    void CreateLdScript(std::ostream& out, const char *entryPoint, bool stripMacsbug, bool palmos) const;
    const std::string &GetSegmentName(int id) const;

private:
    std::vector<SegmentInfo> m_segments;
};

void CreateFlatLdScript(std::ostream& out, const char *entryPoint, bool stripMacsbug);

#endif // SEGMENTMAP_H
