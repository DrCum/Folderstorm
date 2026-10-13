/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_DIALOG_LAYOUT_H
#define FS_SESSION_DIALOG_LAYOUT_H
#include <algorithm>
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
}
#endif
