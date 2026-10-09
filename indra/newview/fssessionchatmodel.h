/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CHAT_MODEL_H
#define FS_SESSION_CHAT_MODEL_H
#include "fssessionprotocol.h"
#include <deque>
#include <map>

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
class EventBuffer
{
public:
    static constexpr std::size_t Capacity = 256;
    void reset() { mEvents.clear(); mLast = 0; }
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
        if (!mEvents.empty() && cursor < mEvents.front().event - 1)
        {
            result.eventType = EventType::Gap; result.event = mEvents.front().event - 1;
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
};
class ChatView
{
public:
    static constexpr std::size_t MaxConversations = 64, MaxLines = 512;
    ChatKey identity;
    std::map<std::string, Conversation> conversations;
    std::deque<Message> lines;
    std::uint64_t cursor = 0;
    bool gap = false;
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
        { gap = true; cursor = event.event; return true; }
        auto found = conversations.find(event.conversation);
        if (found == conversations.end())
        {
            if (conversations.size() >= MaxConversations) { gap = true; if (event.event) cursor = event.event; return false; }
            found = conversations.emplace(event.conversation, Conversation{event.conversation, event.title, "", event.topic, 0}).first;
        }
        found->second.title = event.title;
        found->second.topic = event.topic;
        found->second.unread = event.unread;
        if (event.eventType == EventType::Chat || event.eventType == EventType::Notice)
        {
            lines.push_back(event);
            if (lines.size() > MaxLines) { lines.pop_front(); gap = true; }
        }
        if (event.event) cursor = event.event;
        return true;
    }
};
}
#endif
