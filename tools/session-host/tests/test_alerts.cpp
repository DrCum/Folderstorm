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
    Frame frame; Message roundtrip; assert(encode(event,frame) && decode(frame,roundtrip) && roundtrip.eventAt == 20000 && (roundtrip.flags & AlertEligible));
    std::cout << "Eligible background-only alerts, deduplication, freshness and burst suppression passed.\n";
}
