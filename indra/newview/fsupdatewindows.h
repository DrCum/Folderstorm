/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <array>
#include "fsupdatepolicy.h"
namespace fs_update
{
struct Handle
{
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle=INVALID_HANDLE_VALUE):value(handle){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE && value)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    explicit operator bool()const{return value!=INVALID_HANDLE_VALUE && value;}
};
inline std::filesystem::path binaryPath()
{
    std::array<wchar_t,32768> bytes{};const auto size=GetModuleFileNameW(nullptr,bytes.data(),static_cast<DWORD>(bytes.size()));
    return size && size<bytes.size()?std::filesystem::path(std::wstring(bytes.data(),size)):std::filesystem::path{};
}
inline bool regular(const std::filesystem::path& path)
{
    const auto attrs=GetFileAttributesW(path.c_str());
    return attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
inline bool under(const std::filesystem::path& file,const std::filesystem::path& root)
{
    const auto name=file.wstring(),prefix=root.wstring()+L"\\";
    return name.size()>prefix.size() && CompareStringOrdinal(name.c_str(),static_cast<int>(prefix.size()),prefix.c_str(),static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL;
}
inline std::filesystem::path installation(const std::filesystem::path& binary)
{
    std::error_code error;const auto current=std::filesystem::canonical(binary.parent_path(),error);
    if(error || current.filename()!=L"current" || !regular(current/L"sq.version") || !regular(current/L"folderstorm-bundle.json"))return {};
    const auto root=current.parent_path();
    return regular(root/L"Update.exe")?root:std::filesystem::path{};
}
// Exact full image paths, not process names/PIDs alone. Access-denied inspection
// fails closed. Toolhelp includes sidecars, detached viewers and Crashpad.
inline bool idle(const std::filesystem::path& root,bool updaterOnly=false)
{
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(!snapshot)return false;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(!Process32FirstW(snapshot.value,&entry))return false;
    do
    {
        if(!entry.th32ProcessID || entry.th32ProcessID==GetCurrentProcessId() || entry.th32ProcessID==4)continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID));
        if(!process)
        {
            // Protected OS processes cannot run from a user-owned installation.
            // All matching app names must remain inspectable to proceed.
            const std::wstring name=entry.szExeFile;
            if(name.find(L"Folderstorm")!=std::wstring::npos || name==L"fs-mcp.exe" || name==L"Update.exe" || name==L"folderstorm-crashpad-handler.exe")return false;
            continue;
        }
        std::array<wchar_t,32768> path{};DWORD size=static_cast<DWORD>(path.size());
        if(!QueryFullProcessImageNameW(process.value,0,path.data(),&size))return false;
        const auto image=std::filesystem::path(std::wstring(path.data(),size));
        if(under(image,root) && (!updaterOnly || image.filename()==L"Update.exe"))return false;
    }while(Process32NextW(snapshot.value,&entry));
    return GetLastError()==ERROR_NO_MORE_FILES;
}
inline std::optional<Barrier> readBarrier(const std::filesystem::path& root)
{
    std::ifstream file(root/L".folderstorm-update-pending",std::ios::binary);std::string bytes(241,'\0');
    if(!file)return {};
    file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));return Barrier::decode(bytes);
}
inline bool writeBarrier(const std::filesystem::path& root,const Barrier& barrier)
{
    if(!Barrier::decode(barrier.encode()))return false;
    const auto temporary=root/L".folderstorm-update-pending.tmp";
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);const auto bytes=barrier.encode();
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();
    return file && MoveFileExW(temporary.c_str(),(root/L".folderstorm-update-pending").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
}
#endif
