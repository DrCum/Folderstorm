/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionusability.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    Message status; status.worker[0] = 1; status.generation = 2; status.account = "01234567-89ab-cdef-0123-456789abcdef"; status.grid = "secondlife";
    ChatView view; view.bind(status); PinBook pins;
    assert(pins.add(view,"") && !pins.add(view,""));
    Message event = status; event.eventType = EventType::Conversation; event.topic = Topic::Private;
    event.conversation = "11111111-1111-1111-1111-111111111111"; event.recipient = "22222222-2222-2222-2222-222222222222";
    event.title = "A friend"; assert(view.accept(event)); assert(pins.add(view,event.conversation));
    view.conversations[event.conversation].draft = "unsent";
    assert(pins.move(1,-1) && pins.find(view,event.conversation) == 0);
    const auto draftBefore = view.conversations[event.conversation].draft;
    event.flags = ConversationRestricted; event.title = "Restricted conversation";
    assert(view.accept(event)); pins.bind(view,false); assert(pins.find(view,event.conversation) < 0);
    assert(view.conversations[event.conversation].draft == draftBefore);
    event.flags = 0; event.title = "A friend"; assert(view.accept(event)); pins.bind(view,false); assert(pins.find(view,event.conversation) == 0);
    const auto saved = pins.saved(); assert(saved.size() == 2 && saved[0].destination == event.recipient);
    pins.bind(view,true); assert(pins.find(view,event.conversation) < 0);
    pins.bind(view,false); assert(pins.find(view,event.conversation) == 0);
    assert(view.conversations[event.conversation].draft == "unsent");
    PinBook restored; restored.restore(saved); assert(!restored.entries[0].live(view)); restored.bind(view,false); assert(restored.entries[0].live(view));
    ++status.generation; view.bind(status); pins.bind(view,false); assert(pins.find(view,event.conversation) < 0);
    assert(view.conversations.size() == 1 && pins.entries[1].live(view)); // Nearby can rebind; peer needs an existing native conversation.
    AttentionBook attention; event = status; event.eventType = EventType::Attention; event.topic = Topic::Notice;
    event.conversation = "11111111-1111-1111-1111-111111111111"; event.title = "Teleport offer"; event.event = 1;
    assert(view.accept(event)); assert(view.conversations.size() == 1); // Offers are not fake chat conversations.
    attention.accept(event); attention.accept(event); assert(attention.entries.size() == 1);
    attention.dismiss(event); attention.accept(event); assert(attention.entries.empty());
    event.eventType = EventType::AttentionRemoved; attention.accept(event); event.eventType = EventType::Attention; attention.accept(event); assert(attention.entries.size() == 1);
    auto another = event; ++another.worker[0]; attention.accept(another); assert(attention.entries.size() == 2);
    attention.forget(view.identity); assert(attention.entries.size() == 1 && attention.entries[0].worker == another.worker);
    another.eventType = EventType::Gap; attention.accept(another); assert(attention.entries.empty() && attention.gap);
    Message unread = status; unread.eventType = EventType::Conversation; unread.unread = 1; unread.cursor = 10;
    attention.dismiss(unread); assert(attention.hiddenUnread(unread)); ++unread.cursor; assert(!attention.hiddenUnread(unread));
    assert(attentionCategory("ScriptQuestion") == "Permission request" && attentionCategory("TeleportOffered") == "Teleport offer");
    assert(attentionCategory("UntrustedUnknown").empty());
    Frame frame; Message decoded; event.worker[0] = 1; event.kind = Kind::ReviewAttention;
    assert(encode(event,frame) && decode(frame,decoded) && decoded.kind == Kind::ReviewAttention);
    std::cout << "Pinned draft/source ownership, durable references, privacy flush and attention lifecycle passed.\n";
}
