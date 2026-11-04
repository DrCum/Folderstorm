/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemeeditor.h"
#include <fstream>
#include <iostream>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif
namespace
{
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int count,char** args)
{
    try
    {
        check(count==2,"Pass a new fixture directory");const auto root=std::filesystem::path(args[1]);
        check(!std::filesystem::exists(root),"Fixture directory already exists");
        const auto base=root/L"Viewer folder",viewer=base/L"Viewer.exe",host=base/L"Host.exe";
        std::filesystem::create_directories(base);
        try{fs_theme::editorLaunch(base,viewer,host,false);throw std::runtime_error("Missing editor was accepted");}catch(const std::runtime_error& error){check(std::string(error.what()).find("not included")!=std::string::npos,"Wrong missing-editor diagnostic");}
        const auto editor=base/L"theme-editor"/L"Folderstorm.Theme.Editor.exe";std::filesystem::create_directories(editor.parent_path());std::ofstream(editor).put('x');std::ofstream(host).put('x');
        const auto command=fs_theme::editorLaunch(base,viewer,host,true);check(command.executable==std::filesystem::absolute(editor),"Wrong editor path");
        check(command.arguments.find(L"--library")!=std::wstring::npos && command.arguments.find(L"--host")!=std::wstring::npos,"Library/host arguments missing");
#if defined(_WIN32)
        for(const auto& value:{std::wstring(L"C:\\Folder with spaces\\"),std::wstring(L"C:\\"),std::wstring(L"embedded\"quote\\"),std::wstring(L"")})
        {
            const auto text=L"probe.exe "+fs_theme::quoteEditorArgument(value);int size=0;auto** parsed=CommandLineToArgvW(text.c_str(),&size);
            check(parsed && size==2 && std::wstring(parsed[1])==value,"Windows argument quoting changed content");LocalFree(parsed);
        }
        const auto text=L"probe.exe "+command.arguments;int size=0;auto** parsed=CommandLineToArgvW(text.c_str(),&size);
        check(parsed && size==8 && std::wstring(parsed[1])==L"--base" && std::wstring(parsed[2])==std::filesystem::absolute(base).wstring() && std::wstring(parsed[7])==L"--library","Editor command did not preserve its exact argument vector");LocalFree(parsed);
#endif
        std::cout<<"PASS optional editor absence/path discovery and exact Windows argument quoting without launching an app\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
