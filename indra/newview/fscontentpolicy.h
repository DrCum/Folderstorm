/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CONTENT_POLICY_H
#define FS_CONTENT_POLICY_H
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace fs_content {
// Strict UTF-8: reject NUL, overlong encodings, surrogates and out-of-range values.
inline bool utf8(const std::string& text,std::vector<std::uint32_t>* embedded=nullptr)
{
    for(std::size_t i=0;i<text.size();)
    {
        const auto first=static_cast<unsigned char>(text[i++]);
        std::uint32_t cp=first;unsigned tail=0;
        if(first==0)return false;
        if(first>=0xc2 && first<=0xdf){cp=first&31;tail=1;}
        else if(first>=0xe0 && first<=0xef){cp=first&15;tail=2;}
        else if(first>=0xf0 && first<=0xf4){cp=first&7;tail=3;}
        else if(first>=0x80)return false;
        if(i+tail>text.size())return false;
        const auto count=tail;
        while(tail--){const auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}
        if((count==1 && cp<0x80) || (count==2 && cp<0x800) || (count==3 && cp<0x10000) ||
           (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff)return false;
        if(embedded && cp>=0x100000)embedded->push_back(cp);
    }
    return true;
}
// Native LLNotecard parses embedded inventory; this verifies its exact byte
// extent, declared length and closing structure (which the native parser omits).
// Keep the original inventory prefix byte-for-byte, including ext-char indices.
inline bool notecardLayout(const std::string& raw,const std::string& text,std::size_t textEnd,
                           std::string& prefix,std::string& suffix,int version=2)
{
    if((version!=1 && version!=2) || raw.find('\0')!=std::string::npos || (version==2 && !utf8(text)) || textEnd<text.size() || textEnd>raw.size())return false;
    const auto start=textEnd-text.size();
    if(raw.compare(start,text.size(),text)!=0 || start==0)return false;
    const auto field=raw.rfind("\nText length ",start-1);
    if(field==std::string::npos)return false;
    const auto length=std::to_string(text.size());
    if(raw.substr(field+1,start-field-1)!="Text length "+length+"\n")return false;
    suffix=raw.substr(textEnd);
    if(suffix!="}\n" && suffix!="}")return false;
    if(raw.rfind("Linden text version "+std::to_string(version)+"\n{\n",0)!=0)return false;
    prefix=raw.substr(0,field+1);return true;
}
// Version 1 uses ASCII plus one-byte embedded-item indices, as native
// LLViewerTextEditor::setASCIIEmbeddedText does. Publish stable UTF-8 markers;
// keep the original serialized bytes for hashing. Editing legacy assets remains
// a separately unsupported format conversion.
inline bool notecardReadText(const std::string& source,int version,std::size_t itemCount,std::string& text)
{
    text.clear();
    if(version==2)
    {
        std::vector<std::uint32_t> markers;
        if(itemCount>65536 || !utf8(source,&markers))return false;
        for(const auto marker:markers)if(marker-0x100000>=itemCount)return false;
        text=source;return true;
    }
    if(version!=1 || itemCount>65536)return false;
    for(unsigned char byte:source)
    {
        if(!byte)return false;
        if(byte<0x80){text+=static_cast<char>(byte);continue;}
        const std::uint32_t index=byte-0x80;
        if(index>=itemCount)return false;
        const std::uint32_t cp=0x100000+index;
        text+=static_cast<char>(0xf0|(cp>>18));text+=static_cast<char>(0x80|((cp>>12)&63));
        text+=static_cast<char>(0x80|((cp>>6)&63));text+=static_cast<char>(0x80|(cp&63));
    }
    return true;
}
inline bool preserveEmbedded(const std::string& before,const std::string& after)
{
    std::vector<std::uint32_t> a,b;
    return utf8(before,&a) && utf8(after,&b) && a==b;
}
inline std::string replaceNotecard(const std::string& prefix,const std::string& suffix,const std::string& text)
{return prefix+"Text length "+std::to_string(text.size())+"\n"+text+suffix;}
inline std::string excerpt(const std::string& text,std::size_t limit)
{
    auto end=std::min(text.size(),limit);
    while(end<text.size() && end && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
    return text.substr(0,end);
}
struct Diff {std::size_t prefix=0,removed=0,added=0;std::string before,after;bool truncated=false;};
inline Diff diff(const std::string& before,const std::string& after,std::size_t limit=4096)
{
    Diff result;
    while(result.prefix<std::min(before.size(),after.size()) && before[result.prefix]==after[result.prefix])++result.prefix;
    while(result.prefix && result.prefix<before.size() && (static_cast<unsigned char>(before[result.prefix])&0xc0)==0x80)--result.prefix;
    std::size_t suffix=0;
    while(suffix<std::min(before.size(),after.size())-result.prefix && before[before.size()-suffix-1]==after[after.size()-suffix-1])++suffix;
    while(suffix && ((static_cast<unsigned char>(before[before.size()-suffix])&0xc0)==0x80 ||
                    (static_cast<unsigned char>(after[after.size()-suffix])&0xc0)==0x80))--suffix;
    result.removed=before.size()-result.prefix-suffix;result.added=after.size()-result.prefix-suffix;
    result.before=excerpt(before.substr(result.prefix,result.removed),limit/2);
    result.after=excerpt(after.substr(result.prefix,result.added),limit/2);
    result.truncated=result.before.size()<result.removed || result.after.size()<result.added;return result;
}
}
#endif
