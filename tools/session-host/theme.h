/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_THEME_H
#define FS_HOST_THEME_H
#include <windows.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <algorithm>
namespace fs_host_ui
{
inline void setNativeFont(HWND child,HFONT font)
{
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    wchar_t klass[32]{};GetClassNameW(child,klass,32);
    if(_wcsicmp(klass,L"COMBOBOX") || !(GetWindowLongPtrW(child,GWL_STYLE)&CBS_OWNERDRAWFIXED))return;
    const HDC dc=GetDC(child);if(!dc)return;const auto previous=SelectObject(dc,font);TEXTMETRICW metrics{};
    if(GetTextMetricsW(dc,&metrics)){const int height=static_cast<int>(metrics.tmHeight)+6;SendMessageW(child,CB_SETITEMHEIGHT,0,height);SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),height);}
    if(previous)SelectObject(dc,previous);ReleaseDC(child,dc);
}
class Theme
{
public:
    COLORREF background=0,panel=0,text=0,muted=0,accent=0,border=0,input=0;
    HBRUSH backBrush=nullptr,panelBrush=nullptr,inputBrush=nullptr;
    bool highContrast=false,dark=true;
    unsigned int textPercent=100;
    int uiDpi(unsigned int dpi) const {return MulDiv(static_cast<int>(dpi),static_cast<int>(textPercent),100);}
    int scale(int value,unsigned int dpi) const {return MulDiv(value,uiDpi(dpi),96);}
    Theme(){set(0);} ~Theme(){release();}
    Theme(const Theme&)=delete;Theme& operator=(const Theme&)=delete;
    static LRESULT CALLBACK hoverProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
    {
        if(message==WM_MOUSEMOVE && !GetPropW(child,L"FolderstormHover"))
        {SetPropW(child,L"FolderstormHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,child,0};TrackMouseEvent(&track);InvalidateRect(child,nullptr,FALSE);}
        if(message==WM_MOUSELEAVE){RemovePropW(child,L"FolderstormHover");InvalidateRect(child,nullptr,FALSE);}
        if(message==WM_NCDESTROY){RemovePropW(child,L"FolderstormHover");RemoveWindowSubclass(child,hoverProc,2);}
        return DefSubclassProc(child,message,wp,lp);
    }
    void selection(const DRAWITEMSTRUCT& d) const
    {
        const bool selected=(d.itemState&ODS_SELECTED)!=0;
        FillRect(d.hDC,&d.rcItem,selected && highContrast?GetSysColorBrush(COLOR_HIGHLIGHT):selected?panelBrush:inputBrush);
        SetTextColor(d.hDC,selected && highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):text);
    }
    void selectionFocus(const DRAWITEMSTRUCT& d) const
    {
        if(d.itemState&ODS_SELECTED){const HBRUSH line=CreateSolidBrush(accent);FrameRect(d.hDC,&d.rcItem,line);DeleteObject(line);}
        if(d.itemState&ODS_FOCUS){RECT r=d.rcItem;InflateRect(&r,-2,-2);DrawFocusRect(d.hDC,&r);}
    }
    void release(){if(backBrush)DeleteObject(backBrush);if(panelBrush)DeleteObject(panelBrush);if(inputBrush)DeleteObject(inputBrush);backBrush=panelBrush=inputBrush=nullptr;}
    void set(unsigned int choice)
    {
        DWORD percent=100,bytes=sizeof(percent);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Accessibility",L"TextScaleFactor",RRF_RT_REG_DWORD,nullptr,&percent,&bytes);
        textPercent=(std::max)(100u,(std::min)(225u,static_cast<unsigned int>(percent)));
        HIGHCONTRASTW hc{};hc.cbSize=sizeof(hc);highContrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags&HCF_HIGHCONTRASTON);
        DWORD light=1,size=sizeof(light);if(choice==2)RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
        dark=choice==0 || (choice==2 && light==0);
        background=dark?RGB(17,16,22):RGB(247,245,251);panel=dark?RGB(26,25,33):RGB(255,255,255);
        text=dark?RGB(245,243,250):RGB(33,26,43);muted=dark?RGB(173,169,186):RGB(97,86,111);
        accent=dark?RGB(191,164,252):RGB(108,63,168);border=dark?RGB(115,103,135):RGB(137,125,157);input=background;
        if(highContrast){background=panel=GetSysColor(COLOR_BTNFACE);input=GetSysColor(COLOR_WINDOW);text=GetSysColor(COLOR_WINDOWTEXT);muted=GetSysColor(COLOR_GRAYTEXT);accent=GetSysColor(COLOR_HIGHLIGHT);border=text;}
        release();backBrush=CreateSolidBrush(background);panelBrush=CreateSolidBrush(panel);inputBrush=CreateSolidBrush(input);
    }
    void window(HWND w) const
    {
        if(!w)return;
        EnumChildWindows(w,[](HWND child,LPARAM arg)->BOOL
        {
            const auto& t=*reinterpret_cast<const Theme*>(arg);
            wchar_t name[32]{};GetClassNameW(child,name,32);
            if(!t.highContrast)SetWindowTheme(child,L"",L"");else SetWindowTheme(child,nullptr,nullptr);
            if(_wcsicmp(name,L"BUTTON")==0)
            {
                const auto style=GetWindowLongPtrW(child,GWL_STYLE);const auto kind=style&BS_TYPEMASK;
                if(kind==BS_PUSHBUTTON || kind==BS_DEFPUSHBUTTON)SetWindowLongPtrW(child,GWL_STYLE,(style&~BS_TYPEMASK)|BS_OWNERDRAW);
                if(kind==BS_PUSHBUTTON || kind==BS_DEFPUSHBUTTON || kind==BS_OWNERDRAW)SetWindowSubclass(child,hoverProc,2,0);
            }
            InvalidateRect(child,nullptr,TRUE);return TRUE;
        },reinterpret_cast<LPARAM>(this));
        // Documented DWM attribute; unsupported OS versions keep their native caption.
        using Attribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
        const HMODULE dll=LoadLibraryW(L"dwmapi.dll");
        if(dll){const auto call=reinterpret_cast<Attribute>(GetProcAddress(dll,"DwmSetWindowAttribute"));const BOOL value=dark && !highContrast;if(call)call(w,20,&value,sizeof(value));FreeLibrary(dll);}
        InvalidateRect(w,nullptr,TRUE);
    }
    LRESULT color(UINT message,WPARAM wp,LPARAM lp) const
    {
        const HDC dc=reinterpret_cast<HDC>(wp);const HWND child=reinterpret_cast<HWND>(lp);
        const bool edit=message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX;
        SetTextColor(dc,IsWindowEnabled(child)?text:muted);SetBkColor(dc,edit?input:background);return reinterpret_cast<LRESULT>(edit?inputBrush:backBrush);
    }
    bool button(const DRAWITEMSTRUCT& d) const
    {
        if(d.CtlType!=ODT_BUTTON)return false;
        const bool selected=(d.itemState&ODS_SELECTED)!=0,disabled=(d.itemState&ODS_DISABLED)!=0;
        const bool hovered=GetPropW(d.hwndItem,L"FolderstormHover")!=nullptr;
        FillRect(d.hDC,&d.rcItem,selected && highContrast?GetSysColorBrush(COLOR_HIGHLIGHT):selected || hovered?panelBrush:backBrush);
        const HBRUSH line=CreateSolidBrush((d.itemState&ODS_FOCUS) || hovered?accent:border);FrameRect(d.hDC,&d.rcItem,line);DeleteObject(line);
        wchar_t label[256]{};GetWindowTextW(d.hwndItem,label,256);RECT r=d.rcItem;InflateRect(&r,-5,-2);
        SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,disabled?muted:selected && highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):text);
        const auto old=SelectObject(d.hDC,reinterpret_cast<HFONT>(SendMessageW(d.hwndItem,WM_GETFONT,0,0)));
        DrawTextW(d.hDC,label,-1,&r,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);if(old)SelectObject(d.hDC,old);
        if(d.itemState&ODS_FOCUS){InflateRect(&r,-2,-2);DrawFocusRect(d.hDC,&r);}return true;
    }
};
inline Theme* dialogTheme=nullptr;
}
#endif
