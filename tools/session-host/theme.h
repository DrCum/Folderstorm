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
    void paintScrollbar(HWND child,HDC dc,const RECT& rect,bool vertical,int kind) const
    {
        FillRect(dc,&rect,backBrush);
        const int length=static_cast<int>(vertical?rect.bottom-rect.top:rect.right-rect.left),cross=static_cast<int>(vertical?rect.right-rect.left:rect.bottom-rect.top);
        const int edge=(std::min)(cross,length/2),track=(std::max)(0,length-2*edge);
        SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(child,kind,&info);
        const int total=(std::max)(1,info.nMax-info.nMin+1),thumb=(std::min)(track,(std::max)(edge,MulDiv(track,static_cast<int>(info.nPage),total)));
        const int range=(std::max)(1,total-static_cast<int>(info.nPage)),offset=edge+MulDiv(track-thumb,info.nPos-info.nMin,range);
        RECT grip=vertical?RECT{rect.left+2,rect.top+offset,rect.right-2,rect.top+offset+thumb}:RECT{rect.left+offset,rect.top+2,rect.left+offset+thumb,rect.bottom-2};
        FillRect(dc,&grip,panelBrush);const HBRUSH outline=CreateSolidBrush(border);FrameRect(dc,&grip,outline);DeleteObject(outline);
        const HPEN pen=CreatePen(PS_SOLID,1,text);const auto old=SelectObject(dc,pen);const int arm=(std::max)(3,cross/5);
        for(int end=0;end<2;++end)
        {
            const int center=end?length-edge/2:edge/2,direction=end?1:-1;
            if(vertical){MoveToEx(dc,rect.left+cross/2-arm,rect.top+center-direction*arm/2,nullptr);LineTo(dc,rect.left+cross/2,rect.top+center+direction*arm/2);LineTo(dc,rect.left+cross/2+arm,rect.top+center-direction*arm/2);}
            else{MoveToEx(dc,rect.left+center-direction*arm/2,rect.top+cross/2-arm,nullptr);LineTo(dc,rect.left+center+direction*arm/2,rect.top+cross/2);LineTo(dc,rect.left+center-direction*arm/2,rect.top+cross/2+arm);}
        }
        SelectObject(dc,old);DeleteObject(pen);
    }
    void paintNonClient(HWND child) const
    {
        RECT outer{},client{};GetWindowRect(child,&outer);GetClientRect(child,&client);MapWindowPoints(child,nullptr,reinterpret_cast<POINT*>(&client),2);
        OffsetRect(&client,-outer.left,-outer.top);const int width=static_cast<int>(outer.right-outer.left),height=static_cast<int>(outer.bottom-outer.top);
        const HDC dc=GetWindowDC(child);if(!dc)return;const auto style=GetWindowLongPtrW(child,GWL_STYLE);
        if((style&WS_VSCROLL) && width-client.right>4){RECT bar{client.right,client.top,width-1,client.bottom};paintScrollbar(child,dc,bar,true,SB_VERT);}
        if((style&WS_HSCROLL) && height-client.bottom>4){RECT bar{client.left,client.bottom,client.right,height-1};paintScrollbar(child,dc,bar,false,SB_HORZ);}
        RECT frame{0,0,width,height};const HBRUSH edge=CreateSolidBrush(border);FrameRect(dc,&frame,edge);DeleteObject(edge);ReleaseDC(child,dc);
    }
    void paintControl(HWND child,HDC dc,const wchar_t* klass) const
    {
        RECT rect{};GetClientRect(child,&rect);SetBkMode(dc,TRANSPARENT);
        const bool enabled=IsWindowEnabled(child)!=FALSE;SetTextColor(dc,enabled?text:muted);
        const auto previous=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0)));
        if(!_wcsicmp(klass,L"COMBOBOX"))
        {
            COMBOBOXINFO info{};info.cbSize=sizeof(info);
            if(GetComboBoxInfo(child,&info))
            {
                FillRect(dc,&info.rcButton,panelBrush);const HBRUSH edge=CreateSolidBrush(border);FrameRect(dc,&rect,edge);DeleteObject(edge);
                const int cx=(info.rcButton.left+info.rcButton.right)/2,cy=(info.rcButton.top+info.rcButton.bottom)/2;
                POINT points[]={{cx-4,cy-2},{cx+4,cy-2},{cx,cy+3}};
                const HBRUSH arrow=CreateSolidBrush(enabled?text:muted);const auto oldBrush=SelectObject(dc,arrow);const auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,points,3);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(arrow);
            }
        }
        else if(!_wcsicmp(klass,L"BUTTON"))
        {
            FillRect(dc,&rect,backBrush);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);const int box=(std::max)(12,(std::min)(static_cast<int>(rect.bottom)-6,static_cast<int>(metrics.tmHeight)+2));
            RECT check{3,(rect.bottom-box)/2,3+box,(rect.bottom+box)/2};FillRect(dc,&check,inputBrush);
            const HBRUSH edge=CreateSolidBrush(enabled?border:muted);FrameRect(dc,&check,edge);DeleteObject(edge);
            if(SendMessageW(child,BM_GETCHECK,0,0)!=BST_UNCHECKED)
            {const HPEN pen=CreatePen(PS_SOLID,(std::max)(1,box/8),enabled?accent:muted);const auto old=SelectObject(dc,pen);MoveToEx(dc,check.left+3,(check.top+check.bottom)/2,nullptr);LineTo(dc,check.left+box/2,check.bottom-4);LineTo(dc,check.right-3,check.top+3);SelectObject(dc,old);DeleteObject(pen);}
            wchar_t label[512]{};GetWindowTextW(child,label,512);rect.left=check.right+7;DrawTextW(dc,label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            if(GetFocus()==child){InflateRect(&rect,-1,-2);DrawFocusRect(dc,&rect);}
        }
        else if(!_wcsicmp(klass,WC_TABCONTROLW))
        {
            FillRect(dc,&rect,backBrush);const int selected=TabCtrl_GetCurSel(child);
            for(int i=0;i<TabCtrl_GetItemCount(child);++i)
            {
                RECT item{};TabCtrl_GetItemRect(child,i,&item);wchar_t label[256]{};TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=label;tab.cchTextMax=256;TabCtrl_GetItem(child,i,&tab);
                FillRect(dc,&item,i==selected?panelBrush:backBrush);const HBRUSH edge=CreateSolidBrush(i==selected?accent:border);FrameRect(dc,&item,edge);DeleteObject(edge);
                InflateRect(&item,-6,-2);SetTextColor(dc,i==selected?text:muted);DrawTextW(dc,label,-1,&item,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);
                if(i==selected && GetFocus()==child)DrawFocusRect(dc,&item);
            }
        }
        else if(!_wcsicmp(klass,L"SCROLLBAR"))
            paintScrollbar(child,dc,rect,(GetWindowLongPtrW(child,GWL_STYLE)&SBS_VERT)!=0,SB_CTL);
        if(previous)SelectObject(dc,previous);
    }
    static LRESULT CALLBACK controlProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)
    {
        const auto& theme=*reinterpret_cast<const Theme*>(data);
        if(message==WM_NCDESTROY)RemoveWindowSubclass(child,controlProc,4);
        if(!theme.highContrast && message==WM_NCPAINT)
        {const auto result=DefSubclassProc(child,message,wp,lp);theme.paintNonClient(child);return result;}
        if(!theme.highContrast && (message==WM_PAINT || message==WM_PRINTCLIENT))
        {
            wchar_t klass[32]{};GetClassNameW(child,klass,32);
            if(!_wcsicmp(klass,L"COMBOBOX"))
            {const auto result=DefSubclassProc(child,message,wp,lp);const HDC dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):GetDC(child);if(dc){theme.paintControl(child,dc,klass);if(message!=WM_PRINTCLIENT)ReleaseDC(child,dc);}return result;}
            if(_wcsicmp(klass,L"BUTTON") && _wcsicmp(klass,WC_TABCONTROLW) && _wcsicmp(klass,L"SCROLLBAR")){const auto result=DefSubclassProc(child,message,wp,lp);theme.paintNonClient(child);return result;}
            PAINTSTRUCT paint{};const HDC dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):BeginPaint(child,&paint);
            if(dc)theme.paintControl(child,dc,klass);if(message!=WM_PRINTCLIENT)EndPaint(child,&paint);return 0;
        }
        if(!theme.highContrast && (message==WM_VSCROLL || message==WM_HSCROLL)){const auto result=DefSubclassProc(child,message,wp,lp);theme.paintNonClient(child);return result;}
        return DefSubclassProc(child,message,wp,lp);
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
            const auto controlStyle=GetWindowLongPtrW(child,GWL_STYLE);const auto checkboxKind=controlStyle&BS_TYPEMASK;
            if(!_wcsicmp(name,L"COMBOBOX"))
            {COMBOBOXINFO combo{};combo.cbSize=sizeof(combo);if(GetComboBoxInfo(child,&combo) && combo.hwndList){SetWindowTheme(combo.hwndList,t.highContrast?nullptr:L"",t.highContrast?nullptr:L"");SetWindowSubclass(combo.hwndList,controlProc,4,reinterpret_cast<DWORD_PTR>(&t));}}
            if((controlStyle&(WS_HSCROLL|WS_VSCROLL)) || !_wcsicmp(name,L"COMBOBOX") || !_wcsicmp(name,WC_TABCONTROLW) || !_wcsicmp(name,L"SCROLLBAR") ||
               (!_wcsicmp(name,L"BUTTON") && (checkboxKind==BS_AUTOCHECKBOX || checkboxKind==BS_CHECKBOX)))
                SetWindowSubclass(child,controlProc,4,reinterpret_cast<DWORD_PTR>(&t));
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
