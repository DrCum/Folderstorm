/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_TOOLBAR_LAYOUT_H
#define FS_SESSION_TOOLBAR_LAYOUT_H
#include <algorithm>
#include <vector>
#include <cstdint>
namespace fs_session
{
enum class DockedArea : unsigned int { Automatic,Full,LeftHalf,RightHalf,LeftThird,MiddleThird,RightThird };
struct ToolbarRange {int x=0,width=0;};
inline ToolbarRange dockedToolbarRange(int width,DockedArea area,int windowX=0,const std::vector<ToolbarRange>& monitors={})
{
    width=(std::max)(0,width);
    if(area==DockedArea::Automatic)
    {
        int substantial=0,narrowest=0;
        for(const auto& monitor:monitors)if(monitor.width>0)
        {
            const auto overlap=(std::min)(static_cast<std::int64_t>(windowX)+width,static_cast<std::int64_t>(monitor.x)+monitor.width)-(std::max)(static_cast<std::int64_t>(windowX),static_cast<std::int64_t>(monitor.x));
            if(overlap*3>=monitor.width){++substantial;narrowest=narrowest?(std::min)(narrowest,monitor.width):monitor.width;}
        }
        area=substantial==3 && static_cast<std::int64_t>(width)*2>=static_cast<std::int64_t>(narrowest)*5?DockedArea::MiddleThird:
            substantial==2 && static_cast<std::int64_t>(width)*2>=static_cast<std::int64_t>(narrowest)*3?DockedArea::RightHalf:DockedArea::Full;
    }
    const int secondThird=static_cast<int>(static_cast<std::int64_t>(width)*2/3);
    switch(area)
    {
    case DockedArea::LeftHalf:return {0,width/2};
    case DockedArea::RightHalf:return {width/2,width-width/2};
    case DockedArea::LeftThird:return {0,width/3};
    case DockedArea::MiddleThird:return {width/3,secondThird-width/3};
    case DockedArea::RightThird:return {secondThird,width-secondThird};
    default:return {0,width};
    }
}
struct ToolbarGroups
{
    int selector=0,mainCount=4,leftEnd=0,rightStart=0,centerStart=0,centerAvailable=0;
    bool mainOverflow=false;
};
inline ToolbarGroups toolbarGroups(int width,bool quick,bool pinned=false,const std::vector<int>& mainWidths={})
{
    width=(std::max)(0,width);
    ToolbarGroups g;const int end=(std::max)(0,width-4),right=pinned?92:60;
    g.rightStart=(std::max)(4,end-right);g.selector=(std::max)(0,(std::min)(180,g.rightStart-12));
    const int reserve=quick?28:0;
    const auto buttons=[&](){int size=g.mainOverflow?32:0;for(int i=0;i<g.mainCount;++i)size+=i<static_cast<int>(mainWidths.size())?(std::max)(1,mainWidths[static_cast<std::size_t>(i)]):32;return size;};
    const auto left=[&](){return 4+g.selector+buttons();};
    while(g.mainCount>0 && left()+reserve+16>g.rightStart)
    {
        if(g.selector>80)g.selector=(std::max)(80,g.rightStart-reserve-16-4-buttons());
        else {--g.mainCount;g.mainOverflow=true;}
    }
    if(left()+reserve+16>g.rightStart)g.selector=(std::max)(0,g.rightStart-reserve-16-4-buttons());
    g.leftEnd=(std::min)(g.rightStart,left());g.centerStart=g.leftEnd+8;
    g.centerAvailable=(std::max)(0,g.rightStart-8-g.centerStart);
    return g;
}
inline int centeredQuickStart(int width,const ToolbarGroups& g,int used)
{return (std::max)(g.centerStart,(std::min)((width-used)/2,g.rightStart-8-used));}
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
