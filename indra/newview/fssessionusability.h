/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_USABILITY_H
#define FS_SESSION_USABILITY_H
#include "fssessionchatmodel.h"
#include "fssessionpresentation.h"
namespace fs_session
{
struct ChatPin
{
    AccountKey owner;
    ChatKey source;
    std::string conversation, destination, title;
    Topic topic = Topic::Nearby;
    SavedPin saved() const { return {owner,topic,destination}; }
    bool live(const ChatView& view) const
    { return source.worker == view.identity.worker && source.generation == view.identity.generation && source.account == view.identity.account && source.grid == view.identity.grid && view.conversations.count(conversation) && !view.conversations.at(conversation).restricted; }
};
class PinBook
{
public:
    std::vector<ChatPin> entries;
    int find(const ChatView& view, const std::string& id) const
    {
        for (std::size_t i = 0; i < entries.size(); ++i) if (entries[i].conversation == id && entries[i].live(view)) return static_cast<int>(i);
        return -1;
    }
    bool add(const ChatView& view,const std::string& id)
    {
        const auto found = view.conversations.find(id); if (found == view.conversations.end() || found->second.restricted || found->second.topic == Topic::Notice || find(view,id) >= 0 || entries.size() >= PresentationStore::MaxPins) return false;
        const AccountKey owner{view.identity.account,view.identity.grid}; if (!owner.valid()) return false;
        unsigned int count = 0; for (const auto& pin : entries) if (pin.owner == owner) ++count;
        if (count >= 8) return false;
        const auto& conversation = found->second;
        entries.push_back({owner,view.identity,id,conversation.destination,conversation.title,conversation.topic}); return true;
    }
    bool move(std::size_t index,int direction)
    {
        if (index >= entries.size() || (direction != -1 && direction != 1)) return false;
        const auto target = static_cast<int>(index)+direction;
        if (target < 0 || static_cast<std::size_t>(target) >= entries.size()) return false;
        std::swap(entries[index],entries[static_cast<std::size_t>(target)]); return true;
    }
    void restore(const std::vector<SavedPin>& pins)
    { entries.clear(); for (const auto& pin : pins) if (pin.valid() && entries.size() < PresentationStore::MaxPins) entries.push_back({pin.owner,{},"",pin.destination,"Unavailable",pin.topic}); }
    std::vector<SavedPin> saved() const
    {
        std::vector<SavedPin> result;
        for (const auto& pin : entries) if (pin.saved().valid() && std::find(result.begin(),result.end(),pin.saved()) == result.end()) result.push_back(pin.saved());
        return result;
    }
    void bind(const ChatView& view,bool restricted)
    {
        const AccountKey owner{view.identity.account,view.identity.grid};
        for (auto& pin : entries) if (pin.owner == owner)
        {
            if (restricted) { pin.source = {}; pin.title = "Restricted"; continue; }
            const auto current = view.conversations.find(pin.conversation);
            if (current != view.conversations.end() && current->second.restricted) { pin.source = {}; pin.title = "Restricted"; continue; }
            if (pin.live(view))
            { const auto& liveConversation = view.conversations.at(pin.conversation); pin.title = liveConversation.title; pin.destination = liveConversation.destination; continue; }
            pin.source = {}; pin.title = "Unavailable";
            if (!pin.saved().valid()) { pin.title = "Unavailable"; continue; }
            for (const auto& entry : view.conversations) if (!entry.second.restricted && entry.second.topic == pin.topic && entry.second.destination == pin.destination)
            { pin.source = view.identity; pin.conversation = entry.first; pin.title = entry.second.title; break; }
        }
    }
};
inline std::string attentionCategory(const std::string& name)
{
    if (name == "TeleportOffered" || name == "TeleportOffered_SLUrl" || name == "TeleportOffered_MaturityExceeded" || name == "TeleportOffered_MaturityExceeded_SLUrl" || name == "TeleportOffered_MaturityBlocked" || name == "TeleportOffered_MaturityBlocked_SLUrl") return "Teleport offer";
    if (name == "ScriptQuestion" || name == "ScriptQuestionCaution" || name == "ScriptQuestionExperience") return "Permission request";
    if (name == "UserGiveItem" || name == "UserGiveItemLegacy" || name == "ObjectGiveItem" || name == "OwnObjectGiveItem") return "Inventory offer";
    return {};
}
class AttentionBook
{
public:
    static constexpr std::size_t Capacity = 128;
    std::vector<Message> entries, dismissed;
    bool gap = false;
    static bool same(const Message& a,const Message& b)
    { return ChatKey{a.worker,a.generation,a.account,a.grid,{}}.owns(b) && a.conversation == b.conversation &&
        ((a.eventType == EventType::Attention || a.eventType == EventType::AttentionRemoved) == (b.eventType == EventType::Attention || b.eventType == EventType::AttentionRemoved)); }
    void forget(const ChatKey& key)
    {
        const auto owned = [&key](const Message& item) { return key.owns(item); };
        entries.erase(std::remove_if(entries.begin(),entries.end(),owned),entries.end());
        dismissed.erase(std::remove_if(dismissed.begin(),dismissed.end(),owned),dismissed.end());
    }
    void accept(const Message& event)
    {
        if (event.eventType == EventType::Gap) { forget({event.worker,event.generation,event.account,event.grid,{}}); gap = true; return; }
        if (event.eventType != EventType::Attention && event.eventType != EventType::AttentionRemoved) return;
        const auto match = [&event](const Message& item) { return same(item,event); };
        if (event.eventType == EventType::AttentionRemoved)
        {
            entries.erase(std::remove_if(entries.begin(),entries.end(),match),entries.end());
            dismissed.erase(std::remove_if(dismissed.begin(),dismissed.end(),match),dismissed.end()); return;
        }
        if (std::any_of(dismissed.begin(),dismissed.end(),match)) return;
        auto found = std::find_if(entries.begin(),entries.end(),match);
        if (found != entries.end()) *found = event;
        else { if (entries.size() >= Capacity) { entries.erase(entries.begin()); gap = true; } entries.push_back(event); }
    }
    bool hiddenUnread(const Message& item) const
    { return std::any_of(dismissed.begin(),dismissed.end(),[&item](const Message& old) { return same(old,item) && item.cursor <= old.cursor && item.unread == old.unread; }); }
    void dismiss(const Message& selected)
    {
        const auto match = [&selected](const Message& item) { return same(item,selected); };
        entries.erase(std::remove_if(entries.begin(),entries.end(),match),entries.end());
        if (!std::any_of(dismissed.begin(),dismissed.end(),match))
        { if (dismissed.size() >= Capacity) dismissed.erase(dismissed.begin()); Message key; key.worker = selected.worker; key.generation = selected.generation; key.account = selected.account; key.grid = selected.grid; key.conversation = selected.conversation; key.eventType = selected.eventType; key.cursor = selected.cursor; key.unread = selected.unread; dismissed.push_back(key); }
    }
};
}
#endif
