/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemeeditor.h"
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif
namespace fs_theme
{
std::wstring quoteEditorArgument(const std::wstring& value)
{
    std::wstring result=L"\"";std::size_t slashes=0;
    for(const auto ch:value)
    {
        if(ch==L'\\'){++slashes;continue;}
        result.append(ch==L'"'?slashes*2+1:slashes,L'\\');result+=ch;slashes=0;
    }
    result.append(slashes*2,L'\\');result+=L'"';return result;
}
EditorLaunch editorLaunch(const std::filesystem::path& base,const std::filesystem::path& viewer,const std::filesystem::path& host,bool library)
{
    EditorLaunch result;std::error_code error;
    for(const auto& folder:{base/L"theme-editor",base/L"Editor",base.parent_path()/L"Editor"})
    {
        const auto candidate=folder/L"Folderstorm.Theme.Editor.exe";
        if(std::filesystem::is_regular_file(candidate,error)){result.executable=std::filesystem::absolute(candidate);break;}error.clear();
    }
    if(result.executable.empty())throw std::runtime_error("Theme lab is not included in this build. Use a build with the optional theme editor component.");
    result.arguments=L"--base "+quoteEditorArgument(std::filesystem::absolute(base).wstring())+L" --viewer "+quoteEditorArgument(std::filesystem::absolute(viewer).wstring());
    if(!host.empty() && std::filesystem::is_regular_file(host,error))result.arguments+=L" --host "+quoteEditorArgument(std::filesystem::absolute(host).wstring());
    if(library)result.arguments+=L" --library";
    return result;
}
bool launchEditor(const std::filesystem::path& base,const std::filesystem::path& viewer,const std::filesystem::path& host,bool library,std::string& diagnostic)
{
#if defined(_WIN32)
    try
    {
        const auto command=editorLaunch(base,viewer,host,library);const auto directory=command.executable.parent_path();
        SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
        info.lpVerb=L"open";info.lpFile=command.executable.c_str();info.lpParameters=command.arguments.c_str();info.lpDirectory=directory.c_str();info.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&info)){diagnostic="Theme lab could not start. Check that the optional editor files are present.";return false;}
        if(info.hProcess)CloseHandle(info.hProcess);diagnostic.clear();return true;
    }
    catch(const std::exception& error){diagnostic=error.what();return false;}
#else
    (void)base;(void)viewer;(void)host;(void)library;diagnostic="Theme lab is available in Windows builds with the optional editor component.";return false;
#endif
}
}
