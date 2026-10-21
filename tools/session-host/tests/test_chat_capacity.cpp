/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionalerts.h"
#include "fssessionchatmodel.h"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace fs_session;
static std::string id(unsigned int family,unsigned int value)
{
    std::ostringstream out;out<<"00000000-0000-0000-"<<std::setw(4)<<std::setfill('0')<<family<<"-"<<std::setw(12)<<value;return out.str();
}
int main()
{
    Message source;source.worker[0]=1;source.account=id(0,1);source.grid="agni";
    ChatView chat;chat.bind(source);std::uint64_t sequence=0;
    auto notice=source;notice.topic=Topic::Notice;notice.eventType=EventType::Notice;notice.title="Viewer notice";notice.text="notice";
    for(unsigned int i=1;i<ChatView::MaxConversations;++i)
    {notice.conversation=id(1,i);notice.event=++sequence;assert(chat.accept(notice));}
    // Exact reported failure: 63 hidden notices + Nearby previously refused IMs.
    auto im=source;im.topic=Topic::Private;im.eventType=EventType::Chat;im.conversation=id(2,1);
    im.recipient=id(0,2);im.title="Maddy";im.text="live IM";im.unread=1;im.flags=AlertEligible;im.eventAt=10000;im.event=++sequence;
    assert(chat.accept(im) && chat.conversations.at(im.conversation).unread==1 && !chat.gap);
    AlertGate alerts;assert(alerts.admitCategories(im,SoundPrivate,10001,true));
    chat.conversations.at(im.conversation).draft="unsent draft";
    assert(chat.close(im.conversation,sequence));
    for(unsigned int i=64;i<400;++i)
    {notice.conversation=id(1,i);notice.event=++sequence;assert(chat.accept(notice));}
    assert(chat.conversationCount(true)==ChatView::MaxNotices && chat.conversationCount(false)==2);
    assert(chat.conversations.at(im.conversation).closed && chat.conversations.at(im.conversation).draft=="unsent draft");
    assert(!chat.gap && !chat.conversations.count(id(1,1)));
    auto removal=notice;removal.conversation=id(1,1);removal.eventType=EventType::NoticeRemoved;removal.event=++sequence;
    assert(chat.accept(removal) && chat.conversationCount(true)==ChatView::MaxNotices);
    im.event=++sequence;im.unread=0;im.eventAt=13000;
    assert(chat.accept(im) && !chat.conversations.at(im.conversation).closed);
    assert(alerts.admitCategories(im,SoundPrivate,13001,true)); // Native unread=0 cannot suppress a fresh alert.
    for(unsigned int i=0;i<ChatView::MaxNoticeLines+20;++i)
    {notice.event=++sequence;assert(chat.accept(notice));}
    std::size_t notices=0,ims=0;
    for(const auto& line:chat.lines){if(line.topic==Topic::Notice)++notices;else ++ims;}
    assert(notices==ChatView::MaxNoticeLines && ims==2 && !chat.gap);
    // A full native budget cannot prevent bounded notices, nor erase drafts.
    auto catalog=im;catalog.eventType=EventType::Conversation;catalog.event=0;
    for(unsigned int i=2;i<ChatView::MaxConversations;++i){catalog.conversation=id(2,i);assert(chat.accept(catalog));}
    assert(chat.conversationCount(false)==ChatView::MaxConversations);
    notice.conversation=id(1,401);notice.event=++sequence;assert(chat.accept(notice));
    catalog.conversation=id(2,64);assert(!chat.accept(catalog));
    assert(chat.conversations.at(im.conversation).draft=="unsent draft");
    // Notification pressure cannot prevent remembering a native restriction.
    ChatView restricted;restricted.bind(source);
    for(unsigned int i=1;i<=ChatView::MaxNotices;++i){notice.conversation=id(1,i);notice.event=i;assert(restricted.accept(notice));}
    auto block=im;block.event=65;block.eventType=EventType::Gap;block.flags=ConversationRestricted;
    assert(restricted.accept(block) && restricted.conversations.at(im.conversation).restricted);
    im.event=66;assert(!restricted.accept(im));
    auto different=source;different.generation++;chat.bind(different);
    assert(chat.conversationCount(true)==0 && chat.conversationCount(false)==1 && chat.lines.empty() && !chat.gap);
    std::cout<<"Notification floods preserve live IMs, alerts, drafts, close/restriction fences and separate bounds.\n";
}
