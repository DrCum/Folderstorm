/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <string>
namespace fs_crash
{
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
