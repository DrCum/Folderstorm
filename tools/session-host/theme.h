/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_THEME_H
#define FS_HOST_THEME_H
#include <windows.h>
#include <windowsx.h>
#include "fssessiondialoglayout.h"
#include "lucide.h"
#include "accessibility.h"
#include <uxtheme.h>
#include <commctrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <optional>
namespace fs_host_ui
{
struct Palette
{
    COLORREF background,panel,input,text,muted,accent,border;
};
inline void setNativeFont(HWND child,HFONT font)
{
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    wchar_t klass[32]{};GetClassNameW(child,klass,32);
    if(_wcsicmp(klass,L"COMBOBOX") || !(GetWindowLongPtrW(child,GWL_STYLE)&CBS_OWNERDRAWFIXED))return;
    const HDC dc=GetDC(child);if(!dc)return;const auto previous=SelectObject(dc,font);TEXTMETRICW metrics{};
    if(GetTextMetricsW(dc,&metrics)){const int height=static_cast<int>(metrics.tmHeight);SendMessageW(child,CB_SETITEMHEIGHT,0,height+6);SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),height+2);}
    if(previous)SelectObject(dc,previous);ReleaseDC(child,dc);
}
inline int comboFaceHeight(HWND child,int target)
{
    RECT rect{};GetWindowRect(child,&rect);const int height=static_cast<int>(rect.bottom-rect.top);
    if(height==target)return height;
    const auto selection=SendMessageW(child,CB_GETITEMHEIGHT,static_cast<WPARAM>(-1),0);if(selection==CB_ERR)return height;
    int minimum=1;const HDC dc=GetDC(child);
    if(dc){const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0)));TEXTMETRICW metrics{};if(GetTextMetricsW(dc,&metrics))minimum=static_cast<int>(metrics.tmHeight)+2;if(old)SelectObject(dc,old);ReleaseDC(child,dc);}
    const int trim=(std::max)(0,height-static_cast<int>(selection));
    SendMessageW(child,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),(std::max)(minimum,target-trim));
    GetWindowRect(child,&rect);return static_cast<int>(rect.bottom-rect.top);
}
class Theme
{
public:
    COLORREF background=0,panel=0,text=0,muted=0,accent=0,border=0,input=0;
    HBRUSH backBrush=nullptr,panelBrush=nullptr,inputBrush=nullptr,selectionBrush=nullptr;
    COLORREF selectionText=0;
    bool highContrast=false,systemHighContrast=false,dark=true;
    AccessibleNames accessible;
    IconCache icons;
    unsigned int textPercent=100;
    int uiDpi(unsigned int dpi) const {return MulDiv(static_cast<int>(dpi),static_cast<int>(textPercent),100);}
    int scale(int value,unsigned int dpi) const {return MulDiv(value,uiDpi(dpi),96);}
    Theme(){set(0);} ~Theme(){release();}
    Theme(const Theme&)=delete;Theme& operator=(const Theme&)=delete;
    static LRESULT CALLBACK hoverProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
    {
        if(message==WM_SIZE || message==WM_SETFONT || message==WM_ENABLE || message==WM_SETTEXT)
        {const auto result=DefSubclassProc(child,message,wp,lp);InvalidateRect(child,nullptr,TRUE);return result;}
        if(message==WM_MOUSEMOVE && !GetPropW(child,L"FolderstormHover"))
        {SetPropW(child,L"FolderstormHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,child,0};TrackMouseEvent(&track);InvalidateRect(child,nullptr,FALSE);}
        if(message==WM_MOUSELEAVE){RemovePropW(child,L"FolderstormHover");InvalidateRect(child,nullptr,FALSE);}
        if(message==WM_NCDESTROY){RemovePropW(child,L"FolderstormHover");RemovePropW(child,L"FolderstormIcon");RemovePropW(child,L"FolderstormIconLabel");RemoveWindowSubclass(child,hoverProc,2);}
        return DefSubclassProc(child,message,wp,lp);
    }
    void selection(const DRAWITEMSTRUCT& d) const
    {
        const bool selected=(d.itemState&ODS_SELECTED)!=0 && !(d.itemState&ODS_COMBOBOXEDIT);
        FillRect(d.hDC,&d.rcItem,selected?selectionBrush:inputBrush);
        SetTextColor(d.hDC,(d.itemState&ODS_DISABLED)?muted:selected?selectionText:text);
    }
    void selectionFocus(const DRAWITEMSTRUCT& d) const
    {
        if(!highContrast){if((d.itemState&(ODS_SELECTED|ODS_FOCUS)) && !(d.itemState&ODS_COMBOBOXEDIT)){const HBRUSH line=CreateSolidBrush(accent);FrameRect(d.hDC,&d.rcItem,line);DeleteObject(line);}}
        else if(d.itemState&ODS_FOCUS){RECT r=d.rcItem;InflateRect(&r,-2,-2);DrawFocusRect(d.hDC,&r);}
    }
    void release(){if(backBrush)DeleteObject(backBrush);if(panelBrush)DeleteObject(panelBrush);if(inputBrush)DeleteObject(inputBrush);if(selectionBrush)DeleteObject(selectionBrush);backBrush=panelBrush=inputBrush=selectionBrush=nullptr;}
    void set(unsigned int choice,bool forceContrast=false,const Palette* custom=nullptr)
    {
        icons.clear();
        DWORD percent=100,bytes=sizeof(percent);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Accessibility",L"TextScaleFactor",RRF_RT_REG_DWORD,nullptr,&percent,&bytes);
        textPercent=(std::max)(100u,(std::min)(225u,static_cast<unsigned int>(percent)));
        HIGHCONTRASTW hc{};hc.cbSize=sizeof(hc);systemHighContrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags&HCF_HIGHCONTRASTON);highContrast=systemHighContrast || forceContrast;
        DWORD light=1,size=sizeof(light);if(choice==2)RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
        dark=choice==0 || (choice==2 && light==0);
        background=dark?RGB(17,16,22):RGB(247,245,251);panel=dark?RGB(26,25,33):RGB(255,255,255);
        text=dark?RGB(245,243,250):RGB(33,26,43);muted=dark?RGB(173,169,186):RGB(97,86,111);
        accent=dark?RGB(191,164,252):RGB(108,63,168);border=dark?RGB(115,103,135):RGB(137,125,157);input=background;
        if(custom && !highContrast)
        {
            background=custom->background;panel=custom->panel;input=custom->input;text=custom->text;muted=custom->muted;accent=custom->accent;border=custom->border;
            dark=(GetRValue(background)*299+GetGValue(background)*587+GetBValue(background)*114)<128000;
        }
        if(systemHighContrast){background=panel=GetSysColor(COLOR_BTNFACE);input=GetSysColor(COLOR_WINDOW);text=GetSysColor(COLOR_WINDOWTEXT);muted=GetSysColor(COLOR_GRAYTEXT);accent=GetSysColor(COLOR_HIGHLIGHT);border=text;}
        else if(highContrast){background=input=RGB(0,0,0);panel=RGB(24,24,24);text=border=RGB(255,255,255);muted=RGB(190,190,190);accent=RGB(255,255,0);dark=true;}
        release();backBrush=CreateSolidBrush(background);panelBrush=CreateSolidBrush(panel);inputBrush=CreateSolidBrush(input);
        selectionBrush=CreateSolidBrush(systemHighContrast?GetSysColor(COLOR_HIGHLIGHT):highContrast?accent:panel);
        selectionText=systemHighContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):highContrast?RGB(0,0,0):text;
    }
    void paintScrollbar(HWND child,HDC dc,const RECT& rect,bool vertical,int kind) const
    {
        FillRect(dc,&rect,backBrush);
        const int length=static_cast<int>(vertical?rect.bottom-rect.top:rect.right-rect.left),cross=static_cast<int>(vertical?rect.right-rect.left:rect.bottom-rect.top);
        const int edge=(std::min)(cross,length/2);
        SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(child,kind,&info);
        const auto geometry=fs_session::scrollThumbLayout(length,cross,info.nMin,info.nMax,info.nPage,info.nPos);
        const int thumb=geometry.length,offset=geometry.start;
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
    static bool scrollbarRect(HWND child,int kind,RECT& rect)
    {
        if(kind==SB_CTL){GetClientRect(child,&rect);MapWindowPoints(child,nullptr,reinterpret_cast<POINT*>(&rect),2);return !IsRectEmpty(&rect);}
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&(kind==SB_VERT?WS_VSCROLL:WS_HSCROLL)))return false;
        SCROLLBARINFO bar{};bar.cbSize=sizeof(bar);
        if(!GetScrollBarInfo(child,kind==SB_VERT?OBJID_VSCROLL:OBJID_HSCROLL,&bar) || IsRectEmpty(&bar.rcScrollBar))return false;
        rect=bar.rcScrollBar;return true;
    }
    void paintNonClient(HWND child) const
    {
        RECT outer{},client{};GetWindowRect(child,&outer);GetClientRect(child,&client);MapWindowPoints(child,nullptr,reinterpret_cast<POINT*>(&client),2);
        OffsetRect(&client,-outer.left,-outer.top);const int width=static_cast<int>(outer.right-outer.left),height=static_cast<int>(outer.bottom-outer.top);
        const HDC dc=GetWindowDC(child);if(!dc)return;const auto style=GetWindowLongPtrW(child,GWL_STYLE);
        const int saved=SaveDC(dc);ExcludeClipRect(dc,client.left,client.top,client.right,client.bottom);
        RECT whole{0,0,width,height};FillRect(dc,&whole,inputBrush);
        for(const int kind:{SB_VERT,SB_HORZ})
        {RECT bar{};if((style&(kind==SB_VERT?WS_VSCROLL:WS_HSCROLL)) && scrollbarRect(child,kind,bar)){OffsetRect(&bar,-outer.left,-outer.top);paintScrollbar(child,dc,bar,kind==SB_VERT,kind);}}
        RECT frame{0,0,width,height};const HBRUSH edge=CreateSolidBrush(border);FrameRect(dc,&frame,edge);DeleteObject(edge);RestoreDC(dc,saved);ReleaseDC(child,dc);
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
                FillRect(dc,&rect,inputBrush);
                DRAWITEMSTRUCT item{};item.CtlType=ODT_COMBOBOX;item.CtlID=static_cast<UINT>(GetDlgCtrlID(child));item.itemID=static_cast<UINT>(SendMessageW(child,CB_GETCURSEL,0,0));
                item.itemAction=ODA_DRAWENTIRE;item.itemState=ODS_COMBOBOXEDIT|(enabled?0:ODS_DISABLED);item.hwndItem=child;item.hDC=dc;item.rcItem=info.rcItem;
                SendMessageW(GetParent(child),WM_DRAWITEM,item.CtlID,reinterpret_cast<LPARAM>(&item));
                FillRect(dc,&info.rcButton,panelBrush);const HBRUSH edge=CreateSolidBrush(GetFocus()==child?accent:border);FrameRect(dc,&rect,edge);DeleteObject(edge);
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
            wchar_t label[512]{};GetWindowTextW(child,label,512);rect.left=check.right+7;DrawTextW(dc,label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            if(GetFocus()==child){InflateRect(&rect,-1,-2);DrawFocusRect(dc,&rect);}
        }
        else if(!_wcsicmp(klass,WC_TABCONTROLW))
        {
            FillRect(dc,&rect,backBrush);const int selected=TabCtrl_GetCurSel(child);
            for(int i=0;i<TabCtrl_GetItemCount(child);++i)
            {
                RECT item{};TabCtrl_GetItemRect(child,i,&item);wchar_t label[256]{};TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=label;tab.cchTextMax=256;TabCtrl_GetItem(child,i,&tab);
                FillRect(dc,&item,i==selected?panelBrush:backBrush);const HBRUSH edge=CreateSolidBrush(i==selected?accent:border);FrameRect(dc,&item,edge);DeleteObject(edge);
                InflateRect(&item,-6,-2);SetTextColor(dc,i==selected?text:muted);DrawTextW(dc,label,-1,&item,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                if(i==selected && GetFocus()==child)DrawFocusRect(dc,&item);
            }
        }
        else if(!_wcsicmp(klass,L"SCROLLBAR"))
            paintScrollbar(child,dc,rect,(GetWindowLongPtrW(child,GWL_STYLE)&SBS_VERT)!=0,SB_CTL);
        if(previous)SelectObject(dc,previous);
    }
    struct ScrollDrag
    {
        int grab=0,repeat=-1,kind=SB_CTL,position=0;
        bool active=false,dragging=false,native=false;
        HWND previousCapture=nullptr;
    };
    static ScrollDrag* scrollDrag(HWND child)
    {
        auto* drag=reinterpret_cast<ScrollDrag*>(GetPropW(child,L"FolderstormCustomScroll"));
        return drag?drag:reinterpret_cast<ScrollDrag*>(GetPropW(child,L"FolderstormNativeScroll"));
    }
    static void finishScroll(HWND child,ScrollDrag& drag,bool restoreCapture)
    {
        const bool wasActive=drag.active;
        drag.active=drag.dragging=false;drag.repeat=-1;KillTimer(child,7);
        if(!wasActive || GetCapture()!=child)return;
        // A dropdown already owns capture while open. Keep that ownership so
        // scrolling does not close it or stop outside-click/Escape handling.
        if(restoreCapture && drag.previousCapture==child)return;
        if(restoreCapture && IsWindow(drag.previousCapture) && IsWindowVisible(drag.previousCapture))SetCapture(drag.previousCapture);
        else ReleaseCapture();
    }
    static void removeScroll(HWND child)
    {
        if(auto* drag=scrollDrag(child))finishScroll(child,*drag,false);
        delete reinterpret_cast<ScrollDrag*>(RemovePropW(child,L"FolderstormCustomScroll"));
        delete reinterpret_cast<ScrollDrag*>(RemovePropW(child,L"FolderstormNativeScroll"));
    }
    static void installScroll(HWND child,bool native,bool highContrast)
    {
        if(highContrast){removeScroll(child);return;}
        if(scrollDrag(child))return;
        auto* drag=new ScrollDrag;drag->native=native;
        if(!SetPropW(child,native?L"FolderstormNativeScroll":L"FolderstormCustomScroll",reinterpret_cast<HANDLE>(drag)))delete drag;
    }
    static LRESULT scrollControl(HWND child,UINT message,WPARAM wp,LPARAM lp,bool& handled)
    {
        auto* drag=scrollDrag(child);
        if(!drag){handled=false;return 0;}
        const auto vertical=[&](){return drag->native?drag->kind==SB_VERT:(GetWindowLongPtrW(child,GWL_STYLE)&SBS_VERT)!=0;};
        const auto notify=[&](int command)
        {
            if(drag->native)
            {
                // Native listboxes obtain SB_THUMBTRACK from the OS's private
                // tracking state. Use absolute SB_THUMBPOSITION while dragging;
                // selection stays native and no fabricated nTrackPos is read.
                const int nativeCommand=command==SB_THUMBTRACK?SB_THUMBPOSITION:command;
                SendMessageW(child,vertical()?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(nativeCommand,drag->position),0);
            }
            else SendMessageW(GetParent(child),vertical()?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(command,0),reinterpret_cast<LPARAM>(child));
            RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);
        };
        const auto geometry=[&]()
        {
            RECT r{};scrollbarRect(child,drag->kind,r);SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(child,drag->kind,&info);
            return fs_session::scrollThumbLayout(static_cast<int>(vertical()?r.bottom-r.top:r.right-r.left),static_cast<int>(vertical()?r.right-r.left:r.bottom-r.top),info.nMin,info.nMax,info.nPage,info.nPos);
        };
        const auto coordinate=[&]()
        {
            RECT r{};scrollbarRect(child,drag->kind,r);POINT point{};GetCursorPos(&point);
            return static_cast<int>(vertical()?point.y-r.top:point.x-r.left);
        };
        const auto commandFor=[](fs_session::ScrollHit hit)
        {
            switch(hit){case fs_session::ScrollHit::LineUp:return SB_LINEUP;case fs_session::ScrollHit::LineDown:return SB_LINEDOWN;case fs_session::ScrollHit::PageUp:return SB_PAGEUP;case fs_session::ScrollHit::PageDown:return SB_PAGEDOWN;default:return -1;}
        };
        handled=true;
        if(message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK || message==WM_NCLBUTTONDOWN || message==WM_NCLBUTTONDBLCLK)
        {
            if(drag->native)
            {
                // Open combos can deliver a client mouse message for their
                // scrollbar because the popup already has mouse capture.
                POINT point{};GetCursorPos(&point);bool found=false;
                for(const int kind:{SB_VERT,SB_HORZ}){RECT r{};if(scrollbarRect(child,kind,r) && PtInRect(&r,point)){drag->kind=kind;found=true;break;}}
                SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_RANGE;
                if(!found || !GetScrollInfo(child,drag->kind,&info) || info.nMin<0 || info.nMax>65535){handled=false;return 0;}
            }
            else if(message==WM_NCLBUTTONDOWN || message==WM_NCLBUTTONDBLCLK){handled=false;return 0;}
            const int point=coordinate();const auto g=geometry();const int extent=2*g.edge+g.length+g.travel;
            const auto hit=fs_session::scrollHit(g,extent,point);if(hit==fs_session::ScrollHit::None)return 0;
            drag->previousCapture=GetCapture();drag->active=true;SetCapture(child);
            drag->dragging=hit==fs_session::ScrollHit::Thumb;
            if(drag->dragging){drag->grab=point-g.start;SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_POS;GetScrollInfo(child,drag->kind,&info);drag->position=info.nPos;return 0;}
            drag->repeat=commandFor(hit);
            notify(drag->repeat);SetTimer(child,7,300,nullptr);return 0;
        }
        if(message==WM_MOUSEMOVE && drag->active && GetCapture()==child)
        {
            if(drag->dragging)
            {
                const auto g=geometry();drag->position=fs_session::scrollPositionAt(g,coordinate(),drag->grab);
                if(!drag->native){SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_POS;info.nPos=drag->position;SetScrollInfo(child,SB_CTL,&info,FALSE);}
                notify(SB_THUMBTRACK);
            }
            return 0;
        }
        if(message==WM_TIMER && wp==7)
        {
            if(drag->active && GetCapture()==child && drag->repeat>=0)
            {const auto g=geometry();if(commandFor(fs_session::scrollHit(g,2*g.edge+g.length+g.travel,coordinate()))==drag->repeat)notify(drag->repeat);SetTimer(child,7,65,nullptr);}return 0;
        }
        if(drag->active && (message==WM_LBUTTONUP || message==WM_CAPTURECHANGED || message==WM_CANCELMODE || message==WM_KILLFOCUS || (message==WM_SHOWWINDOW&&!wp)))
        {
            const bool wasDragging=drag->dragging;finishScroll(child,*drag,message==WM_LBUTTONUP);
            if(wasDragging && message==WM_LBUTTONUP)notify(SB_THUMBPOSITION);notify(SB_ENDSCROLL);handled=message==WM_LBUTTONUP;return 0;
        }
        if(message==WM_MOUSEWHEEL && !drag->native)return SendMessageW(GetParent(child),message,wp,lp);
        handled=false;return 0;
    }
    static LRESULT CALLBACK controlProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)
    {
        const auto& theme=*reinterpret_cast<const Theme*>(data);
        if(message==WM_NCDESTROY)
        {
            RemovePropW(child,L"FolderstormToggleControl");removeScroll(child);RemoveWindowSubclass(child,controlProc,4);return DefSubclassProc(child,message,wp,lp);
        }
        if(GetPropW(child,L"FolderstormToggleControl") && (message==WM_SETFOCUS || message==WM_KILLFOCUS || message==BM_SETSTATE || message==BM_SETCHECK || message==WM_UPDATEUISTATE))
        {const auto result=DefSubclassProc(child,message,wp,lp);InvalidateRect(child,nullptr,TRUE);return result;}
        if(GetPropW(child,L"FolderstormToggleControl") && (message==WM_PAINT || message==WM_PRINTCLIENT))
        {
            PAINTSTRUCT paint{};const HDC dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):BeginPaint(child,&paint);
            if(dc)
            {
                DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.hwndItem=child;item.hDC=dc;GetClientRect(child,&item.rcItem);
                if(GetFocus()==child)item.itemState|=ODS_FOCUS;if(!IsWindowEnabled(child))item.itemState|=ODS_DISABLED;
                if(SendMessageW(child,BM_GETSTATE,0,0)&BST_PUSHED)item.itemState|=ODS_SELECTED;
                theme.button(item);
            }
            if(message!=WM_PRINTCLIENT)EndPaint(child,&paint);return 0;
        }
        if(!theme.systemHighContrast && scrollDrag(child))
        {bool handled=false;const auto result=scrollControl(child,message,wp,lp,handled);if(handled)return result;}
        if(!theme.systemHighContrast && (message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE || message==CB_SETCURSEL || message==CB_SHOWDROPDOWN))
        {wchar_t klass[32]{};GetClassNameW(child,klass,32);if(!_wcsicmp(klass,L"COMBOBOX")){const auto result=DefSubclassProc(child,message,wp,lp);InvalidateRect(child,nullptr,FALSE);return result;}}
        if(!theme.systemHighContrast && message==WM_NCPAINT)
        {theme.paintNonClient(child);return 0;}
        if(!theme.systemHighContrast && (message==WM_PAINT || message==WM_PRINTCLIENT))
        {
            wchar_t klass[32]{};GetClassNameW(child,klass,32);
            if(_wcsicmp(klass,L"COMBOBOX") && _wcsicmp(klass,L"BUTTON") && _wcsicmp(klass,WC_TABCONTROLW) && _wcsicmp(klass,L"SCROLLBAR")){const auto result=DefSubclassProc(child,message,wp,lp);theme.paintNonClient(child);return result;}
            PAINTSTRUCT paint{};const HDC dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):BeginPaint(child,&paint);
            if(dc)theme.paintControl(child,dc,klass);if(message!=WM_PRINTCLIENT)EndPaint(child,&paint);return 0;
        }
        if(!theme.systemHighContrast && (message==WM_VSCROLL || message==WM_HSCROLL)){const auto result=DefSubclassProc(child,message,wp,lp);theme.paintNonClient(child);return result;}
        if(!theme.systemHighContrast && GetPropW(child,L"FolderstormNativeScroll") &&
           (message==WM_WINDOWPOSCHANGED || message==WM_SHOWWINDOW || message==WM_SIZE || message==WM_NCACTIVATE ||
            message==WM_MOUSEMOVE || message==WM_NCMOUSEMOVE || message==WM_MOUSEWHEEL))
        {const auto result=DefSubclassProc(child,message,wp,lp);theme.paintNonClient(child);return result;}
        return DefSubclassProc(child,message,wp,lp);
    }
    void window(HWND w) const
    {
        if(!w)return;
        EnumChildWindows(w,[](HWND child,LPARAM arg)->BOOL
        {
            const auto& t=*reinterpret_cast<const Theme*>(arg);
            wchar_t name[32]{};GetClassNameW(child,name,32);
            if(!t.systemHighContrast)SetWindowTheme(child,L"",L"");else SetWindowTheme(child,nullptr,nullptr);
            if(_wcsicmp(name,L"BUTTON")==0)
            {
                const auto style=GetWindowLongPtrW(child,GWL_STYLE);const auto kind=style&BS_TYPEMASK;
                if(kind==BS_PUSHBUTTON || kind==BS_DEFPUSHBUTTON)SetWindowLongPtrW(child,GWL_STYLE,(style&~BS_TYPEMASK)|BS_OWNERDRAW);
                if(kind==BS_PUSHBUTTON || kind==BS_DEFPUSHBUTTON || kind==BS_OWNERDRAW)SetWindowSubclass(child,hoverProc,2,0);
            }
            if(!_wcsicmp(name,L"SCROLLBAR"))
                installScroll(child,false,t.systemHighContrast);
            if(!_wcsicmp(name,L"LISTBOX"))installScroll(child,true,t.systemHighContrast);
            const auto controlStyle=GetWindowLongPtrW(child,GWL_STYLE);const auto checkboxKind=controlStyle&BS_TYPEMASK;
            if(!_wcsicmp(name,L"COMBOBOX"))
            {COMBOBOXINFO combo{};combo.cbSize=sizeof(combo);if(GetComboBoxInfo(child,&combo) && combo.hwndList){SetWindowTheme(combo.hwndList,t.systemHighContrast?nullptr:L"",t.systemHighContrast?nullptr:L"");installScroll(combo.hwndList,true,t.systemHighContrast);SetWindowSubclass(combo.hwndList,controlProc,4,reinterpret_cast<DWORD_PTR>(&t));}}
            if((controlStyle&(WS_HSCROLL|WS_VSCROLL)) || !_wcsicmp(name,L"LISTBOX") || !_wcsicmp(name,L"COMBOBOX") || !_wcsicmp(name,WC_TABCONTROLW) || !_wcsicmp(name,L"SCROLLBAR") ||
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
    bool drawCombo(const DRAWITEMSTRUCT& d) const
    {
        if(d.CtlType!=ODT_COMBOBOX)return false;
        selection(d);SetBkMode(d.hDC,TRANSPARENT);
        if(d.itemID!=static_cast<UINT>(-1))
        {
            const auto length=SendMessageW(d.hwndItem,CB_GETLBTEXTLEN,d.itemID,0);
            if(length!=CB_ERR && length>=0 && length<=4096)
            {
                std::vector<wchar_t> label(static_cast<std::size_t>(length)+1);
                SendMessageW(d.hwndItem,CB_GETLBTEXT,d.itemID,reinterpret_cast<LPARAM>(label.data()));
                const auto previous=SelectObject(d.hDC,reinterpret_cast<HFONT>(SendMessageW(d.hwndItem,WM_GETFONT,0,0)));
                RECT textRect=d.rcItem;InflateRect(&textRect,-5,-1);DrawTextW(d.hDC,label.data(),-1,&textRect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                if(previous)SelectObject(d.hDC,previous);
            }
        }
        selectionFocus(d);return true;
    }
    void checkButton(HWND child,std::optional<bool> checked) const
    {
        if(!child)return;
        const bool prior=GetPropW(child,L"FolderstormToggleControl")!=nullptr;
        if(prior!=checked.has_value())
        {
            auto style=GetWindowLongPtrW(child,GWL_STYLE);
            style=(style&~(BS_TYPEMASK|BS_PUSHLIKE))|(checked?(BS_CHECKBOX|BS_PUSHLIKE):BS_OWNERDRAW);
            SetWindowLongPtrW(child,GWL_STYLE,style);
            if(checked)SetPropW(child,L"FolderstormToggleControl",reinterpret_cast<HANDLE>(1));else RemovePropW(child,L"FolderstormToggleControl");
            if(!checked)RemoveWindowSubclass(child,controlProc,4);
            RemovePropW(child,L"FolderstormToggle");InvalidateRect(child,nullptr,TRUE);
        }
        if(checked)
        {
            SetWindowSubclass(child,controlProc,4,reinterpret_cast<DWORD_PTR>(this));SetWindowSubclass(child,hoverProc,2,0);
            const auto value=*checked?BST_CHECKED:BST_UNCHECKED;
            if(SendMessageW(child,BM_GETCHECK,0,0)!=value){SendMessageW(child,BM_SETCHECK,value,0);InvalidateRect(child,nullptr,TRUE);}
        }
    }
    bool button(const DRAWITEMSTRUCT& d) const
    {
        if(d.CtlType!=ODT_BUTTON)return false;
        const bool selected=(d.itemState&ODS_SELECTED)!=0 || (GetPropW(d.hwndItem,L"FolderstormToggleControl")?SendMessageW(d.hwndItem,BM_GETCHECK,0,0)==BST_CHECKED:GetPropW(d.hwndItem,L"FolderstormToggle")!=nullptr),disabled=(d.itemState&ODS_DISABLED)!=0;
        const bool hovered=GetPropW(d.hwndItem,L"FolderstormHover")!=nullptr;
        const int savedDC=SaveDC(d.hDC);
        FillRect(d.hDC,&d.rcItem,selected?selectionBrush:hovered?panelBrush:backBrush);
        const HBRUSH line=CreateSolidBrush((d.itemState&ODS_FOCUS) || hovered?accent:border);FrameRect(d.hDC,&d.rcItem,line);DeleteObject(line);
        wchar_t label[256]{};GetWindowTextW(d.hwndItem,label,256);RECT r=d.rcItem;InflateRect(&r,-5,-2);
        SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,disabled?muted:selected?selectionText:text);
        const auto old=SelectObject(d.hDC,reinterpret_cast<HFONT>(SendMessageW(d.hwndItem,WM_GETFONT,0,0)));
        const auto icon=buttonIcon(d.hwndItem);const bool withLabel=GetPropW(d.hwndItem,L"FolderstormIconLabel")!=nullptr;
        using WindowDpi=UINT(WINAPI*)(HWND);static const auto windowDpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        const auto dpi=windowDpi?windowDpi(d.hwndItem):static_cast<UINT>(GetDeviceCaps(d.hDC,LOGPIXELSX));
        const int size=(std::max)(1,(std::min)(static_cast<int>(d.rcItem.bottom-d.rcItem.top)-4,scale(20,dpi)));
        SIZE extent{};if(withLabel)GetTextExtentPoint32W(d.hDC,label,lstrlenW(label),&extent);
        const int occupied=size+(withLabel?static_cast<int>(extent.cx)+6:0),center=(static_cast<int>(d.rcItem.left+d.rcItem.right)-occupied)/2;
        const int x=withLabel?(std::max)(static_cast<int>(r.left),center):center;
        const bool painted=icon!=Icon::None && icons.draw(d.hDC,icon,x,(static_cast<int>(d.rcItem.top+d.rcItem.bottom)-size)/2,size,GetTextColor(d.hDC));
        if(painted && withLabel)r.left=x+size+6;
        if(!painted || withLabel)DrawTextW(d.hDC,label,-1,&r,DT_SINGLELINE|(painted?DT_LEFT:DT_CENTER)|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);if(old)SelectObject(d.hDC,old);
        if(GetPropW(d.hwndItem,L"FolderstormToggleControl") && SendMessageW(d.hwndItem,BM_GETCHECK,0,0)==BST_CHECKED)
        {
            const HPEN pen=CreatePen(PS_SOLID,1,GetTextColor(d.hDC));const auto previous=SelectObject(d.hDC,pen);
            const int right=static_cast<int>(d.rcItem.right)-2;MoveToEx(d.hDC,right-7,d.rcItem.top+4,nullptr);LineTo(d.hDC,right-5,d.rcItem.top+6);LineTo(d.hDC,right-1,d.rcItem.top+2);SelectObject(d.hDC,previous);DeleteObject(pen);
        }
        if(d.itemState&ODS_FOCUS){InflateRect(&r,-2,-2);DrawFocusRect(d.hDC,&r);}if(savedDC)RestoreDC(d.hDC,savedDC);return true;
    }
};
inline Theme* dialogTheme=nullptr;
}
#endif
