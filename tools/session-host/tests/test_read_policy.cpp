/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionreadpolicy.h"
#include "fssessionchatprefs.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    Message source; source.worker[0] = 1; source.account = "11111111-1111-1111-1111-111111111111"; source.grid = "grid";
    ActiveReadClock clock; Conversation chat{"","Nearby","",Topic::Nearby,2}; chat.lastIncomingEvent = 2;
    clock.update(source,true,1000); assert(!clock.due(chat,1000,60));
    assert(!clock.due(chat,60999,60) && clock.due(chat,61000,60));
    chat.lastIncomingEvent = 3; assert(!clock.due(chat,62000,60));
    assert(clock.due(chat,122000,60));
    clock.update(source,false,123000); assert(!clock.due(chat,999000,60));
    clock.update(source,true,999000); assert(!clock.due(chat,999000,60));
    source.generation++; clock.update(source,true,1000000); assert(!clock.due(chat,1000000,60));
    Conversation late{"new","New group","",Topic::Group,1}; late.lastIncomingEvent = 9;
    assert(!clock.due(late,2000000,60)); // New conversation after a long active session gets its own delay.
    late.restricted = true; assert(!clock.due(late,3000000,60));
    late.restricted = false; late.topic = Topic::Notice; assert(!clock.due(late,3000000,60));
    ChatView view; view.bind(source); source.eventType = EventType::Chat; source.event = 1;
    assert(view.accept(source)); source.event = 2; assert(view.accept(source));
    Message read = source; read.kind = Kind::MarkRead; read.flags = AutomaticRead; read.event = 2;
    source.event = 3; assert(view.accept(source));
    assert(view.markRead(read) && view.conversations.at("").unread == 1); // Late acknowledgement preserves new arrivals.
    source.generation++; read.generation = source.generation; assert(!view.markRead(read));
    read.cursor = 3; read.event = 2;
    assert(readFenceCurrent(read,3) && !readFenceCurrent(read,4)); // Enqueue between render and read defers natively.
    read.event = 4; assert(!readFenceCurrent(read,3));
    read.event = 2; read.topic = Topic::Notice; assert(!readFenceCurrent(read,3));
    ChatPreferences prefs, out; assert(prefs.visibleRead && !prefs.activeRead && prefs.delay == 60);
    prefs.activeRead = true; prefs.delay = 120;
    prefs.sounds.emplace(AccountKey::from(source),CharacterSound{SoundPrivate,"0123456789abcdef0123456789abcdef.wav","Sodie chime.wav"});
    assert(ChatPreferences::decode(prefs.encode(),out) && out.activeRead && out.delay == 120);
    assert(out.sounds.begin()->second.categories == SoundPrivate && out.sounds.begin()->second.name=="Sodie chime.wav");
    auto legacy=prefs.encode();legacy.replace(0,4,"FSC3");legacy.resize(legacy.size()-4-prefs.sounds.begin()->second.name.size());
    assert(ChatPreferences::decode(legacy,out) && out.sounds.begin()->second.name.empty() && out.activeRead);
    assert(ChatPreferences::decode(prefs.encode(),out));
    const auto previous = out.encode(); assert(!ChatPreferences::decode(prefs.encode()+"bad",out) && out.encode() == previous);
    for (std::size_t size = 0; size < previous.size(); ++size)
        assert(!ChatPreferences::decode(previous.substr(0,size),out) && out.encode() == previous);
    const auto inherited = prefs.sound(AccountKey{source.account,"different-grid"},1);
    assert(inherited.categories == 7 && inherited.asset.empty());
    assert(prefs.sound(AccountKey::from(source),2).categories == SoundPrivate); // Explicit mask wins over legacy and stays grid-bound.
    prefs.delay = 0; assert(prefs.encode().empty());
    assert(legacySoundMask(0) == 0 && legacySoundMask(1) == 7 && legacySoundMask(2) == 15);
    assert((!CharacterSound{1,"../secret.wav"}.valid()));
    std::cout << "Delayed active reads, source guards, unseen-arrival acknowledgements and additive settings passed.\n";
}
