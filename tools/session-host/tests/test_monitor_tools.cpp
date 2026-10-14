/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionmonitortools.h"
#include "fssessionmonitorchat.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    CyclingMonitor cycle;std::array<unsigned int,MaxCharacters> order{{3,1,4,0,2}};
    const auto eligible=[](int i){return i!=0 && i!=2;};
    assert(cycle.seconds==10 && cycle.next(-1,1,order,eligible)==3 && cycle.next(3,1,order,eligible)==1 && cycle.next(3,-1,order,eligible)==4);
    assert(cycle.next(4,1,order,[](int){return false;})==-1);
    cycle.play(100);assert(!cycle.elapsed(10099));assert(cycle.elapsed(10100));assert(!cycle.chatAllowed(true));
    cycle.enteredComposer(true);assert(cycle.playing);cycle.enteredComposer(false);assert(!cycle.playing && cycle.chatAllowed(true));assert(!cycle.elapsed(50000));
    assert(!CyclingMonitor::validInterval(1) && CyclingMonitor::validInterval(2) && CyclingMonitor::validInterval(3600) && !CyclingMonitor::validInterval(3601));
    for(bool row:{false,true})for(unsigned int count=1;count<=MonitorViews;++count)
    {
        const auto tiles=monitorTiles({1920,-800,1919,1041},count,row);assert(tiles.size()==count);
        for(std::size_t i=0;i<tiles.size();++i){const auto& t=tiles[i];assert(t.x>=1920 && t.y>=-800 && t.x+t.width<=3839 && t.y+t.height<=241);for(std::size_t j=0;j<i;++j){const auto& old=tiles[j];assert(t.x>=old.x+old.width || old.x>=t.x+t.width || t.y>=old.y+old.height || old.y>=t.y+t.height);}}
    }
    std::array<Message,MaxCharacters> sources{};std::array<ChatView,MaxCharacters> chats{};MonitorSet set;
    for(int i=0;i<MaxCharacters;++i){auto& s=sources[i];s.worker[0]=static_cast<std::uint8_t>(i+1);s.generation=1;s.pid=static_cast<std::uint32_t>(100+i);s.account="account"+std::to_string(i);s.grid="grid";s.state=State::Ready;chats[i].bind(s);chats[i].conversations.at("").draft="draft "+std::to_string(i);}
    for(int i=1;i<MaxCharacters;++i)assert(set.open(i,sources[i],0,3)>=0);
    assert(set.cycle(1,sources[1],2,3));assert(set.find(1)==0);const auto visible=[](int){return true;};
    assert(set.visibleSources(visible)==4 && set.policyFor(1,visible).width==640);
    unsigned int rate=0;for(int i=1;i<MaxCharacters;++i)rate+=set.policyFor(i,visible).unread;assert(rate<=PreviewAggregateRate);
    assert(set.cycle(0,sources[0],2,3) && set.visibleSources(visible)==5);
    rate=0;for(int i=0;i<MaxCharacters;++i)rate+=set.policyFor(i,visible).unread;assert(rate==PreviewAggregateRate);
    assert(set.cycle(1,sources[1],2,3));
    MonitorChat chat;assert(chat.bind(set.bindings[CyclingView],&sources[1],&chats[1]));assert(chat.draft(set.bindings[CyclingView],sources[1],chats[1],"unsent A"));
    auto oldRequest=chat.request(Kind::SendChat,set.bindings[CyclingView],sources[1],chats[1]);assert(oldRequest);
    assert(set.cycle(3,sources[3],2,3));assert(chat.bind(set.bindings[CyclingView],&sources[3],&chats[3]));assert(chats[1].conversations.at("").draft=="unsent A" && chats[3].conversations.at("").draft=="draft 3");
    assert(!chat.request(Kind::SendChat,set.bindings[CyclingView],sources[1],chats[1]));
    set.stopSlot(3);assert(!set.bindings[CyclingView].enabled && set.find(3)<0);assert(set.find(1)==0);
    std::cout<<"Monitor tools: order/timing, typing pause, source-owned drafts, tiling and shared preview budgets passed.\n";
}
