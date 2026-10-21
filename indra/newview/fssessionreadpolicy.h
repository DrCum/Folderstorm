/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_READ_POLICY_H
#define FS_SESSION_READ_POLICY_H
#include "fssessionchatmodel.h"
#include <map>
namespace fs_session
{
inline bool nativeChatReadAllowed(bool managed,bool ready,bool inputOwner,bool foreground,bool visible,bool switching)
{return !managed || (ready && inputOwner && foreground && visible && !switching);}
inline bool readFenceCurrent(const Message& request,std::uint64_t latest)
{ return request.kind == Kind::MarkRead && request.topic != Topic::Notice && request.cursor == latest && request.event <= request.cursor; }
class ActiveReadClock
{
public:
    void update(const Message& source,bool eligible,std::uint64_t now)
    {
        if (!eligible) { bound = false; observed.clear(); return; }
        if (!bound || !identity.owns(source) || now < since)
        { identity = {source.worker,source.generation,source.account,source.grid,{}}; since = now; observed.clear(); bound = true; }
    }
    bool due(const Conversation& conversation,std::uint64_t now,unsigned int seconds)
    {
        if (!bound || !conversation.unread || conversation.restricted || conversation.topic == Topic::Notice) return false;
        auto found = observed.find(conversation.id);
        if (found == observed.end())
        {
            if (observed.size() >= ChatView::MaxConversations) return false;
            found = observed.emplace(conversation.id,Entry{conversation.lastIncomingEvent,now}).first;
        }
        else if (found->second.event != conversation.lastIncomingEvent)
            found->second = {conversation.lastIncomingEvent,now};
        return now >= found->second.at && now-found->second.at >= static_cast<std::uint64_t>(seconds)*1000;
    }
private:
    struct Entry { std::uint64_t event = 0, at = 0; };
    ChatKey identity;
    bool bound = false;
    std::uint64_t since = 0;
    std::map<std::string,Entry> observed;
};
}
#endif
