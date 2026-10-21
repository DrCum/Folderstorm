/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace fs_install
{
constexpr std::uint32_t AllShortcuts=15, SettingsVersion=0x46530100;
inline std::uint32_t encode(std::uint32_t choices){return SettingsVersion|(choices&AllShortcuts);}
inline std::optional<std::uint32_t> decode(std::uint32_t value)
{return (value&~AllShortcuts)==SettingsVersion?std::optional<std::uint32_t>(value&AllShortcuts):std::nullopt;}
enum class LinkAction {Keep,Write,Remove,Conflict};
inline LinkAction action(bool selected,bool exists,bool owned)
{
    if(exists && !owned)return selected?LinkAction::Conflict:LinkAction::Keep;
    return selected?LinkAction::Write:exists?LinkAction::Remove:LinkAction::Keep;
}
inline bool legacyRegistrationOwned(std::wstring name,bool sameUninstallCommand)
{
    for(auto& c:name)if(c>=L'A'&&c<=L'Z')c=static_cast<wchar_t>(c+(L'a'-L'A'));
    return sameUninstallCommand && name.rfind(L"vlpkfolderstorm",0)==0;
}
inline bool simpleExeName(const std::string& name)
{
    if(name.size()<5 || name.size()>184 || name.substr(name.size()-4)!=".exe")return false;
    for(const unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
    return true;
}
}
