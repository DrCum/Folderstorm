/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <optional>
#include <string>
namespace fs_crash
{
// Open the public tracker without automatically adding account or build data.
inline const std::string BugReportUrl="https://folderstorm.com/bugs";
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
template<class Read,class Ask,class Save>
bool offerConsentOnce(bool available,const std::string& role,Read read,Ask ask,Save save)
{
    if(!available || (role!="viewer" && role!="host"))return true;
    const auto original=read();
    // A valid saved choice, including an explicit refusal, predates this prompt
    // and must never be replaced. Missing/corrupt consent still defaults to Off.
    if(original.enabled || !original.epoch.empty())return true;
    const std::optional<bool> choice=ask();
    if(!choice)return true; // A dialog that could not open is not a decision.
    if(!(read()==original))return true; // Respect settings changed while open.
    return save(*choice);
}
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
