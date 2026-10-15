/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantsession.h"
#include "fssessionassistant.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace fs_assistant;
    assert(sessionMatches(false,"new","",false)); // Existing ordinary clients.
    assert(!sessionMatches(true,"new","",false));
    assert(!sessionMatches(true,"new","old",true));
    assert(!sessionMatches(false,"new","old",true));
    assert(!sessionMatches(true,"","",true));
    assert(sessionMatches(true,"new","new",true));
    assert(!needsActiveCharacter("LLInventory","rename"));
    assert(!needsActiveCharacter("LLAppearance","wearOutfit"));
    assert(!needsActiveCharacter("LLCamera","get"));
    assert(!needsActiveCharacter("LLWorkspace","preview"));
    assert(needsActiveCharacter("LLWorkspace","apply"));
    assert(needsActiveCharacter("LLWorkspace","previous"));
    assert(needsActiveCharacter("LLCamera","snapshot"));
    assert(needsActiveCharacter("LLInventory","snapshotUpload"));
    assert(discoveryScope({"ordinary","hosted","ordinary"},';')=="ordinary;hosted");
    assert(discoveryScope({"bad;path","hosted"},';').empty());
    assert(discoveryScope(std::vector<std::string>(17,"path"),';').empty());
    using namespace fs_session;
    assert(!assistantPauseNeeded(true,false,false,0)); // Wait for first worker identity.
    assert(assistantPauseNeeded(true,false,false,1)); // Includes login-screen workers.
    assert(!assistantPauseNeeded(true,true,false,1));
    assert(assistantPauseNeeded(false,true,false,1));
    assert(!assistantPauseNeeded(true,false,true,1)); // Explicit failure needs user retry.
    Message source;source.worker[0]=1;source.generation=7;source.state=State::Ready;
    source.account="01234567-89ab-cdef-0123-456789abcdef";source.grid="secondlife";source.sequence=1;
    auto info=source;info.kind=Kind::Status;info.width=AssistantEnabled|AssistantReady;info.unread=2;info.event=9;info.height=1;
    assert(assistantInfoOwned(info,source));
    AssistantPreferenceDraft draft;assert(draft.capture(info,source));assert(!draft.edited());
    auto disconnected=source;disconnected.state=State::Disconnected;
    assert(draft.applicable(disconnected)); // Untouched account never blocks global edits.
    assert(!assistantInfoOwned(info,disconnected));
    draft.enabled=false;assert(draft.edited() && draft.applicable(source));
    assert(!draft.applicable(disconnected));
    auto changed=source;changed.generation++;assert(!draft.applicable(changed));
    const auto configure=draft.request();assert(configure.kind==Kind::AssistantConfigure && configure.width==0 && configure.height==info.height && configure.cursor==info.cursor);
    auto sessionOnly=info;sessionOnly.width|=AssistantSessionOnly;AssistantPreferenceDraft runtime;
    assert(runtime.capture(sessionOnly,source));runtime.enabled=false;assert(!runtime.applicable(source));
    const auto attention=assistantAttention(info);
    assert(attention.conversation==AssistantAttentionId && attention.unread==2 && attention.event==9);
    assert(attention.eventType==EventType::Attention && attention.title.find("2")!=std::string::npos);
    info.generation++;assert(!assistantInfoOwned(info,source));info=source;
    info.account="11234567-89ab-cdef-0123-456789abcdef";assert(!assistantInfoOwned(info,source));
    info=source;info.flags=Error;assert(!assistantInfoOwned(info,source));
    for(const auto command:{Kind::AssistantInfo,Kind::AssistantConfigure,Kind::AssistantReview,Kind::AssistantPause})
    {auto request=source;request.kind=command;Frame wire{};Message roundtrip;assert(encode(request,wire) && decode(wire,roundtrip) && roundtrip.kind==command);}
    std::cout<<"Assistant session targeting and active-character eligibility passed.\n";
}
