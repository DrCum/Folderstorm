/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantassetfetch.h"
#include <cassert>
using namespace fs_assistant;
int main()
{
    const AssetReadSource original{"account","login","grid","A",1,true};
    for(int replacement=0;replacement<7;++replacement)
    {
        bool permission=true;int submissions=0;
        auto guard=std::make_shared<AsyncOperation>([&](){return permission?std::string():std::string("revoked");});
        auto task=std::make_shared<AsyncTask>(guard);AssetReadAuthority authority(original,task);guard->seal();
        assert(authority.allowed(original) && guard->pending()==1);
        auto current=original;
        switch(replacement)
        {
            case 0:permission=false;break;
            case 1:current.account="other";break;
            case 2:current.login="next";break;
            case 3:current.grid="other-grid";break;
            case 4:current.region="B";break;
            case 5:current.regionGeneration+=2;break; // A -> B -> A while waiting for caps
            case 6:current.connected=false;break;
        }
        // This is the same authority object retained by the queued coroutine,
        // checked again after waits and immediately at each HTTP/UDP boundary.
        if(authority.allowed(current))++submissions;
        assert(submissions==0);
        permission=true;
        assert(!authority.allowed(original)); // revocation/source replacement is sticky
        assert(guard->pending()==1); // task lives through suspension independently of TLS
    }
    auto guard=std::make_shared<AsyncOperation>([](){return std::string();});
    auto task=std::make_shared<AsyncTask>(guard);AssetReadAuthority authority(original,task);
    bool allowedUnderScope=false;
    {AsyncScope other(std::make_shared<AsyncOperation>([](){return std::string("unrelated scope");}));allowedUnderScope=authority.allowed(original);}
    assert(allowedUnderScope); // unrelated coroutine TLS cannot replace captured authority
}
