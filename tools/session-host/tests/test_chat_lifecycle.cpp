#include "fssessionchatprefs.h"
#include "fssessionmonitorchat.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
static Message chatSource()
{
    Message source;source.worker[0]=1;source.generation=3;source.state=State::Ready;
    source.account="11111111-1111-1111-1111-111111111111";source.grid="agni";
    return source;
}
static void testNativeCloseOrdering()
{
    // Native close delivery can precede or follow the host acknowledgement.
    // Neither ordering lets an older buffered arrival reverse the user's close.
    for(const bool acknowledgeFirst : {false,true})
    {
        const auto source=chatSource();ChatView view;view.bind(source);
        auto chat=source;chat.eventType=EventType::Chat;chat.topic=Topic::Private;
        chat.conversation="22222222-2222-2222-2222-222222222222";
        chat.recipient="33333333-3333-3333-3333-333333333333";chat.event=1;chat.text="hello";
        assert(view.accept(chat));view.conversations.at(chat.conversation).draft="keep draft";
        const auto pending=(std::numeric_limits<std::uint64_t>::max)();
        assert(view.close(chat.conversation,pending));
        if(acknowledgeFirst)assert(view.close(chat.conversation,6));
        auto nativeClose=chat;nativeClose.eventType=EventType::ConversationClosed;nativeClose.event=5;
        assert(view.accept(nativeClose));
        assert(view.conversations.at(chat.conversation).closedAt==(acknowledgeFirst?6:pending));
        if(!acknowledgeFirst)assert(view.close(chat.conversation,6)); // Replace only the pending fence.
        chat.event=6;assert(view.accept(chat) && view.conversations.at(chat.conversation).closed);
        chat.event=7;assert(view.accept(chat) && !view.conversations.at(chat.conversation).closed);
        assert(view.conversations.at(chat.conversation).draft=="keep draft");
        nativeClose.event=8;assert(view.accept(nativeClose));
        assert(view.conversations.at(chat.conversation).closed && view.conversations.at(chat.conversation).closedAt==8);
        chat.event=9;assert(view.accept(chat) && !view.conversations.at(chat.conversation).closed);
    }
}
static void testLostNoticeRemoval()
{
    const auto source=chatSource();ChatView view;view.bind(source);EventBuffer queue;
    auto chat=source;chat.eventType=EventType::Chat;chat.topic=Topic::Private;
    chat.conversation="22222222-2222-2222-2222-222222222222";
    chat.recipient="33333333-3333-3333-3333-333333333333";chat.unread=2;chat.text="keep history";
    queue.push(chat);assert(view.accept(queue.after(view.cursor)));
    view.conversations.at(chat.conversation).draft="keep draft";
    auto notice=source;notice.eventType=EventType::Notice;notice.topic=Topic::Notice;
    notice.conversation="44444444-4444-4444-4444-444444444444";notice.text="temporary notice";
    queue.push(notice);assert(view.accept(queue.after(view.cursor)));
    assert(view.conversations.at(notice.conversation).unread==1);
    auto removed=notice;removed.eventType=EventType::NoticeRemoved;queue.push(removed);
    // Drop the removal out of the actual bounded worker queue before polling.
    for(std::size_t i=0;i<EventBuffer::Capacity;++i)queue.push(chat);
    auto gap=queue.after(view.cursor);gap.worker=source.worker;gap.generation=source.generation;
    gap.account=source.account;gap.grid=source.grid;
    assert(gap.eventType==EventType::Gap && gap.event==3);
    auto foreign=gap;foreign.generation++;assert(!view.accept(foreign));
    assert(view.conversations.count(notice.conversation));
    // A targeted restriction response is not evidence of lost removal events.
    ChatView targeted=view;auto restriction=gap;restriction.event=view.cursor+1;restriction.flags=ConversationRestricted;
    restriction.conversation="55555555-5555-5555-5555-555555555555";restriction.topic=Topic::Group;
    assert(targeted.accept(restriction) && targeted.conversations.count(notice.conversation));
    assert(!targeted.accept(gap) && targeted.conversations.count(notice.conversation)); // Stale gap is harmless.
    assert(view.accept(gap) && view.gap);
    assert(!view.conversations.count(notice.conversation));
    for(const auto& line:view.lines)assert(line.topic!=Topic::Notice);
    assert(view.lines.size()==1 && view.lines.front().text=="keep history");
    assert(view.conversations.at(chat.conversation).unread==2 && view.conversations.at(chat.conversation).draft=="keep draft");
    removed.event=queue.latest()+1;assert(view.accept(removed) && !view.conversations.count(notice.conversation));
    // A later native event for an active notice can repopulate the bounded view.
    notice.event=queue.latest()+2;assert(view.accept(notice));
    assert(view.conversations.at(notice.conversation).unread==1);
}
int main()
{
    testLostNoticeRemoval();
    testNativeCloseOrdering();
    assert(conversationCategory(Topic::Private)!=conversationCategory(Topic::Group));
    assert(legacyAttentionCategories(29)==254 && legacyAttentionCategories(2)==CategoryNearby);
    StoreWriter legacy("FSC2");legacy.number(1);legacy.number(0);legacy.number(60);legacy.number(0);
    ChatPreferences prefs;assert(ChatPreferences::decode(legacy.data,prefs) && !prefs.haveCategories);
    prefs.showMask&=~CategoryGroup;prefs.attentionMask&=~CategoryViewer;prefs.profileNames[2]="Sodie";
    ChatPreferences restored;assert(ChatPreferences::decode(prefs.encode(),restored));
    assert(restored.haveCategories && restored.profileNames[2]=="Sodie" && !(restored.showMask&CategoryGroup));
    auto corrupt=prefs.encode()+"x";assert(!ChatPreferences::decode(corrupt,restored) && restored.profileNames[2]=="Sodie");
    prefs.profileNames[1]="bad\nlabel";assert(prefs.encode().empty());
    Message source;source.worker[0]=1;source.generation=3;source.state=State::Ready;
    source.account="11111111-1111-1111-1111-111111111111";source.grid="agni";
    ChatView view;view.bind(source);Message chat=source;chat.eventType=EventType::Chat;chat.topic=Topic::Private;
    chat.conversation="22222222-2222-2222-2222-222222222222";chat.recipient="33333333-3333-3333-3333-333333333333";chat.event=1;chat.text="hello";
    assert(view.accept(chat));view.conversations.at(chat.conversation).draft="unsent";
    assert(view.close(chat.conversation,(std::numeric_limits<std::uint64_t>::max)()));
    chat.event=2;assert(view.accept(chat) && view.conversations.at(chat.conversation).closed);
    assert(view.close(chat.conversation,10)); // Worker acknowledgement covers queued arrivals.
    auto catalog=chat;catalog.event=0;catalog.eventType=EventType::Conversation;
    assert(view.accept(catalog) && view.conversations.at(chat.conversation).closed);
    chat.event=11;chat.recipient=source.account;assert(view.accept(chat) && view.conversations.at(chat.conversation).closed);
    chat.event=12;chat.recipient="33333333-3333-3333-3333-333333333333";
    assert(view.accept(chat) && !view.conversations.at(chat.conversation).closed && view.conversations.at(chat.conversation).draft=="unsent");
    auto close=chat;close.event=13;close.eventType=EventType::ConversationClosed;
    assert(view.accept(close) && view.conversations.at(chat.conversation).closed);
    auto system=chat;system.event=14;system.recipient.clear();assert(view.accept(system) && view.conversations.at(chat.conversation).closed);
    assert(!view.close("",14)); // Nearby survives close commands.
    auto notice=source;notice.event=20;notice.eventType=EventType::Notice;notice.topic=Topic::Notice;
    notice.conversation="44444444-4444-4444-4444-444444444444";notice.text="Inventory delivery declined";notice.title="Viewer: Inventory Declined";
    assert(view.accept(notice) && view.conversations.at(notice.conversation).unread==1);
    notice.event=21;notice.eventType=EventType::NoticeRemoved;
    assert(view.accept(notice) && !view.conversations.count(notice.conversation));
    for(const auto& line:view.lines)assert(line.conversation!=notice.conversation);
    auto stale=close;stale.generation=4;stale.event=99;assert(!view.accept(stale));
    notice.eventType=EventType::Notice;notice.recipient=chat.recipient;notice.text="private detail";
    assert(!filterChatEvent(notice,source.account,[](const std::string&){return false;}) && notice.text.empty());
    std::cout << "Category/profile migration, monotonic close fences, draft retention and lost notice removal passed.\n";
}
