/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_DIALOG_LAYOUT_H
#define FS_SESSION_DIALOG_LAYOUT_H
#include <algorithm>
#include <cstdint>
namespace fs_session
{
struct EditorScrollLayout { int width, height; bool horizontal, vertical; };
inline EditorScrollLayout editorScrollLayout(int contentWidth,int contentHeight,int width,int height,int bar)
{
    bool horizontal=false,vertical=false;
    for(int i=0;i<3;++i)
    {
        horizontal=contentWidth>std::max(1,width-(vertical?bar:0));
        vertical=contentHeight>std::max(1,height-(horizontal?bar:0));
    }
    return {std::max(1,width-(vertical?bar:0)),std::max(1,height-(horizontal?bar:0)),horizontal,vertical};
}
struct ScrollThumbLayout
{
    int edge=0, start=0, length=0, travel=0, minimum=0, maximum=0;
};
inline ScrollThumbLayout scrollThumbLayout(int extent,int cross,int minimum,int maximum,unsigned int page,int position)
{
    ScrollThumbLayout r;r.edge=(std::max)(0,(std::min)(cross,extent/2));
    const int track=(std::max)(0,extent-2*r.edge);
    const auto total=(std::max)(std::int64_t{1},static_cast<std::int64_t>(maximum)-minimum+1);
    r.minimum=minimum;r.maximum=static_cast<int>((std::max)(static_cast<std::int64_t>(minimum),static_cast<std::int64_t>(maximum)-page+1));
    r.length=(std::min)(track,(std::max)(r.edge,static_cast<int>(static_cast<std::int64_t>(track)*page/total)));
    r.travel=track-r.length;
    const auto range=static_cast<std::int64_t>(r.maximum)-r.minimum;
    const int bounded=(std::max)(r.minimum,(std::min)(r.maximum,position));
    r.start=r.edge+(range?static_cast<int>(static_cast<std::int64_t>(r.travel)*(bounded-r.minimum)/range):0);
    return r;
}
inline int scrollPositionAt(const ScrollThumbLayout& r,int coordinate,int grab)
{
    if(!r.travel)return r.minimum;
    const int offset=(std::max)(0,(std::min)(r.travel,coordinate-grab-r.edge));
    return r.minimum+static_cast<int>(static_cast<std::int64_t>(offset)*(r.maximum-r.minimum)/r.travel);
}
}
#endif
