#include "fssessionchatprefs.h"
#include "fssessionmonitorchat.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
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
    std::cout << "Category/profile migration, close fences, draft retention and native notice removal passed.\n";
}
