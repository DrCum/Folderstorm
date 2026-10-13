/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_LUCIDE_MODEL_H
#define FS_HOST_LUCIDE_MODEL_H
#include "lucide_data.h"
#include "fssessionexperience.h"
#include <vector>
namespace fs_host_ui
{
inline const IconMask* iconMask(Icon icon,int size)
{
    const IconMask* last=nullptr;
    for(const auto& mask:iconMasks)if(mask.icon==icon){last=&mask;if(mask.size>=size)return last;}
    return last;
}
inline bool iconAlpha(const IconMask& mask,std::vector<std::uint8_t>& alpha)
{
    if(mask.size<1 || mask.size>96 || mask.length%2)return false;
    const auto count=static_cast<std::size_t>(mask.size*mask.size);alpha.clear();alpha.reserve(count);
    for(std::size_t i=0;i<mask.length;i+=2)
    {if(!mask.runs[i] || mask.runs[i]>count-alpha.size())return false;alpha.insert(alpha.end(),mask.runs[i],mask.runs[i+1]);}
    return alpha.size()==count;
}
inline Icon commandIcon(unsigned int command)
{
    using namespace fs_session;
    switch(command)
    {
    case MonitorSelected:return Icon::Cctv;
    case 240:return Icon::RotateCw;
    case 201:case RestoreShell:case RecoverShell:case ViewActions:case SetActions:return Icon::LayoutDashboard;
    case 204:return Icon::Mic;
    case 205:return Icon::VolumeX;
    case 208:case NextAttention:return Icon::ChevronDown;
    case PreviousAttention:return Icon::ChevronUp;
    case Details:case 206:return Icon::Info;
    case 274:case AttentionSettings:return Icon::Bell;
    case 270:case 271:return Icon::ExternalLink;
    case SelectedActions:case SettingsActions:return Icon::Settings;
    case AppearanceSettings:return Icon::Palette;
    case QuickSettings:return Icon::MousePointerClick;
    default:return Icon::None;
    }
}
}
#endif
