/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_PROFILE_SEED_H
#define FS_SESSION_PROFILE_SEED_H
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace fs_session
{
constexpr std::size_t ProfileSettingsLimit = 4u * 1024u * 1024u;
constexpr std::size_t ProfileSettingsEntries = 8192;
// Reuse the native backup eligibility, with extra exclusions for a different
// isolated profile. Never import paths, logins, account objects or local services.
inline bool seedPreference(const std::string& name, const std::string& type,
    bool persisted, bool backupable, bool sourceBackupable)
{
    if (!persisted || !backupable || !sourceBackupable || name.empty() || name.size() > 128) return false;
    const std::array<const char*,11> types{"Boolean","S32","U32","F32","String","Vector3","Vector3D","Color3","Color4","Color4U","Rect"};
    if (std::find(types.begin(),types.end(),type) == types.end()) return false;
    std::string lower = name;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const char* excluded[] = {"login","password","credential","token","secret","cookie","cache","path","directory","folder",
        "settingsfile","account","uuid","assistant","mcp","eventapi","bridge","voice","chatlog","imlog","logfile",
        "firstname","lastname","firstlocal","lastlocal","firstscreen","lastscreen","workspace","gestureboard","snapshot",
        "location","history","grid","remember","trusted","certificate","auth","apikey","webhook","proxy","socks"};
    for (const auto* part : excluded) if (lower.find(part) != std::string::npos) return false;
    return true;
}
inline const std::array<const char*,4>& seedUiFiles()
{
    static const std::array<const char*,4> files{"colors.xml","key_bindings.xml","quick_preferences.xml","ignorable_dialogs.xml"};
    return files;
}
}
#endif
