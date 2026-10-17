/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include "fsreleasepolicy.h"
namespace fs_update
{
inline std::string packageVersion(const std::string& version)
{
    if(!fs_release::Version::parse(version))return {};
    const auto dot=version.find_last_of('.');return version.substr(0,dot)+"-"+version.substr(dot+1);
}
struct Barrier
{
    std::string source,target,commit;
    std::string encode()const{return "1\n"+source+"\n"+target+"\n"+commit+"\n";}
    static std::optional<Barrier> decode(const std::string& bytes)
    {
        if(bytes.size()>240)return {};
        std::istringstream input(bytes);std::string schema,extra;Barrier result;
        if(!std::getline(input,schema)||!std::getline(input,result.source)||!std::getline(input,result.target)||!std::getline(input,result.commit)||std::getline(input,extra)||schema!="1")return {};
        const auto source=fs_release::Version::parse(result.source),target=fs_release::Version::parse(result.target);
        if(!source || !target || !(*source<*target) || !fs_release::hex(result.commit,40))return {};
        return result;
    }
};
enum class Phase{Ready,Waiting,Applying,Finished,Cancelled};
struct Coordinator
{
    Phase phase=Phase::Ready;bool requested=false;
    void request(){if(phase==Phase::Ready){requested=true;phase=Phase::Waiting;}}
    bool mayApply(bool leaseExclusive,bool processesIdle,bool sameInstallation,bool sameVersion)const
    {return requested && phase==Phase::Waiting && leaseExclusive && processesIdle && sameInstallation && sameVersion;}
    void applying(){if(phase==Phase::Waiting)phase=Phase::Applying;}
    void cancel(){if(phase!=Phase::Applying && phase!=Phase::Finished){phase=Phase::Cancelled;requested=false;}}
};
}
