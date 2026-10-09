/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionchatmodel.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    EventBuffer buffer;
    Message message; message.worker[0] = 9; message.generation = 3; message.account = "11111111-1111-1111-1111-111111111111";
    message.grid = "secondlife"; message.eventType = EventType::Chat; message.title = "Nearby chat"; message.text = "hello";
    for (std::size_t i = 0; i < EventBuffer::Capacity + 4; ++i) buffer.push(message);
    auto gap = buffer.after(0); assert(gap.eventType == EventType::Gap && gap.event == 4 && gap.cursor == 260);
    auto next = buffer.after(gap.event); assert(next.event == 5 && next.text == "hello");
    ChatView view; view.bind(message);
    gap.worker = message.worker; gap.generation = message.generation; gap.account = message.account; gap.grid = message.grid;
    assert(view.accept(gap) && view.gap && view.cursor == 4);
    assert(view.accept(next) && view.lines.size() == 1 && view.cursor == 5);
    assert(!view.accept(next) && view.lines.size() == 1); // Duplicate cannot increment badges/history.
    auto other = next; other.generation++; assert(!view.accept(other));
    other = next; other.account = "22222222-2222-2222-2222-222222222222"; assert(!view.accept(other));
    other = next; other.grid = "another-grid"; assert(!view.accept(other));
    auto key = view.identity; key.conversation = "";
    view.conversations.at("").draft = "Sam's unsent text";
    message.mode = Mode::Active; view.bind(message);
    assert(view.conversations.at("").draft == "Sam's unsent text" && key.owns(message)); // View switching cannot retarget drafts.
    message.generation++; view.bind(message);
    assert(view.lines.empty() && view.cursor == 0 && view.conversations.at("").draft.empty() && !key.owns(message));
    buffer.resetSession(); assert(buffer.after(0).eventType == EventType::None);
    assert(boundedText("a\xc3\xa9", 2) == "a");
    assert(boundedText("x\ny", 3) == "x y");
    assert(boundedText("x\ny", 3, true) == "x\ny");
    for (std::size_t i = 0; i < ChatView::MaxLines + 10; ++i)
    {
        message.event = i + 1; assert(view.accept(message));
    }
    assert(view.lines.size() == ChatView::MaxLines && view.gap);

    // Restrictions toggle on/off while the connection is busy with a handoff:
    // the host sees no ChatRestricted response and retains its old cursor.
    EventBuffer live;
    ChatView waiting;
    message.event = 0; message.topic = Topic::Nearby; message.conversation.clear();
    waiting.bind(message);
    for (int i = 0; i < 3; ++i)
    { live.push(message); assert(waiting.accept(live.after(waiting.cursor))); }
    assert(waiting.cursor == 3 && waiting.conversations.at("").unread == 3);
    live.clear(); live.clear(); // No session generation change and no host response.
    live.push(message);
    next = live.after(waiting.cursor);
    assert(next.event == 4 && next.eventType == EventType::Chat && waiting.accept(next));
    assert(waiting.conversations.at("").unread == 4 && !waiting.accept(next));
    assert(waiting.conversations.at("").unread == 4); // Duplicate cannot increase badges.

    live.push(message); // A message not yet fetched is discarded by restrictions.
    live.clear();
    gap = live.after(waiting.cursor);
    gap.worker = message.worker; gap.generation = message.generation; gap.account = message.account; gap.grid = message.grid;
    assert(gap.eventType == EventType::Gap && gap.event == 5 && waiting.accept(gap));
    assert(live.after(waiting.cursor).eventType == EventType::None); // Empty flush is acknowledged once.
    live.push(message); assert(waiting.accept(live.after(waiting.cursor)) && waiting.cursor == 6);

    // Badges depend on accepted events, not active-world mode or panel layout.
    message.mode = Mode::Warm; live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    message.mode = Mode::Economy; live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    assert(waiting.conversations.at("").unread == 7);
    Message read = message; read.kind = Kind::MarkRead;
    read.generation++; assert(!waiting.markRead(read) && waiting.conversations.at("").unread == 7);
    read.generation = message.generation;
    assert(waiting.markRead(read) && waiting.conversations.at("").unread == 0);
    message.eventType = EventType::Conversation; message.event = 0; message.unread = 99;
    assert(waiting.accept(message) && waiting.conversations.at("").unread == 0); // Catalog cannot replace local count.
    message.eventType = EventType::Chat; live.push(message);
    assert(waiting.accept(live.after(waiting.cursor)) && waiting.conversations.at("").unread == 1);
    message.recipient = message.account; live.push(message); // Native own-message echo remains in history, not unread.
    assert(waiting.accept(live.after(waiting.cursor)) && waiting.conversations.at("").unread == 1);
    message.recipient.clear();
    waiting.conversations.at("").unread = (std::numeric_limits<std::uint32_t>::max)();
    live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    assert(waiting.conversations.at("").unread == (std::numeric_limits<std::uint32_t>::max)());

    message.topic = Topic::Private; message.conversation = "22222222-2222-2222-2222-222222222222";
    message.unread = 3; live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    message.unread = 1; live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    assert(waiting.conversations.at(message.conversation).unread == 1); // IMs retain native absolute counts.
    waiting.conversations.at(message.conversation).draft = "refused send stays here";
    read = message; read.kind = Kind::MarkRead;
    assert(waiting.markRead(read) && waiting.conversations.at(message.conversation).draft == "refused send stays here");

    message.topic = Topic::Notice; message.eventType = EventType::Notice;
    message.conversation = "00000000-0000-0000-0000-000000000000"; message.unread = 100;
    live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    live.push(message); assert(waiting.accept(live.after(waiting.cursor)));
    assert(waiting.conversations.at(message.conversation).unread == 1); // Attention is boolean.
    read = message; read.kind = Kind::MarkRead;
    assert(waiting.markRead(read) && waiting.conversations.at(message.conversation).unread == 0);
    live.push(message); assert(waiting.accept(live.after(waiting.cursor)) && waiting.conversations.at(message.conversation).unread == 1);
    message.generation++; waiting.bind(message); live.resetSession();
    live.push(message); assert(waiting.accept(live.after(waiting.cursor)) && waiting.cursor == 1);
    std::cout << "Bounded event retention/gaps, dedupe, source/session binding and draft isolation passed.\n";
}
