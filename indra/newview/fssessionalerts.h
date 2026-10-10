/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_ALERTS_H
#define FS_SESSION_ALERTS_H
#include "fssessionpresentation.h"
namespace fs_session
{
class AlertGate
{
public:
    bool admit(const Message& event,unsigned int policy,std::uint64_t now,bool background)
    {
        if (event.eventType != EventType::Chat && event.eventType != EventType::Attention) return false;
        const auto source = std::make_pair(event.worker,event.generation);
        auto found = mSeen.find(source);
        if (found != mSeen.end() && event.event <= found->second) return false;
        if (mSeen.size() >= 32 && found == mSeen.end()) mSeen.erase(mSeen.begin());
        mSeen[source] = event.event;
        if (!event.event || !(event.flags & AlertEligible) || !background || !policy || !event.eventAt ||
            now < event.eventAt || now-event.eventAt > 3000 || event.recipient == event.account ||
            (event.eventType == EventType::Attention ? policy != 2 : event.topic == Topic::Nearby || event.topic == Topic::Notice)) return false;
        const auto key = AccountKey::from(event); if (!key.valid()) return false;
        const auto previous = mLast.find(key);
        if ((mGlobal && (now < mGlobal || now-mGlobal < 500)) || (previous != mLast.end() && (now < previous->second || now-previous->second < 2000))) return false;
        if (mLast.size() >= 32 && previous == mLast.end()) mLast.erase(mLast.begin());
        mLast[key] = now; mGlobal = now; return true;
    }
private:
    std::map<std::pair<WorkerId,std::uint64_t>,std::uint64_t> mSeen;
    std::map<AccountKey,std::uint64_t> mLast;
    std::uint64_t mGlobal = 0;
};
}
#endif
