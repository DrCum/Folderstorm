/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_AUDIO_H
#define FS_SESSION_AUDIO_H
namespace fs_session
{
constexpr unsigned int AudioVoice = 1, AudioMuteWorld = 2, AudioHostUI = 4;
enum class WorkerAlertDecision { Allowed,PolicyOff,NotReady,Active,Switching,Restricted,Dnd,Muted,NotIncoming,RegionMessage };
inline WorkerAlertDecision backgroundHostAlertDecision(bool ready,bool background,bool switching,bool restricted,bool dnd,bool muted,bool incoming)
{
    if(!ready)return WorkerAlertDecision::NotReady;
    if(switching)return WorkerAlertDecision::Switching;
    if(restricted)return WorkerAlertDecision::Restricted;
    if(dnd)return WorkerAlertDecision::Dnd;
    if(muted)return WorkerAlertDecision::Muted;
    if(!incoming)return WorkerAlertDecision::NotIncoming;
    return background?WorkerAlertDecision::Allowed:WorkerAlertDecision::Active;
}
inline const char* workerAlertDecisionText(WorkerAlertDecision value)
{
    switch(value)
    {
    case WorkerAlertDecision::Allowed:return "Alert: eligible";
    case WorkerAlertDecision::PolicyOff:return "Alert: host alerts not applied in viewer";
    case WorkerAlertDecision::NotReady:return "Alert: viewer not ready";
    case WorkerAlertDecision::Active:return "Alert: receiving character active";
    case WorkerAlertDecision::Switching:return "Alert: character switching";
    case WorkerAlertDecision::Restricted:return "Alert: shared chat or conversation restricted";
    case WorkerAlertDecision::Dnd:return "Alert: Do Not Disturb";
    case WorkerAlertDecision::Muted:return "Alert: viewer master/UI muted or UI volume zero";
    case WorkerAlertDecision::NotIncoming:return "Alert: own, system, or muted sender";
    case WorkerAlertDecision::RegionMessage:return "Alert: region system message";
    }
    return "Alert: unknown";
}
inline bool backgroundHostAlertAllowed(bool ready, bool background, bool switching, bool restricted,
                                       bool dnd, bool muted, bool incoming)
{ return backgroundHostAlertDecision(ready,background,switching,restricted,dnd,muted,incoming)==WorkerAlertDecision::Allowed; }
}
#endif
