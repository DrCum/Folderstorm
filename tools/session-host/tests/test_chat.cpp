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
    buffer.reset(); assert(buffer.after(0).eventType == EventType::None);
    assert(boundedText("a\xc3\xa9", 2) == "a");
    assert(boundedText("x\ny", 3) == "x y");
    assert(boundedText("x\ny", 3, true) == "x\ny");
    for (std::size_t i = 0; i < ChatView::MaxLines + 10; ++i)
    {
        message.event = i + 1; assert(view.accept(message));
    }
    assert(view.lines.size() == ChatView::MaxLines && view.gap);
    std::cout << "Bounded event retention/gaps, dedupe, source/session binding and draft isolation passed.\n";
}
