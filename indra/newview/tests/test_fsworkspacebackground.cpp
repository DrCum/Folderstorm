/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fsworkspacebackgroundmodel.h"
#include <cassert>
#include <iostream>

using namespace FSWorkspaceBackground;
bool near(double a,double b) { return std::abs(a-b)<.0001; }
int main()
{
    const Rect frame{0,0,3000,1000}, world{1000,0,2000,1000};
    const auto clips=outside(frame,world);
    assert(clips.size()==2);
    double area=0;
    for (size_t i=0;i<clips.size();++i)
    {
        assert(intersect(clips[i],world).empty()); area+=clips[i].width()*clips[i].height();
        for (size_t j=i+1;j<clips.size();++j) assert(intersect(clips[i],clips[j]).empty());
    }
    assert(area==2000000);
    assert(outside(frame,frame).empty());
    assert(outside(frame,{4000,0,5000,1000}).size()==1);
    assert(outside({0,0,0,0},world).empty());
    assert(outside(frame,{500,100,2500,900}).size()==4);
    const Rect target{100,200,500,400};
    auto p=place(target,100,100,Mode::Fill);
    assert(p.destination.left==100 && p.destination.top==500 && p.destination.bottom==100);
    p=place(target,100,100,Mode::Fit);
    assert(p.destination.left==200 && p.destination.right==400 && p.destination.bottom==200);
    p=place(target,100,100,Mode::Stretch); assert(p.destination.width()==400 && p.destination.height()==200);
    p=place(target,100,100,Mode::Center); assert(p.destination.left==250 && p.destination.bottom==250);
    p=place(target,64,32,Mode::Tile); assert(near(p.uv.right,6.25) && near(p.uv.top,6.25));
    p=place(target,100,100,Mode::Span); assert(p.destination.height()==400);
    assert(place(target,0,100,Mode::Fill).destination.empty());
    // Native image aspect must survive the decoder's power-of-two normalization.
    p=place({0,0,3840,2160},1920,1080,Mode::Fit); assert(p.destination.width()==3840 && p.destination.height()==2160);
    auto screens=orderedDisplays({{1920,0,3840,1080},{-1600,0,0,900},{0,0,1920,1080}});
    assert(screens.front().left==-1600 && screens[1].left==0 && screens.size()==3);
    auto moved=screens; for (auto& r:moved) {r.left+=150;r.right+=150;r.top-=10;r.bottom-=10;}
    assert(arrangement(screens)==arrangement(moved));
    auto scaled=screens; for (auto& r:scaled) {r.left*=2;r.right*=2;r.top*=2;r.bottom*=2;}
    assert(arrangement(screens)==arrangement(scaled));
    Config defaults; defaults.common.color={.2f,.3f,.4f};
    Choice inherited; assert(&resolve(inherited,defaults)==&defaults);
    Choice custom; custom.custom=true; assert(&resolve(custom,defaults)==&custom.config);
    Config config; config.arrangement=arrangement(screens); config.displays[0].color={1,0,0};
    assert(forDisplay(config,config.arrangement,0).color[0]==1);
    assert(&forDisplay(config,"different",0)==&config.common);
    auto unplugged=screens; unplugged.pop_back();
    assert(&forDisplay(config,arrangement(unplugged),0)==&config.common && config.displays.size()==1);
    config.common.mode=Mode::Span; assert(&forDisplay(config,config.arrangement,0)==&config.common);
    std::cout<<"Workspace background geometry and inheritance passed\n";
}
