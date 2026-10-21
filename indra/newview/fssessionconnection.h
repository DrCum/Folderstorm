/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CONNECTION_H
#define FS_SESSION_CONNECTION_H
#include "fssessionprotocol.h"
namespace fs_session
{
constexpr std::uint64_t ReplyStallMs = 12000, StartupReplyStallMs = 60000, ControlSilenceMs = 15000;
inline bool connectionOverdue(std::uint64_t now,std::uint64_t since,std::uint64_t limit)
{return now>=since && now-since>limit;}
inline bool sameConnectionSession(const Message& a,const Message& b)
{return a.worker==b.worker && a.generation==b.generation && a.account==b.account && a.grid==b.grid;}
inline bool sessionModeBlocked(Mode requested,bool ready,bool applied,bool focusLocked,bool modal,bool preferences)
{
    // Bringing an account forward must expose its unsaved Preferences draft;
    // hiding an account with that draft open still requires the user to finish.
    return (requested==Mode::Active&&!ready) || !applied || focusLocked || modal ||
        (requested!=Mode::Active&&preferences);
}

// A late reply is drained exactly once. It never becomes a new submission or
// a delayed handoff grant. Only an acknowledged reset can end quarantine.
class LinkRecovery
{
public:
    enum class Phase { Healthy, Draining, Reset };
    bool begin(const Message& source,bool wasActive)
    {
        if(active())return false;
        origin=source;restore=wasActive;phase=Phase::Draining;return true;
    }
    void drained(const Message& source)
    {if(active()){restore=restore&&sameConnectionSession(origin,source);phase=Phase::Reset;}}
    bool finish(const Message& request,const Message& response,bool inputReleased)
    {
        if(phase!=Phase::Reset || request.kind!=Kind::Recover || response.kind!=Kind::Status ||
            request.sequence!=response.sequence || !sameConnectionSession(request,response) ||
            response.mode!=Mode::Warm || !inputReleased ||
            (response.flags&(Error|ControlPaused|Promoting|Transitioning|VoiceOwner|Embedded)))return false;
        restore=restore&&sameConnectionSession(origin,response);phase=Phase::Healthy;return true;
    }
    bool active() const {return phase!=Phase::Healthy;}
    bool resetReady() const {return phase==Phase::Reset;}
    bool resumeActive() const {return !active()&&restore;}
    void abandonResume(){restore=false;}
private:
    Phase phase=Phase::Healthy;
    Message origin;
    bool restore=false;
};
enum class ConnectionEvent { Stalled, Recovered, PeerClosed, ProtocolError, ResetFailed };
inline std::string connectionDiagnostic(ConnectionEvent event,int slot,std::uint32_t pid,
                                        const Message& request,std::uint64_t elapsed)
{
    const char* reasons[]={"reply_stalled","control_recovered","peer_or_pipe_closed","invalid_reply","reset_refused"};
    return std::string(reasons[static_cast<unsigned int>(event)])+" slot="+std::to_string(slot+1)+
        " pid="+std::to_string(pid)+" kind="+std::to_string(static_cast<unsigned int>(request.kind))+
        " sequence="+std::to_string(request.sequence)+" generation="+std::to_string(request.generation)+
        " elapsed_ms="+std::to_string(elapsed);
}
}
#endif
