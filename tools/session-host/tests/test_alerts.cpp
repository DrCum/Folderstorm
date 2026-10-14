/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionalerts.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    Message event; event.worker[0] = 1; event.account = "01234567-89ab-cdef-0123-456789abcdef"; event.grid = "secondlife";
    event.eventType = EventType::Chat; event.topic = Topic::Private; event.flags = AlertEligible; event.event = 1; event.eventAt = 10000;
    AlertGate gate;
    assert(gate.admit(event,1,10000,true)); assert(!gate.admit(event,1,14000,true)); // duplicate, even after cooldown
    ++event.event; event.eventAt = 10500; assert(!gate.admit(event,1,10500,true));
    ++event.event; event.eventAt = 12500; assert(gate.admit(event,1,12500,true));
    ++event.event; event.eventAt = 15000; assert(!gate.admit(event,1,15000,false)); // native active alert remains sole producer
    ++event.event; assert(!gate.admit(event,0,15000,true));
    ++event.event; event.eventAt = 16000; assert(!gate.admit(event,1,20001,true)); // backlog is silent
    ++event.event; event.eventAt = 20000; event.recipient = event.account; assert(!gate.admit(event,1,20000,true));
    ++event.event; event.recipient.clear(); event.flags = 0; assert(!gate.admit(event,1,20000,true));
    ++event.event; event.flags = AlertEligible; event.eventType = EventType::Attention; assert(!gate.admit(event,1,20000,true));
    ++event.event; assert(gate.admit(event,2,20000,true));
    AlertGate categories; event.eventType = EventType::Chat; event.topic = Topic::Group;
    ++event.event; event.eventAt = 30000; assert(!categories.admitCategories(event,SoundPrivate,30000,true));
    event.topic = Topic::Private; ++event.event; assert(categories.admitCategories(event,SoundPrivate,30000,true));
    event.topic = Topic::Conference; ++event.event; event.eventAt = 33000;
    assert(!categories.admitCategories(event,SoundPrivate,33000,true));
    ++event.event; assert(categories.admitCategories(event,SoundConference,33000,true));
    event.topic = Topic::Group; ++event.event; event.eventAt = 36000;
    assert(categories.admitCategories(event,SoundGroup,36000,true));
    event.eventType = EventType::Attention; ++event.event; event.eventAt = 39000;
    assert(!categories.admitCategories(event,SoundGroup,39000,true));
    ++event.event; assert(categories.admitCategories(event,SoundAttention,39000,true));
    ++event.event; event.eventAt = 42000; event.flags |= ConversationRestricted;
    assert(!categories.admitCategories(event,15,42000,true)); event.flags &= ~ConversationRestricted;
    Frame frame; Message roundtrip; assert(encode(event,frame) && decode(frame,roundtrip) && roundtrip.eventAt == 42000 && (roundtrip.flags & AlertEligible));
    event.eventType=EventType::Chat;event.topic=Topic::Private;event.eventAt=50000;event.unread=0;event.event++;
    AlertGate reasons;assert(reasons.decide(event,SoundPrivate,50001,true)==AlertDecision::Allowed);
    assert(reasons.decide(event,SoundPrivate,50001,true)==AlertDecision::Duplicate);
    const auto check=[](Message item,unsigned int mask,std::uint64_t now,bool background)
    {AlertGate single;return single.decide(item,mask,now,background);};
    assert(check(event,SoundPrivate,50001,false)==AlertDecision::BackgroundUnavailable);
    assert(check(event,SoundGroup,50001,true)==AlertDecision::CategoryOff);
    assert(check(event,SoundPrivate,54001,true)==AlertDecision::Stale);
    event.flags=0;assert(check(event,SoundPrivate,50001,true)==AlertDecision::WorkerIneligible);
    event.flags=AlertEligible|ConversationRestricted;assert(check(event,SoundPrivate,50001,true)==AlertDecision::Restricted);
    event.flags=AlertEligible;event.recipient=event.account;assert(check(event,SoundPrivate,50001,true)==AlertDecision::OwnMessage);
    event.recipient.clear();event.detail="Alert: viewer master/UI muted or UI volume zero";
    assert(encode(event,frame) && decode(frame,roundtrip) && roundtrip.detail==event.detail && roundtrip.unread==0);
    std::cout << "Eligible background-only alerts, deduplication, freshness and burst suppression passed.\n";
}
