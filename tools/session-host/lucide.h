/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_LUCIDE_H
#define FS_HOST_LUCIDE_H
#include <windows.h>
#include "lucide_model.h"
#include <map>
#include <tuple>
namespace fs_host_ui
{
inline Icon buttonIcon(HWND child)
{return static_cast<Icon>(reinterpret_cast<UINT_PTR>(GetPropW(child,L"FolderstormIcon")));}
inline void setButtonIcon(HWND child,Icon icon,bool label=false)
{
    if(!child)return;
    const bool previous=GetPropW(child,L"FolderstormIconLabel")!=nullptr;
    if(buttonIcon(child)==icon && previous==label)return;
    if(icon==Icon::None)RemovePropW(child,L"FolderstormIcon");else SetPropW(child,L"FolderstormIcon",reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(icon)));
    if(label)SetPropW(child,L"FolderstormIconLabel",reinterpret_cast<HANDLE>(1));else RemovePropW(child,L"FolderstormIconLabel");
    InvalidateRect(child,nullptr,FALSE);
}
class IconCache
{
    using Key=std::tuple<Icon,int,COLORREF>;
    mutable std::map<Key,HBITMAP> bitmaps;
public:
    IconCache()=default;
    IconCache(const IconCache&)=delete;IconCache& operator=(const IconCache&)=delete;
    ~IconCache(){clear();}
    void clear() const {for(const auto& item:bitmaps)DeleteObject(item.second);bitmaps.clear();}
    bool draw(HDC dc,Icon icon,int x,int y,int size,COLORREF color) const
    {
        const auto* mask=iconMask(icon,size);if(!mask || size<=0)return false;
        const Key key{icon,mask->size,color};auto found=bitmaps.find(key);
        if(found==bitmaps.end())
        {
            std::vector<std::uint8_t> alpha;if(!iconAlpha(*mask,alpha))return false;
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=mask->size;info.bmiHeader.biHeight=-mask->size;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            void* pixels=nullptr;const HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap)return false;
            auto* output=static_cast<std::uint32_t*>(pixels);
            for(std::size_t i=0;i<alpha.size();++i){const auto a=static_cast<std::uint32_t>(alpha[i]);output[i]=(a<<24)|((GetRValue(color)*a/255)<<16)|((GetGValue(color)*a/255)<<8)|(GetBValue(color)*a/255);}
            if(bitmaps.size()>=96)clear();found=bitmaps.emplace(key,bitmap).first;
        }
        const HDC source=CreateCompatibleDC(dc);if(!source)return false;const auto previous=SelectObject(source,found->second);
        const BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool drawn=AlphaBlend(dc,x,y,size,size,source,0,0,mask->size,mask->size,blend)!=FALSE;
        SelectObject(source,previous);DeleteDC(source);return drawn;
    }
};
}
#endif
