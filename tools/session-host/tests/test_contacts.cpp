/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionfriendspresentation.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
static std::string uuid(unsigned int n){auto id=std::string("11111111-1111-1111-1111-111111111111");id.replace(24,12,12-std::to_string(n).size(),'0');id+=std::to_string(n);return id;}
int main()
{
    Contact a{uuid(1),"Zebra","zebra.resident",true,true},b{uuid(2),"Alice","alice.resident",false,true},c{uuid(3),"Alpha","alpha.resident",true,false};
    const auto sorted=sortedContacts({b,a,c});assert(sorted[0].id==c.id && sorted[1].id==a.id && sorted[2].id==b.id);
    assert(sortedContacts(sorted,"RESIDENT").size()==3 && sortedContacts(sorted,"alice")[0].id==b.id);
    const auto grouped=presentFriends({b,a,c});
    assert(grouped.rows.size()==5 && grouped.rows[0].kind==FriendsRowKind::OnlineHeading && grouped.rows[0].count==2);
    assert(grouped.rows[3].kind==FriendsRowKind::OfflineHeading && grouped.rows[3].count==1);
    assert(!grouped.at(0) && !grouped.at(3) && !grouped.at(static_cast<std::size_t>(-1)));
    assert(grouped.at(1)->id==c.id && grouped.at(2)->id==a.id && grouped.at(4)->id==b.id);
    assert(grouped.selectable(0,1)==1 && grouped.selectable(3,1)==4 && grouped.selectable(3,-1)==2);
    assert(grouped.selectable(5,1)==-1 && grouped.selectable(-1,-1)==-1 && grouped.selectable(0,0)==-1);
    const auto onlineOnly=presentFriends({b,a,c},{},true);
    assert(onlineOnly.contacts.size()==2 && onlineOnly.rows.size()==3 && onlineOnly.find(b.id)==-1);
    const auto offlineOnly=presentFriends({b,a,c},"alice");
    assert(offlineOnly.rows.size()==2 && offlineOnly.rows[0].kind==FriendsRowKind::OfflineHeading && offlineOnly.rows[0].count==1);
    assert(offlineOnly.at(1)->id==b.id && presentFriends({b,a,c},"alice",true).rows.empty());
    auto changedStatus=a;changedStatus.online=false;
    const auto refreshed=presentFriends({b,changedStatus,c});
    assert(grouped.find(a.id)==2 && refreshed.find(a.id)==4 && refreshed.at(static_cast<std::size_t>(refreshed.find(a.id)))->id==a.id);
    const auto results=presentFriends({b,a,c},{},true,true); // Searches do not claim online status or hide offline/unknown results.
    assert(results.rows.size()==3 && results.contacts.size()==3 && results.at(static_cast<std::size_t>(results.find(b.id)))->id==b.id);
    assert(presentFriends({}).rows.empty());
    Message source;source.worker[0]=1;source.generation=9;source.account=uuid(9);source.grid="grid";source.state=State::Ready;
    ContactDirectory directory;assert(directory.bind(source));directory.beginFriends();
    Message request=source;request.kind=Kind::Contacts;request.width=0;request.event=directory.friends.token;request.cursor=0;
    Message response=request;response.kind=Kind::Status;response.height=0;response.unread=3;std::size_t next=0;response.text=contactPage(sorted,0,next);response.cursor=next;
    Frame wire;assert(encode(response,wire));Message decoded;assert(decode(wire,decoded));assert(directory.accept(request,decoded));
    assert(directory.selected(source,b,0,directory.friends.token)); // Offline friends remain available.
    assert(!directory.selected(source,c,0,directory.friends.token));assert(!directory.selected(source,b,0,directory.friends.token+1));
    directory.beginSearch();auto search=request;search.width=1;search.event=directory.search.token;auto ready=search;ready.kind=Kind::Status;ready.height=0;ready.text=response.text;ready.cursor=3;ready.unread=3;
    auto stale=ready;--stale.event;assert(!directory.accept(search,stale));assert(directory.accept(search,ready));
    for(unsigned int change=0;change<5;++change){auto altered=source;if(change==0)++altered.worker[0];if(change==1)++altered.generation;if(change==2)altered.account=uuid(10);if(change==3)altered.grid="other";if(change==4)altered.flags|=ChatRestricted;assert(!directory.selected(altered,b,1,directory.search.token));}
    auto nextLogin=source;++nextLogin.generation;assert(directory.bind(nextLogin));assert(directory.friends.rows.empty() && directory.search.rows.empty());assert(!directory.accept(search,ready));
    ChatView chat;chat.bind(source);auto opened=source;opened.eventType=EventType::Conversation;opened.topic=Topic::Private;opened.conversation=uuid(1);opened.title="Resident";opened.recipient=uuid(1);opened.cursor=20;assert(chat.reopen(opened));
    auto oldClose=opened;oldClose.eventType=EventType::ConversationClosed;oldClose.event=19;assert(chat.accept(oldClose) && !chat.conversations.at(uuid(1)).closed);oldClose.event=21;assert(chat.accept(oldClose) && chat.conversations.at(uuid(1)).closed);
    directory.bind(source);directory.beginSearch();auto pending=source;pending.width=1;pending.event=directory.search.token;auto waiting=pending;waiting.height=1;assert(directory.accept(pending,waiting) && directory.search.loading && directory.search.pending);
    auto refused=pending;refused.flags=Error;refused.detail="Search failed";refused.event=0;assert(directory.accept(pending,refused) && !directory.search.loading && directory.search.error=="Search failed");
    ContactDirectory pages;pages.bind(source);pages.beginFriends();std::vector<Contact> many;
    for(unsigned int i=0;i<200;++i)many.push_back({uuid(i+1),std::string(255,'x'),std::string(127,'u'),false,true});
    std::size_t at=0;while(at<many.size())
    {
        auto q=source;q.width=0;q.event=pages.friends.token;q.cursor=at;auto r=q;r.height=0;r.unread=static_cast<std::uint32_t>(many.size());r.text=contactPage(many,at,next);r.cursor=next;
        assert(r.text.size()<=3071 && next>at && pages.accept(q,r));at=next;
    }
    assert(!pages.friends.loading && pages.friends.rows.size()==many.size());
    std::vector<Contact> parsed;assert(!readContactPage("FSC1\ninvalid\n",parsed));auto bad=a;bad.display="bad\tname";assert(!bad.valid());
    auto blocked=source;blocked.flags|=ChatRestricted;pages.bind(blocked);assert(pages.friends.rows.empty());
    std::cout<<"Contacts: grouped status/counts, safe heading selection, online-only filtering, stable resident selection, offline messaging, bounded pagination and ownership guards passed.\n";
}
