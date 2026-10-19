/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionconnection.h"
#include "fssessionmonitorchat.h"
#include <cassert>
using namespace fs_session;
Message source()
{
    Message m;m.worker[0]=1;m.pid=42;m.generation=3;m.sequence=10;m.state=State::Ready;m.mode=Mode::Active;
    m.account="11111111-1111-1111-1111-111111111111";m.grid="grid";return m;
}
int main()
{
    assert(!connectionOverdue(12000,0,ReplyStallMs));assert(connectionOverdue(12001,0,ReplyStallMs));
    assert(!connectionOverdue(1,20,ReplyStallMs)); // No unsigned clock-underflow timeout.
    assert(!connectionOverdue(59999,0,StartupReplyStallMs));
    assert(connectionOverdue(15001,0,ControlSilenceMs));
    assert(!sessionModeBlocked(Mode::Active,true,true,false,false,true));
    assert(sessionModeBlocked(Mode::Warm,true,true,false,false,true));
    assert(sessionModeBlocked(Mode::Economy,true,true,false,false,true));
    assert(sessionModeBlocked(Mode::Active,false,true,false,false,false));
    assert(sessionModeBlocked(Mode::Active,true,true,true,false,true));
    assert(sessionModeBlocked(Mode::Active,true,true,false,true,true));
    auto s=source();LinkRecovery recovery;
    assert(recovery.begin(s,true));assert(recovery.active()&&!recovery.resetReady());
    assert(!recovery.begin(s,false)); // Repeated timer ticks cannot restart the transaction.
    auto reset=s;reset.kind=Kind::Recover;++reset.sequence;
    auto reply=reset;reply.kind=Kind::Status;reply.mode=Mode::Warm;
    assert(!recovery.finish(reset,reply,true)); // The original reply must be drained first.
    recovery.drained(s);assert(recovery.resetReady());
    assert(!recovery.finish(s,reply,true)); // An old mode/chat acknowledgement cannot release quarantine.
    assert(!recovery.finish(reset,reply,false)); // Native input-owner hint still present.
    for(const auto flag:{Error,ControlPaused,Promoting,Transitioning,VoiceOwner,Embedded})
    {auto rejected=reply;rejected.flags=flag;assert(!recovery.finish(reset,rejected,true));}
    for(int field=0;field<5;++field)
    {
        auto wrong=reply;
        if(field==0)++wrong.sequence;
        if(field==1)++wrong.generation;
        if(field==2)++wrong.worker[0];
        if(field==3)wrong.account="22222222-2222-2222-2222-222222222222";
        if(field==4)wrong.grid="other-grid";
        assert(!recovery.finish(reset,wrong,true));
    }
    auto stillActive=reply;stillActive.mode=Mode::Active;assert(!recovery.finish(reset,stillActive,true));
    assert(recovery.finish(reset,reply,true));assert(!recovery.active()&&recovery.resumeActive());

    LinkRecovery loginChanged;assert(loginChanged.begin(s,true));
    auto newer=s;++newer.generation;newer.account="22222222-2222-2222-2222-222222222222";
    loginChanged.drained(newer);reset=newer;reset.kind=Kind::Recover;++reset.sequence;
    reply=reset;reply.kind=Kind::Status;reply.mode=Mode::Warm;
    assert(loginChanged.finish(reset,reply,true));assert(!loginChanged.resumeActive());
    LinkRecovery stopped;assert(stopped.begin(s,true));stopped.abandonResume();stopped.drained(s);
    reset=s;reset.kind=Kind::Recover;reply=reset;reply.kind=Kind::Status;reply.mode=Mode::Warm;
    assert(stopped.finish(reset,reply,true));assert(!stopped.resumeActive());

    // Paused private composition keeps the source/destination and its draft;
    // sending and marking read are refused until the reset is acknowledged.
    ChatView chat;chat.bind(s);Message catalog=s;catalog.eventType=EventType::Conversation;catalog.topic=Topic::Private;
    catalog.conversation="33333333-3333-3333-3333-333333333333";catalog.title="Private";
    assert(chat.accept(catalog));
    chat.conversations.at(catalog.conversation).draft="private text";
    MonitorSet monitors;const int monitor=monitors.open(1,s,0,0);assert(monitor>=0);
    MonitorChat pane;assert(pane.bind(monitors.bindings[monitor],&s,&chat));
    assert(pane.select(monitors.bindings[monitor],s,chat,catalog.conversation));
    s.flags=ControlPaused;
    assert(!pane.bind(monitors.bindings[monitor],&s,&chat)); // Same binding, not a new Nearby selection.
    assert(!pane.request(Kind::SendChat,monitors.bindings[monitor],s,chat));
    assert(!pane.request(Kind::MarkRead,monitors.bindings[monitor],s,chat));
    s.flags=0;
    const auto send=pane.request(Kind::SendChat,monitors.bindings[monitor],s,chat);
    assert(send && send->topic==Topic::Private && send->conversation==catalog.conversation && send->account==s.account);
    assert(chat.conversations.at(catalog.conversation).draft=="private text");
    Frame frame{};reset=s;reset.kind=Kind::Recover;reset.flags=ControlPaused;
    assert(encode(reset,frame));Message decoded;assert(decode(frame,decoded)&&decoded.kind==Kind::Recover&&(decoded.flags&ControlPaused));
    const auto diagnostic=connectionDiagnostic(ConnectionEvent::Stalled,2,42,reset,13000);
    assert(diagnostic.find("slot=3 pid=42")!=std::string::npos && diagnostic.find("elapsed_ms=13000")!=std::string::npos);
    assert(diagnostic.find(s.account)==std::string::npos && diagnostic.find(s.grid)==std::string::npos);
}
