/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_TOOLBAR_LAYOUT_H
#define FS_SESSION_TOOLBAR_LAYOUT_H
#include <algorithm>
#include <vector>
namespace fs_session
{
struct QuickButtonSize {int full=0,compact=0;};
struct QuickToolbarLayout
{
    std::vector<int> widths;
    std::size_t visible=0;
    int used=0;
    bool overflow=false;
};
// Earlier actions retain labels longest. Hidden actions retain their order in overflow.
inline QuickToolbarLayout quickToolbarLayout(int available,const std::vector<QuickButtonSize>& buttons,int gap=4,int more=28)
{
    QuickToolbarLayout r;available=(std::max)(0,available);
    for(const auto& b:buttons)r.widths.push_back((std::max)(b.compact,b.full));
    const auto total=[&](std::size_t count){int sum=0;for(std::size_t i=0;i<count;++i)sum+=r.widths[i]+(i?gap:0);return sum;};
    for(std::size_t i=buttons.size();total(buttons.size())>available && i>0;--i)r.widths[i-1]=(std::max)(1,buttons[i-1].compact);
    r.visible=buttons.size();r.overflow=total(r.visible)>available;
    const int budget=r.overflow?(std::max)(0,available-more-gap):available;
    while(r.visible && total(r.visible)>budget)--r.visible;
    r.used=total(r.visible)+(r.overflow?(r.visible?gap:0)+(std::min)(available,more):0);
    return r;
}
}
#endif
