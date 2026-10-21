/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_NEARBY_CHAT_POLICY_H
#define FS_ASSISTANT_NEARBY_CHAT_POLICY_H
namespace fs_assistant_social
{
enum class NearbySource { Agent, Object, System, Unknown };
enum class NearbyType { Text, Owner, Direct, NonText };
struct NearbyOrigin
{
    NearbySource source=NearbySource::Unknown;
    NearbyType type=NearbyType::NonText;
    bool self=false;
    bool ownedAttachment=false;
    bool emote=false;
};
struct NearbyReceivePolicy
{
    bool enabled=false;
    bool recvChat=false,recvChatException=false;
    bool recvChatFrom=false,recvChatFromMatch=false;
    bool recvEmote=false,recvEmoteException=false;
    bool recvEmoteFrom=false,recvEmoteFromMatch=false;
};
// Matches the source/type/exemption selection in process_chat_from_simulator.
// The assistant conservatively omits a row whenever native receive policy would
// filter its content. It never republishes an original retained/translated body
// merely because that body was visible when it was first enqueued.
inline bool nearbyReceiveAllowed(const NearbyOrigin& origin,const NearbyReceivePolicy& policy)
{
    if(origin.source==NearbySource::Unknown || origin.type==NearbyType::NonText)return false;
    if(!policy.enabled)return true;
    const bool filter=(origin.source==NearbySource::Agent && !origin.self) ||
        (origin.source==NearbySource::Object && !origin.ownedAttachment &&
            origin.type!=NearbyType::Owner && origin.type!=NearbyType::Direct);
    if(!filter)return true;
    if(origin.emote)return !(policy.recvEmote && !policy.recvEmoteException) &&
        !(policy.recvEmoteFrom && policy.recvEmoteFromMatch);
    return !(policy.recvChat && !policy.recvChatException) &&
        !(policy.recvChatFrom && policy.recvChatFromMatch);
}
}
#endif
