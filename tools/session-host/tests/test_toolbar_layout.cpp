/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessiontoolbarlayout.h"
#include <cassert>
#include <iostream>
#include <limits>
#ifdef NDEBUG
#error Toolbar checks require assertions in Release.
#endif
using namespace fs_session;
int main()
{
    const std::vector<QuickButtonSize> buttons{{100,28},{80,28},{75,40}};
    auto r=quickToolbarLayout(300,buttons);assert(r.visible==3 && !r.overflow && r.widths[0]==100);
    r=quickToolbarLayout(200,buttons);assert(r.visible==3 && !r.overflow && r.widths[0]==100 && r.widths[2]==40);
    r=quickToolbarLayout(130,buttons);assert(r.visible==3 && !r.overflow && r.widths[0]==28);
    r=quickToolbarLayout(90,buttons);assert(r.visible==1 && r.overflow && r.used<=90);
    r=quickToolbarLayout(10,buttons);assert(r.visible==0 && r.overflow && r.used==10);
    r=quickToolbarLayout(0,buttons);assert(!r.visible && r.used==0);
    r=quickToolbarLayout(300,{});assert(!r.visible && !r.overflow && r.used==0);
    for(int width=0;width<600;++width){r=quickToolbarLayout(width,buttons);assert(r.used<=width && r.visible<=buttons.size());}
    const std::vector<ToolbarRange> two{{-1920,1920},{0,1920}},three{{-1920,1920},{0,1920},{1920,1920}};
    auto band=dockedToolbarRange(3840,DockedArea::Automatic,-1920,two);assert(band.x==1920 && band.width==1920);
    band=dockedToolbarRange(5760,DockedArea::Automatic,-1920,three);assert(band.x==1920 && band.width==1920);
    band=dockedToolbarRange(1920,DockedArea::Automatic,0,two);assert(band.x==0 && band.width==1920);
    band=dockedToolbarRange(1930,DockedArea::Automatic,-10,two);assert(band.x==0 && band.width==1930); // Tiny seam overlap.
    band=dockedToolbarRange(2100,DockedArea::Automatic,-1050,two);assert(band.x==0); // Not a broad span.
    band=dockedToolbarRange(3840,DockedArea::LeftHalf,-1920,two);assert(band.x==0 && band.width==1920);
    band=dockedToolbarRange((std::numeric_limits<int>::max)(),DockedArea::RightThird);assert(band.x>0 && band.width>0 && band.x+band.width==(std::numeric_limits<int>::max)());
    for(int width=1;width<1000;++width)for(unsigned int area=0;area<7;++area)
    {band=dockedToolbarRange(width,static_cast<DockedArea>(area));assert(band.x>=0 && band.width>=0 && band.x+band.width<=width);}
    for(int width=180;width<3000;++width)for(bool quick:{false,true})for(bool pin:{false,true})
    {
        const auto group=toolbarGroups(width,quick,pin);assert(group.selector>=0 && group.leftEnd<=group.rightStart && group.rightStart<width);
        r=quickToolbarLayout(group.centerAvailable,quick?buttons:std::vector<QuickButtonSize>{});
        const int start=centeredQuickStart(width,group,r.used);assert(start>=group.leftEnd && start+r.used<=group.rightStart);
        assert(group.mainCount>=0 && group.mainCount<=4);
    }
    const auto roomy=toolbarGroups(1920,true);r=quickToolbarLayout(roomy.centerAvailable,buttons);
    assert(centeredQuickStart(1920,roomy,r.used)==(1920-r.used)/2 && roomy.mainCount==4);
    const auto narrow=toolbarGroups(186,true);assert(narrow.mainOverflow && narrow.mainCount<4);
    // Permanent labels consume their measured widths independently of quick-action mode.
    for(const auto& measured: {std::vector<int>{32,32,32,32},std::vector<int>{70,60,95,120},std::vector<int>{95,85,120,145}})
        for(int width=180;width<3000;++width)for(bool quick:{false,true})for(bool pin:{false,true})
        {
            const auto group=toolbarGroups(width,quick,pin,measured);
            int occupied=8+group.selector+(group.mainOverflow?32:0);
            for(int i=0;i<group.mainCount;++i)occupied+=measured[static_cast<std::size_t>(i)];
            assert(occupied<=group.rightStart && group.selector>=0);
            r=quickToolbarLayout(group.centerAvailable,quick?buttons:std::vector<QuickButtonSize>{});
            const auto start=centeredQuickStart(width,group,r.used);
            assert(start>=occupied && start+r.used<=group.rightStart);
        }
    std::cout<<"Quick actions retain order, collapse labels, and fit bounded overflow.\n";
}
