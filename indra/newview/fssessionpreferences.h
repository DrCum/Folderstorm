/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_PREFERENCES_H
#define FS_SESSION_PREFERENCES_H
#include "fssessionhostoptions.h"
#include "fssessionchatprefs.h"
#include "fssessionchatmodel.h"
#include "fssessionexperience.h"
#include "fssessionthemeselection.h"
#include <vector>
namespace fs_session
{
// One transaction for related choices. No transcripts, drafts or credentials.
struct HostPreferences
{
    static constexpr std::size_t MaxBytes=256u*1024u;
    HostOptions options;
    PresentationStore presentation;
    HostExperience experience;
    ChatPreferences chat;
    HostThemeSelection hostTheme;
    std::string encode() const
    {
        std::vector<std::string> parts{options.encode(),presentation.encode(),experience.encode(),chat.encode()};
        HostOptions checked;
        if(!HostOptions::decode(parts[0],checked) || bindingsConflict(experience,presentation))return {};
        const auto theme=hostTheme.encode();if(theme.empty())return {};
        const bool custom=!hostTheme.empty();if(custom)parts.push_back(theme);
        std::string result=custom?"FHP2":"FHP1";
        for(const auto& part:parts)
        {
            if(part.empty())return {};
            const auto size=static_cast<std::uint32_t>(part.size());
            for(unsigned int i=0;i<4;++i)result.push_back(static_cast<char>((size>>(8*i))&255));
            result+=part;
        }
        return result.size()<=MaxBytes?result:std::string{};
    }
    static bool decode(const std::string& data,HostPreferences& output)
    {
        if(data.size()<20 || data.size()>MaxBytes)return false;
        const bool custom=data.compare(0,4,"FHP2")==0;
        if(!custom && data.compare(0,4,"FHP1")!=0)return false;
        std::vector<std::string> parts(custom?5:4);std::size_t at=4;
        for(auto& part:parts)
        {
            if(data.size()-at<4)return false;
            std::uint32_t size=0;for(unsigned int i=0;i<4;++i)size|=static_cast<std::uint32_t>(static_cast<unsigned char>(data[at++]))<<(8*i);
            if(!size || size>65536 || size>data.size()-at)return false;
            part=data.substr(at,size);at+=size;
        }
        HostPreferences next;
        if(at!=data.size() || !HostOptions::decode(parts[0],next.options) || !PresentationStore::decode(parts[1],next.presentation) ||
            !HostExperience::decode(parts[2],next.experience) || !ChatPreferences::decode(parts[3],next.chat) ||
            custom && !HostThemeSelection::decode(parts[4],next.hostTheme) ||
            bindingsConflict(next.experience,next.presentation))return false;
        output=std::move(next);return true;
    }
};
// Compare with the accepted baseline, not the current login in the profile slot.
// Unedited accounts must not gate global choices or consume bounded records.
struct AccountPreferenceDraft
{
    CharacterAppearance originalAppearance,appearance;
    CharacterSound originalSound,sound;
    bool importedWav=false;
    bool aliasChanged() const {return appearance.alias!=originalAppearance.alias;}
    bool appearanceChanged() const {return aliasChanged() || appearance.color!=originalAppearance.color;}
    bool soundChanged() const
    {return importedWav || sound.categories!=originalSound.categories || sound.asset!=originalSound.asset || sound.name!=originalSound.name;}
    bool edited() const {return appearanceChanged() || soundChanged();}
    bool apply(HostPreferences& choices,const AccountKey& key,bool owns) const
    {
        if(!edited())return true;
        if(!owns || !key.valid())return false;
        auto next=choices;
        if(appearanceChanged())
        {
            auto changed=next.presentation.appearance(key);
            if(aliasChanged())changed.alias=appearance.alias;
            if(appearance.color!=originalAppearance.color)changed.color=appearance.color;
            if(!next.presentation.set(key,changed))return false;
        }
        if(soundChanged())
        {
            auto changed=sound;if(importedWav)changed.asset.clear(); // Assigned after validated asset creation.
            next.chat.sounds[key]=std::move(changed);
            if(!next.chat.valid())return false;
        }
        choices=std::move(next);return true;
    }
    std::string profileLabel(const std::string& captured,const std::string& requested,
        const std::string& current,const std::string& accountName) const
    {
        const auto native=boundedText(accountName,64);
        if(!aliasChanged() || requested!=captured || current!=captured ||
            (!captured.empty() && captured!=native && captured!=originalAppearance.alias))return current;
        return appearance.alias.empty()?native:appearance.alias;
    }
};
class ChoiceAutosave
{
public:
    std::string saved,observed;
    std::uint64_t changedAt=0,retryAt=0;
    void observe(const std::string& value,std::uint64_t now)
    {
        if(value!=observed){observed=value;changedAt=now;}
    }
    bool dirty() const {return !observed.empty() && observed!=saved;}
    bool due(std::uint64_t now) const {return dirty() && now>=retryAt && now>=changedAt && now-changedAt>=750;}
    void committed(const std::string& value){saved=observed=value;retryAt=0;}
    void failed(std::uint64_t now){retryAt=now+30000;}
};
}
#endif
