/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemepackage.h"
#include <iostream>
int main(int count,char** args)
{
    try
    {
        if(count==3 && std::string(args[1])=="host-palette")
        {
            const auto palette=fs_theme::hostPalette(fs_theme::readFile(fs_theme::fromUtf8(args[2]),4096));boost::json::object colors;
            for(const auto& entry:palette.colors)colors[entry.first]=entry.second;
            std::cout<<"valid "<<boost::json::serialize(colors)<<'\n';
        }
        else if(count==3 && std::string(args[1])=="capture-contract")
        {
            const auto fixture=fs_theme::parse(fs_theme::readFile(fs_theme::fromUtf8(args[2]))).as_object();
            const auto request=fs_theme::captureRequest(boost::json::serialize(fixture.at("request")),std::string(fixture.at("request_id").as_string()),fixture.at("generation").as_int64(),std::string(fixture.at("revision").as_string()));
            std::cout<<"valid "<<request.id<<'\n';
        }
        else if(count==3 && std::string(args[1])=="manifest")
        {const auto manifest=fs_theme::manifest(fs_theme::readFile(fs_theme::fromUtf8(args[2])));std::cout<<"valid "<<manifest.id<<'\n';}
        else if(count==4 && std::string(args[1])=="revision")
        {const auto revision=fs_theme::load(fs_theme::fromUtf8(args[2]),fs_theme::fromUtf8(args[3]));std::cout<<"valid "<<revision.revision<<'\n';}
        else {std::cerr<<"theme_validate manifest file | revision directory base\n";return 2;}
        return 0;
    }
    catch(const fs_theme::Error& error){std::cerr<<(count>1 && std::string(args[1])=="revision"?error.what():error.code)<<'\n';return 2;}
    catch(const std::exception& error){std::cerr<<"input: "<<error.what()<<'\n';return 2;}
}
