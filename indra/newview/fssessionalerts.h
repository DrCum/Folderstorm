/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_ALERTS_H
#define FS_SESSION_ALERTS_H
#include "fssessionchatprefs.h"
namespace fs_session
{
enum class AlertDecision { Allowed,NotAnAlert,Duplicate,InvalidEvent,Restricted,WorkerIneligible,BackgroundUnavailable,CategoryOff,Stale,OwnMessage,InvalidSource,BurstLimited };
inline const char* alertDecisionText(AlertDecision value)
{
    switch(value)
    {
    case AlertDecision::Allowed:return "Playback requested";
    case AlertDecision::NotAnAlert:return "Not an alert event";
    case AlertDecision::Duplicate:return "Duplicate event";
    case AlertDecision::InvalidEvent:return "Missing event identity";
    case AlertDecision::Restricted:return "Conversation or shared chat restricted";
    case AlertDecision::WorkerIneligible:return "Viewer did not mark the event eligible";
    case AlertDecision::BackgroundUnavailable:return "Character active, unavailable, switching, or host alerts not applied";
    case AlertDecision::CategoryOff:return "Alert category disabled";
    case AlertDecision::Stale:return "Event too old or missing arrival time";
    case AlertDecision::OwnMessage:return "Own outgoing message";
    case AlertDecision::InvalidSource:return "Invalid account/grid identity";
    case AlertDecision::BurstLimited:return "Alert burst limit";
    }
    return "Unknown";
}
class AlertGate
{
public:
    bool admit(const Message& event,unsigned int policy,std::uint64_t now,bool background)
    { return admitCategories(event,legacySoundMask(policy),now,background); }
    bool admitCategories(const Message& event,unsigned int categories,std::uint64_t now,bool background)
    {return decide(event,categories,now,background)==AlertDecision::Allowed;}
    AlertDecision decide(const Message& event,unsigned int categories,std::uint64_t now,bool background)
    {
        if (event.eventType != EventType::Chat && event.eventType != EventType::Attention) return AlertDecision::NotAnAlert;
        const auto source = std::make_pair(event.worker,event.generation);
        auto found = mSeen.find(source);
        if (found != mSeen.end() && event.event <= found->second) return AlertDecision::Duplicate;
        if (mSeen.size() >= 32 && found == mSeen.end()) mSeen.erase(mSeen.begin());
        mSeen[source] = event.event;
        if(!event.event)return AlertDecision::InvalidEvent;
        if(event.flags&(ChatRestricted|ConversationRestricted))return AlertDecision::Restricted;
        if(!(event.flags&AlertEligible))return AlertDecision::WorkerIneligible;
        if(!background)return AlertDecision::BackgroundUnavailable;
        if(!(categories&soundCategory(event)))return AlertDecision::CategoryOff;
        if(!event.eventAt || now<event.eventAt || now-event.eventAt>3000)return AlertDecision::Stale;
        if(event.recipient==event.account)return AlertDecision::OwnMessage;
        const auto key = AccountKey::from(event); if (!key.valid()) return AlertDecision::InvalidSource;
        const auto previous = mLast.find(key);
        if ((mGlobal && (now < mGlobal || now-mGlobal < 500)) || (previous != mLast.end() && (now < previous->second || now-previous->second < 2000))) return AlertDecision::BurstLimited;
        if (mLast.size() >= 32 && previous == mLast.end()) mLast.erase(mLast.begin());
        mLast[key] = now; mGlobal = now; return AlertDecision::Allowed;
    }
private:
    std::map<std::pair<WorkerId,std::uint64_t>,std::uint64_t> mSeen;
    std::map<AccountKey,std::uint64_t> mLast;
    std::uint64_t mGlobal = 0;
};
}
#endif
