/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fsassistantnearbychatpolicy.h"
#include "../fsassistantsocialmodel.h"
#include <cassert>
#include <string>
using namespace fs_assistant_social;
struct RetainedNearby {NearbyOrigin origin;std::string nativeText;};
int main()
{
    NearbyOrigin agent;agent.source=NearbySource::Agent;agent.type=NearbyType::Text;
    NearbyReceivePolicy policy;policy.enabled=true;
    Stream<RetainedNearby> stream;
    assert(nearbyReceiveAllowed(agent,policy));stream.append({agent,"private original (translation)"},128);
    // Actual production delivery helper is rerun after enqueue: no original or
    // translated text is eligible for history, search or event delivery.
    policy.recvChat=true;
    assert(!nearbyReceiveAllowed(stream.entries().front().value.origin,policy));
    policy.recvChatException=true;assert(nearbyReceiveAllowed(agent,policy));
    policy.recvChatFrom=true;policy.recvChatFromMatch=true;
    assert(!nearbyReceiveAllowed(agent,policy)); // from-specific deny wins
    policy.recvChatFromMatch=false;assert(nearbyReceiveAllowed(agent,policy));
    agent.self=true;policy.recvChatException=false;policy.recvChatFromMatch=true;
    assert(nearbyReceiveAllowed(agent,policy)); // native self exemption
    agent.self=false;agent.emote=true;
    assert(nearbyReceiveAllowed(agent,policy)); // recvchat does not filter emotes
    stream.append({agent,"/me private emote (translation)"},128);
    policy.recvEmote=true;
    assert(!nearbyReceiveAllowed(stream.entries().back().value.origin,policy));
    policy.recvEmoteException=true;assert(nearbyReceiveAllowed(agent,policy));
    policy.recvEmoteFrom=true;policy.recvEmoteFromMatch=true;
    assert(!nearbyReceiveAllowed(agent,policy));
    policy.recvEmoteFromMatch=false;assert(nearbyReceiveAllowed(agent,policy));

    NearbyOrigin object;object.source=NearbySource::Object;object.type=NearbyType::Text;
    assert(!nearbyReceiveAllowed(object,policy));
    object.ownedAttachment=true;assert(nearbyReceiveAllowed(object,policy));
    object.ownedAttachment=false;assert(!nearbyReceiveAllowed(object,policy)); // lost/unloaded ownership evidence
    object.type=NearbyType::Owner;assert(nearbyReceiveAllowed(object,policy));
    object.type=NearbyType::Direct;assert(nearbyReceiveAllowed(object,policy));
    object.type=NearbyType::Text;object.emote=true;
    policy.recvEmoteException=false;assert(!nearbyReceiveAllowed(object,policy));
    object.source=NearbySource::System;assert(nearbyReceiveAllowed(object,policy));
    object.source=NearbySource::Unknown;assert(!nearbyReceiveAllowed(object,policy));
    object.source=NearbySource::Agent;object.type=NearbyType::NonText;
    assert(!nearbyReceiveAllowed(object,policy));

    // Native-redacted text is retained verbatim, never reconstituted from an
    // earlier original when receive restrictions are lifted.
    RetainedNearby nativeRedacted{agent,"/me ..."};
    policy.enabled=false;
    assert(nearbyReceiveAllowed(nativeRedacted.origin,policy));
    assert(nativeRedacted.nativeText=="/me ...");
    policy.enabled=true;
    assert(!nearbyReceiveAllowed(nativeRedacted.origin,policy)); // conservative omission
}
