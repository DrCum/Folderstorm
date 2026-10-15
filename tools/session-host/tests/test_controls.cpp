/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../controls_model.h"
#include <cassert>
int main()
{
    using namespace fs_host_ui;
    ControlState state;
    auto visual=commandVisual(205,state);assert(visual.checked && !*visual.checked && visual.icon==Icon::VolumeX);
    state.backgroundAudio=true;visual=commandVisual(205,state);assert(*visual.checked && visual.icon==Icon::Volume2);
    visual=commandVisual(204,state);assert(visual.checked && !*visual.checked && visual.icon==Icon::MicOff);
    state.voice=true;assert(commandVisual(204,state).icon==Icon::Mic && *commandVisual(204,state).checked);
    state.chatDetached=true;visual=commandVisual(270,state);assert(!visual.checked && visual.icon==Icon::ArrowDownLeft);
    assert(!commandVisual(240,state).checked); // Reusing an audio slot for Restart must restore action semantics.
    std::vector<VoiceCandidate> candidates{{0,true,true,true,true,true},{1,true,true,false,false,true}};
    assert(voiceStatus(true,false,0,candidates).display==VoiceDisplay::Assigned);
    assert(voiceStatus(true,false,0,candidates).slot==0);
    assert(voiceStatus(false,false,0,candidates).display==VoiceDisplay::Off);
    assert(voiceStatus(true,true,0,candidates).display==VoiceDisplay::Switching);
    candidates[0].policyAccepted=false;assert(voiceStatus(true,false,0,candidates).display==VoiceDisplay::Switching);
    candidates[0].policyAccepted=true;candidates[1].owner=true;assert(voiceStatus(true,false,0,candidates).display==VoiceDisplay::Unavailable);
    candidates[1].owner=false;candidates[0].current=false;assert(voiceStatus(true,false,0,candidates).slot==-1);
    candidates[0].current=true;candidates[0].ready=false;assert(voiceStatus(true,false,0,candidates).slot==-1);
    candidates[0].ready=true;candidates[0].active=false;assert(voiceStatus(true,false,0,candidates).slot==-1);
    candidates[0].active=true;candidates[0].owner=false;assert(voiceStatus(true,false,0,candidates).slot==-1);
    assert(voiceStatus(true,false,1,candidates).slot==-1); // A management/Chat selection cannot assign voice.
    assert(voiceStatus(true,false,-1,candidates).slot==-1);
    assert(voiceStatus(true,false,0,{}).slot==-1);
}
