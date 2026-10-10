/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_STORE_CODEC_H
#define FS_SESSION_STORE_CODEC_H
#include "fssessionpresentation.h"
namespace fs_session
{
struct StoreWriter
{
    std::string data;
    explicit StoreWriter(const char* magic) : data(magic) {}
    void number(std::uint32_t n) { for (int i=0;i<4;++i) data.push_back(static_cast<char>((n>>(8*i))&255)); }
    void text(const std::string& s) { number(static_cast<std::uint32_t>(s.size())); data+=s; }
    void rect(const ShellRect& r) { number(static_cast<std::uint32_t>(r.x)); number(static_cast<std::uint32_t>(r.y)); number(static_cast<std::uint32_t>(r.width)); number(static_cast<std::uint32_t>(r.height)); number(r.dpi); }
};
struct StoreReader
{
    const std::string& data; std::size_t at=4; bool ok=true;
    StoreReader(const std::string& s,const char* magic,std::size_t limit) : data(s) { ok=s.size()>=4 && s.size()<=limit && s.compare(0,4,magic)==0; }
    std::uint32_t number()
    {
        if (!ok || at>data.size() || data.size()-at<4) { ok=false; return 0; }
        std::uint32_t n=0; for(int i=0;i<4;++i) n|=static_cast<std::uint32_t>(static_cast<unsigned char>(data[at++]))<<(8*i); return n;
    }
    int signedNumber() { const auto n=number(); return n<=0x7fffffffu ? static_cast<int>(n) : -1-static_cast<int>(0xffffffffu-n); }
    std::string text(std::size_t limit)
    {
        const auto n=number(); if(!ok || n>limit || n>data.size()-at) { ok=false; return {}; }
        auto s=data.substr(at,n); at+=n; if(!validUtf8(s) || s.find('\0')!=std::string::npos) ok=false; return s;
    }
    ShellRect rect() { ShellRect r; r.x=signedNumber(); r.y=signedNumber(); r.width=signedNumber(); r.height=signedNumber(); r.dpi=number(); return r; }
    bool done() const { return ok && at==data.size(); }
};
}
#endif
