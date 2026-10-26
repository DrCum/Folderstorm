/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemepackage.h"
#include <iostream>
int main(int count,char** args)
{
    try
    {
        if(count==3 && std::string(args[1])=="manifest")
        {const auto manifest=fs_theme::manifest(fs_theme::readFile(std::filesystem::u8path(args[2])));std::cout<<"valid "<<manifest.id<<'\n';}
        else if(count==4 && std::string(args[1])=="revision")
        {const auto revision=fs_theme::load(std::filesystem::u8path(args[2]),std::filesystem::u8path(args[3]));std::cout<<"valid "<<revision.revision<<'\n';}
        else {std::cerr<<"theme_validate manifest file | revision directory base\n";return 2;}
        return 0;
    }
    catch(const fs_theme::Error& error){std::cerr<<(count>1 && std::string(args[1])=="revision"?error.what():error.code)<<'\n';return 2;}
    catch(const std::exception& error){std::cerr<<"input: "<<error.what()<<'\n';return 2;}
}
