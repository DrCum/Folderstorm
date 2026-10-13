/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CHAT_PREFS_H
#define FS_SESSION_CHAT_PREFS_H
#include "fssessionexperience.h"
#include "fssessioncategories.h"
namespace fs_session
{
enum SoundCategory : unsigned int { SoundPrivate = 1, SoundGroup = 2, SoundConference = 4, SoundAttention = 8 };
inline unsigned int legacySoundMask(unsigned int policy) { return policy == 0 ? 0u : policy == 1 ? 7u : 15u; }
inline unsigned int soundCategory(const Message& event)
{
    if (event.eventType == EventType::Attention) return SoundAttention;
    if (event.eventType != EventType::Chat) return 0;
    return event.topic == Topic::Private ? SoundPrivate : event.topic == Topic::Group ? SoundGroup :
        event.topic == Topic::Conference ? SoundConference : 0u;
}
struct CharacterSound
{
    unsigned int categories = 0;
    std::string asset;
    bool valid() const
    {
        if (categories > 15) return false;
        if (asset.empty()) return true;
        return asset.size() == 36 && asset.substr(32) == ".wav" && asset.substr(0,32).find_first_not_of("0123456789abcdef") == std::string::npos;
    }
};
// Additive store: FSP1/FSX1 remain readable and unchanged. Missing accounts
// inherit their legacy alert policy until explicitly customized.
struct ChatPreferences
{
    static constexpr std::size_t MaxBytes = 65536;
    bool visibleRead = true, activeRead = false;
    unsigned int delay = 60;
    unsigned int showMask=ChatCategories, attentionMask=legacyAttentionCategories(29);
    bool haveCategories=false;
    std::array<std::string,5> profileNames{};
    std::map<AccountKey,CharacterSound> sounds;
    CharacterSound sound(const AccountKey& key,unsigned int legacy) const
    { const auto found = sounds.find(key); return found == sounds.end() ? CharacterSound{legacySoundMask(legacy),{}} : found->second; }
    bool valid() const
    {
        if (delay < 5 || delay > 3600 || sounds.size() > PresentationStore::MaxProfiles || showMask>ChatCategories || attentionMask>AllCategories) return false;
        for(const auto& name:profileNames) if(name.size()>64 || !validUtf8(name)) return false;
        for (const auto& entry : sounds) if (!entry.first.valid() || !entry.second.valid()) return false;
        return true;
    }
    std::string encode() const
    {
        if (!valid()) return {};
        StoreWriter writer("FSC3"); writer.number(visibleRead); writer.number(activeRead); writer.number(delay);
        writer.number(showMask);writer.number(attentionMask);
        for(const auto& name:profileNames)writer.text(name);
        writer.number(static_cast<std::uint32_t>(sounds.size()));
        for (const auto& entry : sounds)
        { writer.text(entry.first.account); writer.text(entry.first.grid); writer.number(entry.second.categories); writer.text(entry.second.asset); }
        return writer.data.size() <= MaxBytes ? writer.data : std::string{};
    }
    static bool decode(const std::string& data,ChatPreferences& output)
    {
        const bool categories=data.substr(0,4)=="FSC3";
        StoreReader reader(data,categories?"FSC3":"FSC2",MaxBytes); ChatPreferences next;
        const auto visible = reader.number(), active = reader.number();
        if (visible > 1 || active > 1) return false;
        next.visibleRead = visible != 0; next.activeRead = active != 0; next.delay = reader.number();
        next.haveCategories=categories;
        if(categories){next.showMask=reader.number();next.attentionMask=reader.number();for(auto& name:next.profileNames)name=reader.text(64);}
        const auto count = reader.number(); if (!reader.ok || count > PresentationStore::MaxProfiles) return false;
        for (std::uint32_t i = 0; i < count && reader.ok; ++i)
        {
            AccountKey key{reader.text(36),reader.text(127)};
            CharacterSound choice{reader.number(),reader.text(36)};
            if (!next.sounds.emplace(key,choice).second) return false;
        }
        if (!reader.done() || !next.valid()) return false;
        output = std::move(next); return true;
    }
};
}
#endif
