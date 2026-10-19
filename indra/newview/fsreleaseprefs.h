/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_RELEASE_PREFS_H
#define FS_RELEASE_PREFS_H
#include "fsreleasepolicy.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif
namespace fs_release
{
inline std::uint64_t now()
{return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
inline std::filesystem::path preferencesPath()
{
#if defined(_WIN32)
    wchar_t* local=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)) || !local)return {};
    const auto result=std::filesystem::path(local)/L"FolderstormSessions"/L"Updates-v1"/L"preferences.dat";
    CoTaskMemFree(local);return result;
#else
    const auto* config=std::getenv("XDG_CONFIG_HOME");const auto* home=std::getenv("HOME");
    if(config && *config)return std::filesystem::path(config)/"folderstorm"/"updates-v1"/"preferences.dat";
    return home && *home?std::filesystem::path(home)/".config"/"folderstorm"/"updates-v1"/"preferences.dat":std::filesystem::path{};
#endif
}
inline Preferences loadPreferences(const std::string& defaultChannel)
{
    Preferences result;result.channel=defaultChannel;
    const auto path=preferencesPath();if(path.empty())return result;
    std::ifstream file(path,std::ios::binary);std::string bytes(257,'\0');
    if(file){file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(file.gcount()));Preferences::decode(bytes,result);}
    return result;
}
inline bool savePreferences(const Preferences& value)
{
    const auto path=preferencesPath();if(path.empty() || !channelValid(value.channel) || (!value.skipped.empty() && !Version::parse(value.skipped)))return false;
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
#if defined(_WIN32)
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    auto temporary=path;temporary+=".tmp."+std::to_string(pid);
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);const auto bytes=value.encode();
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();if(!file)return false;
#if defined(_WIN32)
    const bool saved=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!saved)std::filesystem::remove(temporary,error);
    return saved;
#else
    std::filesystem::rename(temporary,path,error);return !error;
#endif
}
}
#endif
