/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_preview.h"
#include <iostream>
#include <functional>

namespace
{
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void reject(const std::function<void()>& action)
{try{action();}catch(const fs_theme::Error&){return;}throw std::runtime_error("Invalid preview request accepted");}
}
int main()
{
    try
    {
        const std::string nonce(64,'a'),revision(64,'b');
        boost::json::object request{{"host_preview_version",1},{"request_id",nonce},{"generation",1},{"theme_id",""},{"revision",""},{"baseline",0}};
        const auto parse=[&]{return fs_host_theme::previewRequest(boost::json::serialize(request));};
        check(!parse().choice && parse().baseline==0,"Built-in request failed");request["baseline"]=1;check(parse().baseline==1,"Daylight request failed");
        request["theme_id"]="11111111-2222-3333-4444-555555555555";request["revision"]=revision;check(parse().choice->revision==revision,"Custom request failed");
        for(const auto key:{"host_preview_version","request_id","generation","theme_id","revision","baseline"}){auto saved=request.at(key);request.erase(key);reject([&]{parse();});request[key]=std::move(saved);}
        request["extra"]=true;reject([&]{parse();});request.erase("extra");
        const auto invalid=[&](const char* key,boost::json::value replacement){auto saved=request.at(key);request[key]=std::move(replacement);reject([&]{parse();});request[key]=std::move(saved);};
        invalid("host_preview_version",2);invalid("generation",0);invalid("baseline",2);invalid("baseline",1.0);
        invalid("request_id","AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");invalid("theme_id","../escape");invalid("revision","../escape");
        reject([&]{fs_host_theme::previewRequest(std::string(4097,' '));});
        auto duplicate=boost::json::serialize(request);duplicate.insert(1,"\"baseline\":0,");reject([&]{fs_host_theme::previewRequest(duplicate);});
        std::cout<<"PASS strict native host preview owner, baseline, immutable choice, bounds, unknown/missing/duplicate fields and version checks\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
