/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionmonitorchat.h"
#include <cassert>
#include <iostream>
#ifdef NDEBUG
#error Session regressions require assertions, including Release configurations.
#endif
using namespace fs_session;
Message source(int index)
{
    Message result; result.worker[0] = static_cast<std::uint8_t>(index+1);
    result.pid = static_cast<std::uint32_t>(100+index); result.generation = 1;
    result.account = "account"+std::to_string(index); result.grid = "secondlife";
    result.state = State::Ready; result.mode = Mode::Warm; return result;
}
int main()
{
    std::array<Message,MaxCharacters> sources{};
    std::array<ChatView,MaxCharacters> views{};
    for (int i = 0; i < MaxCharacters; ++i)
    {
        sources[i] = source(i); views[i].bind(sources[i]);
        views[i].conversations.at("").draft = "Nearby draft "+std::to_string(i);
        views[i].conversations.emplace("im",Conversation{"im","Private","IM draft "+std::to_string(i),Topic::Private,3});
    }
    MonitorSet set;
    const int id = set.open(1,sources[1],0,1); assert(id == 0);
    MonitorChat chat;
    assert(chat.bind(set.bindings[id],&sources[1],&views[1]));
    assert(chat.select(set.bindings[id],sources[1],views[1],"im"));
    assert(!chat.bind(set.bindings[id],&sources[1],&views[1]));
    assert(chat.identity.conversation == "im");
    assert(chat.draft(set.bindings[id],sources[1],views[1],"B text"));
    assert(views[1].conversations.at("im").draft == "B text");
    auto request = chat.request(Kind::SendChat,set.bindings[id],sources[1],views[1]);
    assert(request && request->account == sources[1].account && request->topic == Topic::Private);
    request->text = "B text";
    assert(!chatRequestAllowed(views[0],sources[0],*request)); // Wrong account never sends.
    assert(!chat.draft(set.bindings[id],sources[0],views[0],"wrong"));
    assert(!chat.select(set.bindings[id],sources[1],views[1],"missing"));
    auto next = sources[1]; ++next.generation;
    assert(!chat.request(Kind::SendChat,set.bindings[id],next,views[1]));
    auto differentWorker = sources[1]; ++differentWorker.worker[0];
    assert(!chat.request(Kind::MarkRead,set.bindings[id],differentWorker,views[1]));
    auto differentGrid = sources[1]; differentGrid.grid = "other";
    assert(!chat.request(Kind::SendChat,set.bindings[id],differentGrid,views[1]));
    auto restricted = sources[1]; restricted.flags |= ChatRestricted;
    assert(!chat.request(Kind::SendChat,set.bindings[id],restricted,views[1]));
    assert(!chat.request(Kind::MarkRead,set.bindings[id],restricted,views[1]));
    views[1].conversations.at("im").restricted = true;
    assert(!chat.draft(set.bindings[id],sources[1],views[1],"hidden"));
    assert(views[1].conversations.at("im").draft == "B text");
    views[1].conversations.at("im").restricted = false;
    auto read = chat.request(Kind::MarkRead,set.bindings[id],sources[1],views[1]);
    assert(read && views[1].markRead(*read));
    assert(views[1].conversations.at("im").unread == 0 && views[0].conversations.at("im").unread == 3);
    // A monitor exchange keeps both accounts' canonical drafts and changes the
    // visible sender/selection only after the handoff succeeds.
    set.beginExchange(0,1,&sources[0],sources[1]);
    const auto at = [&](int i) -> const Message* { return &sources[i]; };
    assert(set.finish(false,0,at) == MonitorSet::Exchange::Kept);
    assert(!chat.bind(set.bindings[id],&sources[1],&views[1]) && chat.identity.conversation == "im");
    set.beginExchange(0,1,&sources[0],sources[1]);
    assert(set.finish(true,1,at) == MonitorSet::Exchange::Exchanged);
    assert(!chat.request(Kind::SendChat,set.bindings[id],sources[0],views[0])); // Stale UI before refresh.
    assert(chat.bind(set.bindings[id],&sources[0],&views[0]));
    assert(chat.identity.conversation.empty());
    auto a = chat.request(Kind::SendChat,set.bindings[id],sources[0],views[0]);
    assert(a && a->account == sources[0].account && a->topic == Topic::Nearby);
    assert(views[0].conversations.at("").draft == "Nearby draft 0");
    assert(views[1].conversations.at("im").draft == "B text");
    assert(!acknowledgeChatDraft(views[0],*request)); // Old send cannot clear A.
    views[1].conversations.at("im").draft = "newer B draft";
    assert(!acknowledgeChatDraft(views[1],*request)); // Late ACK cannot erase newer text.
    views[1].conversations.at("im").draft = "B text";
    assert(acknowledgeChatDraft(views[1],*request));
    assert(views[1].conversations.at("im").draft.empty());
    auto replacement = sources[1]; ++replacement.generation; views[1].bind(replacement);
    views[1].conversations.emplace("im",Conversation{"im","Private","B text",Topic::Private,1});
    assert(!acknowledgeChatDraft(views[1],*request)); // Same account/conversation, new login.
    assert(views[1].conversations.at("im").draft == "B text");
    views[0].conversations.emplace("notice",Conversation{"notice","Notices","",Topic::Notice,1});
    assert(chat.select(set.bindings[id],sources[0],views[0],"notice"));
    assert(!chat.request(Kind::SendChat,set.bindings[id],sources[0],views[0]));
    assert(chat.request(Kind::MarkRead,set.bindings[id],sources[0],views[0]));
    assert(chat.select(set.bindings[id],sources[0],views[0],""));
    const std::string longDraft(2046,'x');
    assert(chat.draft(set.bindings[id],sources[0],views[0],longDraft)); // Retain text over send limit.
    assert(!chat.draft(set.bindings[id],sources[0],views[0],std::string(4093,'x')));
    assert(!chat.draft(set.bindings[id],sources[0],views[0],std::string(1,static_cast<char>(0xff))));
    set.stop(id);
    assert(!chat.request(Kind::SendChat,set.bindings[id],sources[0],views[0]));
    assert(chat.bind(set.bindings[id],nullptr,nullptr));
    assert(!chat.bound && chat.identity.account.empty());
    assert(views[0].conversations.at("").draft == longDraft); // Closing monitor keeps session draft.
    std::cout << "Monitor Chat source guards, swaps, shared drafts, restrictions, Mark read and exact acknowledgement passed.\n";
}
