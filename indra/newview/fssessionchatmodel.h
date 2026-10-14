/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CHAT_MODEL_H
#define FS_SESSION_CHAT_MODEL_H
#include "fssessionprotocol.h"
#include <deque>
#include <map>
#include <limits>

namespace fs_session
{
// Session-local only. No combined transcript files or uncertain-send replay.
inline std::string boundedText(std::string text, std::size_t limit, bool multiline = false)
{
    for (auto& c : text)
        if ((static_cast<unsigned char>(c) < 32 && !(multiline && (c == '\n' || c == '\r' || c == '\t'))) || c == 127) c = ' ';
    if (text.size() > limit) text.resize(limit);
    while (!text.empty() && !validUtf8(text, multiline)) text.pop_back();
    return text;
}
// Used on the worker's queue-drain path, after enqueue-time restrictions.
// A group can become blocked while its individual sender remains permitted.
template<typename CanReceive>
inline bool filterChatEvent(Message& event,const std::string& self,CanReceive canReceive)
{
    const bool groupBlocked = event.topic == Topic::Group && !canReceive(event.conversation);
    const bool senderBlocked = event.topic != Topic::Nearby && (event.topic != Topic::Notice || event.eventType == EventType::Notice || (event.eventType == EventType::Conversation && !event.text.empty())) &&
        !event.recipient.empty() && event.recipient != self && !canReceive(event.recipient);
    if (!groupBlocked && !senderBlocked) return true;
    event.text.clear(); event.sender.clear(); event.recipient.clear(); event.title = "Restricted conversation";
    event.unread = 0; event.eventAt = 0; event.flags &= ~AlertEligible;
    event.eventType = EventType::Gap; event.flags |= ConversationRestricted;
    return false;
}
class EventBuffer
{
public:
    static constexpr std::size_t Capacity = 256;
    void resetSession() { clear(); mLast = 0; } // Only at a new login generation.
    void clear() { mEvents.clear(); } // Privacy flushes keep session-local IDs monotonic.
    std::uint64_t latest() const { return mLast; }
    void push(Message message)
    {
        message.event = ++mLast;
        message.sender = boundedText(message.sender, 127);
        message.title = boundedText(message.title, 255);
        message.text = boundedText(message.text, 3071, true);
        mEvents.push_back(std::move(message));
        if (mEvents.size() > Capacity) mEvents.pop_front();
    }
    Message after(std::uint64_t cursor) const
    {
        Message result; result.cursor = mLast;
        const auto discarded = mEvents.empty() ? mLast : mEvents.front().event - 1;
        if (cursor < discarded)
        {
            result.eventType = EventType::Gap; result.event = discarded;
            result.text = "Earlier messages are outside the shared view's retained window. Check this character's native chat history.";
        }
        else for (const auto& event : mEvents) if (event.event > cursor) { result = event; result.cursor = mLast; break; }
        return result;
    }
private:
    std::deque<Message> mEvents;
    std::uint64_t mLast = 0;
};
struct ChatKey
{
    WorkerId worker{};
    std::uint64_t generation = 0;
    std::string account, grid, conversation;
    bool owns(const Message& message) const
    {
        return worker == message.worker && generation == message.generation &&
            account == message.account && grid == message.grid;
    }
};
struct Conversation
{
    std::string id, title, draft;
    Topic topic = Topic::Nearby;
    std::uint32_t unread = 0;
    std::string destination;
    std::uint64_t lastIncomingEvent = 0, lastEvent = 0;
    bool restricted = false;
    bool closed=false;
    std::uint64_t closedAt=0;
    Conversation() = default;
    Conversation(std::string idValue,std::string titleValue,std::string draftValue,Topic topicValue,std::uint32_t count) :
        id(std::move(idValue)),title(std::move(titleValue)),draft(std::move(draftValue)),topic(topicValue),unread(count) {}
};
class ChatView
{
public:
    static constexpr std::size_t MaxConversations = 64, MaxLines = 512;
    static constexpr std::size_t MaxNotices = 64, MaxNoticeLines = 128;
    ChatKey identity;
    std::map<std::string, Conversation> conversations;
    std::deque<Message> lines;
    std::uint64_t cursor = 0;
    bool gap = false;
    std::size_t conversationCount(bool notices) const
    {return static_cast<std::size_t>(std::count_if(conversations.begin(),conversations.end(),[notices](const auto& entry){return (entry.second.topic==Topic::Notice)==notices;}));}
    bool close(const std::string& id,std::uint64_t watermark)
    {
        const auto found=conversations.find(id);
        if(found==conversations.end() || found->second.topic==Topic::Nearby) return false;
        found->second.closed=true;found->second.closedAt=watermark;return true;
    }
    bool markRead(const Message& request)
    {
        if (request.kind != Kind::MarkRead || !identity.owns(request)) return false;
        const auto found = conversations.find(request.conversation);
        if (found == conversations.end() || found->second.topic != request.topic) return false;
        if (request.flags & AutomaticRead)
        {
            if (found->second.lastIncomingEvent > request.event)
            {
                // An acknowledgement cannot dismiss arrivals the surface did
                // not render. Nearby uses local counts; native IMs converge
                // through the next authoritative count/catalog response.
                if (found->second.topic == Topic::Nearby)
                {
                    std::uint32_t newer = 0;
                    for (const auto& line : lines) if (line.conversation == request.conversation &&
                        line.eventType == EventType::Chat && line.recipient != identity.account && line.event > request.event) ++newer;
                    if (!lines.empty() && lines.front().event <= request.event+1)
                        found->second.unread = (std::min)(found->second.unread,newer);
                }
                return true;
            }
        }
        found->second.unread = 0; return true;
    }
    void bind(const Message& status)
    {
        if (identity.owns(status)) return;
        identity = {status.worker, status.generation, status.account, status.grid, {}};
        conversations.clear(); lines.clear(); cursor = 0; gap = false;
        conversations.emplace("", Conversation{"", "Nearby chat", "", Topic::Nearby, 0});
    }
    bool accept(const Message& event)
    {
        if (!identity.owns(event)) return false;
        if (event.eventType == EventType::None) return true;
        if (event.event && event.event <= cursor) return false;
        if (event.eventType == EventType::Gap)
        {
            gap = true; cursor = event.event;
            if (event.flags & ConversationRestricted)
            {
                restrictConversation(event);
            }
            else
            {
                // A lost removal cannot be reconciled by the IM catalog. Drop
                // uncertain notices and their history, retaining ordinary chat
                // and drafts. A targeted restriction gap affects only its chat.
                for (auto it = conversations.begin(); it != conversations.end();)
                    if (it->second.topic == Topic::Notice) it = conversations.erase(it);
                    else ++it;
                lines.erase(std::remove_if(lines.begin(),lines.end(),[](const Message& line)
                    { return line.topic == Topic::Notice; }),lines.end());
            }
            return true;
        }
        if (event.eventType == EventType::Attention || event.eventType == EventType::AttentionRemoved || event.eventType == EventType::SwitchIntent)
        { if (event.event) cursor = event.event; return true; }
        auto found = conversations.find(event.conversation);
        if(event.eventType==EventType::NoticeRemoved)
        {
            if(found!=conversations.end() && found->second.topic==Topic::Notice)
            {conversations.erase(found);lines.erase(std::remove_if(lines.begin(),lines.end(),[&event](const Message& line){return line.topic==Topic::Notice && line.conversation==event.conversation;}),lines.end());}
            if(event.event)cursor=event.event;
            return true;
        }
        if (found == conversations.end())
        {
            if(!makeRoom(event.topic)){gap=true;if(event.event)cursor=event.event;return false;}
            found = conversations.emplace(event.conversation, Conversation{event.conversation, event.title, "", event.topic, 0}).first;
        }
        // A notice UUID cannot replace a native chat/draft, or vice versa.
        if((found->second.topic==Topic::Notice)!=(event.topic==Topic::Notice))
        {if(event.event)cursor=event.event;return false;}
        if(event.event)found->second.lastEvent=event.event;
        if(event.eventType==EventType::ConversationClosed)
        {
            // Native close delivery must not lower an acknowledged host fence
            // or its pending maximum. The acknowledgement explicitly replaces
            // that temporary maximum through close().
            if(found->second.topic!=Topic::Nearby){close(event.conversation,(std::max)(found->second.closedAt,event.event));found->second.unread=0;}
            if(event.event)cursor=event.event;
            return true;
        }
        if (event.eventType == EventType::Conversation)
        {
            found->second.restricted = (event.flags & ConversationRestricted) != 0;
            if (!found->second.restricted) found->second.destination = event.recipient;
        }
        if ((event.flags & ConversationRestricted) || found->second.restricted)
        {
            restrictConversation(event);
            if (event.event) cursor = event.event; // Consume it; never alert or retain its content.
            return event.eventType == EventType::Conversation;
        }
        found->second.title = event.title;
        found->second.topic = event.topic;
        if(((event.eventType==EventType::Chat && !event.recipient.empty() && event.recipient!=identity.account) || event.eventType==EventType::Notice) && event.event>found->second.closedAt)
            found->second.closed=false;
        if (event.eventType == EventType::Chat && event.recipient != identity.account) found->second.lastIncomingEvent = event.event;
        if (event.topic == Topic::Nearby)
        {
            // Only accepted incoming events count: replayed IDs, our own echo
            // and catalog refreshes cannot manufacture or reset shell badges.
            if (event.eventType == EventType::Chat && event.recipient != identity.account &&
                found->second.unread < (std::numeric_limits<std::uint32_t>::max)()) ++found->second.unread;
        }
        else if (event.topic == Topic::Notice)
        {
            // Attention is a boolean, never an offer count/acceptance action.
            if (event.eventType == EventType::Notice) found->second.unread = 1;
        }
        else found->second.unread = event.unread; // Native absolute IM counts.
        if (event.eventType == EventType::Chat || event.eventType == EventType::Notice)
        {
            lines.push_back(event);
            const bool notice=event.topic==Topic::Notice;
            const auto count=std::count_if(lines.begin(),lines.end(),[notice](const Message& line){return (line.topic==Topic::Notice)==notice;});
            if(static_cast<std::size_t>(count)>(notice?MaxNoticeLines:MaxLines))
            {const auto oldest=std::find_if(lines.begin(),lines.end(),[notice](const Message& line){return (line.topic==Topic::Notice)==notice;});lines.erase(oldest);if(!notice)gap=true;}
        }
        if (event.event) cursor = event.event;
        return true;
    }
private:
    bool makeRoom(Topic topic)
    {
        if(topic!=Topic::Notice)return conversationCount(false)<MaxConversations;
        if(conversationCount(true)<MaxNotices)return true;
        auto oldest=conversations.end();
        for(auto it=conversations.begin();it!=conversations.end();++it)if(it->second.topic==Topic::Notice &&
            (oldest==conversations.end() || it->second.lastEvent<oldest->second.lastEvent))oldest=it;
        if(oldest==conversations.end())return false;
        const auto id=oldest->first;conversations.erase(oldest);
        lines.erase(std::remove_if(lines.begin(),lines.end(),[&id](const Message& line){return line.topic==Topic::Notice && line.conversation==id;}),lines.end());
        return true;
    }
    void restrictConversation(const Message& event)
    {
        auto found = conversations.find(event.conversation);
        if (found == conversations.end() && makeRoom(event.topic))
            found = conversations.emplace(event.conversation,Conversation{event.conversation,"Restricted conversation","",event.topic,0}).first;
        if (found != conversations.end())
        {
            found->second.restricted = true; found->second.title = "Restricted conversation";
            found->second.unread = 0; found->second.destination.clear();
            if(event.event)found->second.lastEvent=event.event;
        }
        lines.erase(std::remove_if(lines.begin(),lines.end(),[&event](const Message& line)
            { return line.conversation == event.conversation; }),lines.end());
    }
};
}
#endif
