/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <string>
namespace fs_crash
{
inline const std::string IssueUrl="https://github.com/DrCum/Folderstorm/issues/new";
inline std::string bugReportUrl(const std::string& environment)
{
    // A generic prefilled issue works before this branch's template is on main.
    const std::string body="### What happened?\n\n\n### Steps to reproduce\n\n\n### Environment\n"+environment;
    std::string encoded;
    constexpr char hex[]="0123456789ABCDEF";
    for(const unsigned char c:body)
    {
        if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~')encoded+=static_cast<char>(c);
        else{encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}
    }
    return IssueUrl+"?title=%5BBug%5D%20&body="+encoded;
}
struct Consent
{
    bool enabled=false;
    std::string epoch;
    std::string encode() const { return "1\n"+std::string(enabled?"1":"0")+"\n"+epoch+"\n"; }
    static Consent decode(const std::string& bytes)
    {
        if(bytes.size()!=37 || bytes.compare(0,2,"1\n") || (bytes[2]!='0' && bytes[2]!='1') || bytes[3]!='\n' || bytes.back()!='\n')return {};
        const auto epoch=bytes.substr(4,33-1);
        if(epoch.find_first_not_of("0123456789abcdef")!=std::string::npos)return {};
        return {bytes[2]=='1',epoch};
    }
    bool operator==(const Consent& other)const {return enabled==other.enabled && epoch==other.epoch;}
};
inline bool validRole(const std::string& role)
{return role=="host" || role=="worker" || role=="viewer";}
inline std::string environmentText(const std::string& version,const std::string& platform,
                                   const std::string& cpu,const std::string& role,const std::string& commit)
{
    // This deliberately accepts build metadata only, never account/location/logs.
    return "Folderstorm "+version+"\nPlatform: "+platform+"\nCPU build: "+cpu+"\nProcess: "+
        (validRole(role)?role:"viewer")+"\nSource: "+commit;
}
}
