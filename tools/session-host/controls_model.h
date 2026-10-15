/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#ifndef FS_HOST_CONTROLS_MODEL_H
#define FS_HOST_CONTROLS_MODEL_H
#include "lucide_model.h"
#include <optional>
namespace fs_host_ui
{
struct ControlState
{
    bool voice=false,backgroundAudio=false,attention=false,chatDetached=false,controlsDetached=false,diagnostics=false;
};
struct CommandVisual
{
    Icon icon=Icon::None;
    std::optional<bool> checked;
    const wchar_t* suffix=L"";
};
inline CommandVisual commandVisual(unsigned int command,const ControlState& state)
{
    using namespace fs_session;
    switch(command)
    {
    case 204:return {state.voice?Icon::Mic:Icon::MicOff,state.voice,state.voice?L"On":L"Off"};
    case 205:return {state.backgroundAudio?Icon::Volume2:Icon::VolumeX,state.backgroundAudio,state.backgroundAudio?L"On":L"Muted"};
    case 274:return {Icon::Bell,state.attention,L""};
    case Details:return {commandIcon(command),state.diagnostics,L""};
    case 270:return {state.chatDetached?Icon::ArrowDownLeft:Icon::ExternalLink,{},L""};
    case 271:return {state.controlsDetached?Icon::ArrowDownLeft:Icon::ExternalLink,{},L""};
    default:return {commandIcon(command),{},L""};
    }
}
enum class VoiceDisplay { Off,Switching,Unavailable,Assigned };
struct VoiceCandidate
{
    int slot=-1;
    bool current=false,ready=false,active=false,owner=false,policyAccepted=false;
};
struct VoiceStatus
{
    VoiceDisplay display=VoiceDisplay::Unavailable;
    int slot=-1;
};
// Presentation only: consume acknowledged state, never grant or route voice.
inline VoiceStatus voiceStatus(bool following,bool switching,int active,const std::vector<VoiceCandidate>& candidates)
{
    if(!following)return {VoiceDisplay::Off,-1};
    if(switching)return {VoiceDisplay::Switching,-1};
    const VoiceCandidate* target=nullptr;unsigned int owners=0;
    for(const auto& candidate:candidates)if(candidate.current)
    {
        if(candidate.owner)++owners;
        if(candidate.slot==active)target=&candidate;
    }
    if(owners>1 || !target || !target->ready || !target->active)return {};
    if(!target->policyAccepted)return {VoiceDisplay::Switching,-1};
    if(owners!=1 || !target->owner)return {};
    return {VoiceDisplay::Assigned,target->slot};
}
}
#endif
