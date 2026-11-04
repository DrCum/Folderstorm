/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_THEME_SELECTION_H
#define FS_SESSION_THEME_SELECTION_H
#include <optional>
#include <string>

namespace fs_session
{
struct HostThemeReference
{
    std::string id,revision;
    bool valid() const
    {
        const auto hex=[](char ch){return ch>='0' && ch<='9' || ch>='a' && ch<='f';};
        if(id.size()!=36 || revision.size()!=64)return false;
        for(std::size_t i=0;i<id.size();++i)if((i==8 || i==13 || i==18 || i==23)?id[i]!='-':!hex(id[i]))return false;
        for(const auto ch:revision)if(!hex(ch))return false;
        return true;
    }
    bool operator==(const HostThemeReference& other) const {return id==other.id && revision==other.revision;}
    bool operator!=(const HostThemeReference& other) const {return !(*this==other);}
};
struct HostThemeSelection
{
    std::optional<HostThemeReference> current,previous;
    bool empty() const {return !current && !previous;}
    std::string encode() const
    {
        if(current && !current->valid() || previous && !previous->valid())return {};
        std::string bytes="FHT1";bytes.push_back(static_cast<char>((current?1:0)|(previous?2:0)));
        for(const auto* choice:{&current,&previous})if(*choice){bytes+=(*choice)->id;bytes+=(*choice)->revision;}
        return bytes;
    }
    static bool decode(const std::string& bytes,HostThemeSelection& output)
    {
        if(bytes.size()<5 || bytes.size()>205 || bytes.compare(0,4,"FHT1")!=0)return false;
        const auto flags=static_cast<unsigned char>(bytes[4]);if(flags>3 || bytes.size()!=5u+100u*((flags&1u?1u:0u)+(flags&2u?1u:0u)))return false;
        HostThemeSelection next;std::size_t at=5;
        for(unsigned int i=0;i<2;++i)if(flags&(1u<<i))
        {
            HostThemeReference value{bytes.substr(at,36),bytes.substr(at+36,64)};if(!value.valid())return false;at+=100;
            (i?next.previous:next.current)=std::move(value);
        }
        output=std::move(next);return true;
    }
};
}
#endif
