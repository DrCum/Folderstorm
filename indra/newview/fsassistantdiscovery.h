/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_DISCOVERY_H
#define FS_ASSISTANT_DISCOVERY_H
#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
namespace fs_assistant
{
inline bool protectDiscoveryPath(const std::wstring& path)
{
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<unsigned char> user(size);
    const bool read=size && GetTokenInformation(token,TokenUser,user.data(),size,&size);
    CloseHandle(token);if(!read)return false;
    wchar_t* sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid))return false;
    const std::wstring sddl=L"D:P(A;OICI;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
    const bool secured=SetFileSecurityW(path.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,descriptor)!=FALSE;
    LocalFree(descriptor);return secured;
}
inline std::wstring managedDiscoveryDirectory()
{
    PWSTR local=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return {};
    std::filesystem::path path(local);CoTaskMemFree(local);
    for(const auto* part:{L"FolderstormSessions",L"Prototype-v1",L"mcp-discovery"})
    {
        path/=part;std::error_code error;std::filesystem::create_directory(path,error);
        const DWORD attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return {};
    }
    return protectDiscoveryPath(path.wstring())?path.wstring():std::wstring{};
}
}
#endif
#endif
