/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../lucide_model.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#ifdef NDEBUG
#error Icon checks require assertions in Release.
#endif
int main()
{
    using namespace fs_host_ui;
    std::vector<std::uint8_t> alpha;
    for(const auto& mask:iconMasks)
    {
        assert(iconAlpha(mask,alpha) && alpha.size()==static_cast<std::size_t>(mask.size*mask.size));
        assert(std::any_of(alpha.begin(),alpha.end(),[](auto value){return value>0;}));
        assert(std::any_of(alpha.begin(),alpha.end(),[](auto value){return value==0;}));
        assert(iconMask(mask.icon,mask.size)==&mask);
    }
    assert(!iconMask(Icon::None,20));assert(iconMask(Icon::Cctv,30)->size==32);
    const std::uint8_t badRuns[]{255,0};const IconMask bad{Icon::Cctv,16,badRuns,2};assert(!iconAlpha(bad,alpha));
    for(const auto& command:fs_session::shellCommands())assert(commandIcon(command.id)!=Icon::None);
    assert(commandIcon(274)==Icon::Bell && commandIcon(fs_session::QuickSettings)==Icon::MousePointerClick);
    assert(commandIcon(fs_session::MonitorSelected)==Icon::Cctv);
    assert(std::strstr(lucideLicense,"ISC") && std::strstr(lucideLicense,"Copyright"));
    std::cout<<"Bundled Lucide masks decode, scale, retain transparent pixels and cover registered actions.\n";
}
