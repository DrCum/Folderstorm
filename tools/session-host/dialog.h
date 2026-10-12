/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_DIALOG_H
#define FS_HOST_DIALOG_H
#include <windows.h>
#include "theme.h"
#include "fssessiondialoglayout.h"
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <functional>
namespace fs_host_ui
{
enum class FieldType { Text, Number, Choice, Check, Color, Action };
struct Field
{
    std::wstring label, value;
    FieldType type = FieldType::Text;
    std::vector<std::wstring> choices;
    unsigned int minimum = 0, maximum = 64;
    HWND control = nullptr;
    unsigned int page = 0; HWND caption = nullptr;
    std::function<void(HWND)> action;
};
inline unsigned int number(const Field& field) { return static_cast<unsigned int>(std::wcstoul(field.value.c_str(), nullptr, 10)); }
struct Editor
{
    const wchar_t* title; std::vector<Field>* fields; HFONT font=nullptr; std::vector<std::wstring> pages;
    int x=0,y=0,dpi=96,page=0; HWND body=nullptr,tabs=nullptr,vertical=nullptr,horizontal=nullptr;
    std::vector<int> heights, rows;
    int scale(int value) const { return MulDiv(value,dpi,96); }
};
inline LRESULT CALLBACK editorBodyProc(HWND body,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_COMMAND || message==WM_NOTIFY || message==WM_DRAWITEM || message==WM_CTLCOLORSTATIC ||
       message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || message==WM_CTLCOLORBTN || message==WM_MOUSEWHEEL ||
       message==WM_VSCROLL || message==WM_HSCROLL || message==WM_ERASEBKGND)
        return SendMessageW(GetParent(body),message,wp,lp);
    return DefWindowProcW(body,message,wp,lp);
}
inline void scrollEditor(HWND dialog,Editor& editor,int x,int y)
{
    SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_RANGE|SIF_PAGE;
    GetScrollInfo(editor.horizontal,SB_CTL,&scroll);x=(std::max)(0,(std::min)(x,scroll.nMax-static_cast<int>(scroll.nPage)+1));
    GetScrollInfo(editor.vertical,SB_CTL,&scroll);y=(std::max)(0,(std::min)(y,scroll.nMax-static_cast<int>(scroll.nPage)+1));
    const int dx=editor.x-x,dy=editor.y-y;
    for(auto& field:*editor.fields)for(HWND child:{field.caption,field.control})
    {
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,editor.body,reinterpret_cast<POINT*>(&r),2);
        SetWindowPos(child,nullptr,r.left+dx,r.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    editor.x=x;editor.y=y;SetScrollPos(editor.horizontal,SB_CTL,x,TRUE);SetScrollPos(editor.vertical,SB_CTL,y,TRUE);
    InvalidateRect(editor.body,nullptr,TRUE);(void)dialog;
}
inline void layoutEditor(HWND dialog,Editor& editor)
{
    RECT client{};GetClientRect(dialog,&client);
    const int gap=editor.scale(12),head=editor.pages.empty()?0:editor.scale(42),footer=editor.scale(48);
    const int width=(std::max)(1,static_cast<int>(client.right)-2*gap),height=(std::max)(1,static_cast<int>(client.bottom)-head-footer);
    const int contentWidth=editor.scale(496),contentHeight=editor.scale(editor.heights[static_cast<std::size_t>(editor.page)]);
    const int bar=editor.scale(16);
    const auto fit=fs_session::editorScrollLayout(contentWidth,contentHeight,width,height,bar);
    SetWindowPos(editor.body,nullptr,gap,head,fit.width,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    if(editor.tabs)SetWindowPos(editor.tabs,nullptr,gap,editor.scale(6),width,editor.scale(30),SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(dialog,IDOK),nullptr,(std::max)(gap,static_cast<int>(client.right)-editor.scale(244)),static_cast<int>(client.bottom)-editor.scale(36),editor.scale(110),editor.scale(28),SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(dialog,IDCANCEL),nullptr,(std::max)(gap,static_cast<int>(client.right)-editor.scale(124)),static_cast<int>(client.bottom)-editor.scale(36),editor.scale(110),editor.scale(28),SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(editor.horizontal,fit.horizontal?SW_SHOW:SW_HIDE);ShowWindow(editor.vertical,fit.vertical?SW_SHOW:SW_HIDE);
    SetWindowPos(editor.horizontal,nullptr,gap,head+fit.height,fit.width,bar,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(editor.vertical,nullptr,gap+fit.width,head,bar,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_RANGE|SIF_PAGE;scroll.nMax=contentWidth-1;scroll.nPage=static_cast<UINT>(fit.width);SetScrollInfo(editor.horizontal,SB_CTL,&scroll,TRUE);
    scroll.nMax=contentHeight-1;scroll.nPage=static_cast<UINT>(fit.height);SetScrollInfo(editor.vertical,SB_CTL,&scroll,TRUE);
    for(std::size_t i=0;i<editor.fields->size();++i)
    {
        auto& field=(*editor.fields)[i];const int top=editor.scale(editor.rows[i])-editor.y;
        SetWindowPos(field.caption,nullptr,-editor.x,top+editor.scale(4),editor.scale(180),editor.scale(25),SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(field.control,nullptr,(field.type==FieldType::Check?0:editor.scale(184))-editor.x,top,editor.scale(field.type==FieldType::Check?494:310),editor.scale(field.type==FieldType::Choice?220:25),SWP_NOZORDER|SWP_NOACTIVATE);
        ShowWindow(field.caption,field.type!=FieldType::Check && field.page==static_cast<unsigned int>(editor.page)?SW_SHOW:SW_HIDE);
        ShowWindow(field.control,field.page==static_cast<unsigned int>(editor.page)?SW_SHOW:SW_HIDE);
    }
    scrollEditor(dialog,editor,editor.x,editor.y);
}
inline LRESULT CALLBACK editorFieldProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
{
    if(message==WM_SETFOCUS)SendMessageW(GetAncestor(child,GA_ROOT),WM_APP+2,0,reinterpret_cast<LPARAM>(child));
    if(message==WM_NCDESTROY)RemoveWindowSubclass(child,editorFieldProc,3);
    return DefSubclassProc(child,message,wp,lp);
}
inline INT_PTR CALLBACK editorProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        editor=reinterpret_cast<Editor*>(lparam);SetWindowLongPtrW(dialog,DWLP_USER,lparam);SetWindowTextW(dialog,editor->title);
        const HINSTANCE instance=GetModuleHandleW(nullptr);
        using WindowDpi=UINT(WINAPI*)(HWND);const auto windowDpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        editor->dpi=dialogTheme?dialogTheme->uiDpi(windowDpi?windowDpi(dialog):96):windowDpi?static_cast<int>(windowDpi(dialog)):96;
        editor->font=CreateFontW(-editor->scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto font=editor->font?editor->font:reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        WNDCLASSW bodyClass{};bodyClass.hInstance=instance;bodyClass.lpfnWndProc=editorBodyProc;bodyClass.lpszClassName=L"FolderstormSettingsFields";bodyClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&bodyClass);
        editor->body=CreateWindowExW(WS_EX_CONTROLPARENT,bodyClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,dialog,nullptr,instance,nullptr);
        editor->vertical=CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|SBS_VERT,0,0,1,1,dialog,nullptr,instance,nullptr);
        editor->horizontal=CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|SBS_HORZ,0,0,1,1,dialog,nullptr,instance,nullptr);
        if(!editor->pages.empty())
        {
            editor->tabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(90)),instance,nullptr);
            SendMessageW(editor->tabs,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            for(std::size_t i=0;i<editor->pages.size();++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=editor->pages[i].data();TabCtrl_InsertItem(editor->tabs,static_cast<int>(i),&item);}
        }
        editor->heights.assign((std::max)(std::size_t{1},editor->pages.size()),14);editor->rows.clear();
        for(std::size_t i=0;i<editor->fields->size();++i)
        {
            auto& field=(*editor->fields)[i];const auto page=(std::min)(static_cast<std::size_t>(field.page),editor->heights.size()-1);
            field.page=static_cast<unsigned int>(page);editor->rows.push_back(editor->heights[page]);editor->heights[page]+=36;
            field.caption=CreateWindowExW(0,L"STATIC",field.label.c_str(),WS_CHILD|WS_VISIBLE,0,0,1,1,editor->body,nullptr,instance,nullptr);
            const bool check=field.type==FieldType::Check,choice=field.type==FieldType::Choice,color=field.type==FieldType::Color,action=field.type==FieldType::Action;
            const DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|(check?BS_AUTOCHECKBOX:choice?CBS_DROPDOWNLIST|WS_VSCROLL:(color||action)?BS_PUSHBUTTON:WS_BORDER|ES_AUTOHSCROLL);
            field.control=CreateWindowExW(0,check||color||action?L"BUTTON":choice?L"COMBOBOX":L"EDIT",check?field.label.c_str():color?L"Choose color…":field.value.c_str(),style,0,0,1,1,editor->body,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),instance,nullptr);
            for(HWND child:{field.caption,field.control})SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            SetWindowSubclass(field.control,editorFieldProc,3,0);
            if(check)SendMessageW(field.control,BM_SETCHECK,number(field)?BST_CHECKED:BST_UNCHECKED,0);
            if(choice){for(const auto& text:field.choices)SendMessageW(field.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));SendMessageW(field.control,CB_SETCURSEL,number(field),0);}
            if(!check&&!choice&&!color&&!action)SendMessageW(field.control,EM_SETLIMITTEXT,field.type==FieldType::Number?6:field.maximum,0);
        }
        for(const int id:{IDOK,IDCANCEL})
        {const HWND child=CreateWindowExW(0,L"BUTTON",id==IDOK?L"Apply":L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(id==IDOK?BS_DEFPUSHBUTTON:BS_PUSHBUTTON),0,0,1,1,dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
        const int contentHeight=*std::max_element(editor->heights.begin(),editor->heights.end());
        RECT size{0,0,editor->scale(520),editor->scale(contentHeight+48+(editor->pages.empty()?0:42))};AdjustWindowRectEx(&size,static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_STYLE)),FALSE,0);
        RECT owner{};GetWindowRect(GetParent(dialog),&owner);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(GetParent(dialog),MONITOR_DEFAULTTONEAREST),&monitor))
        {
            const int width=(std::min)(static_cast<int>(size.right-size.left),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left));
            const int height=(std::min)(static_cast<int>(size.bottom-size.top),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
            const int x=(std::max)(static_cast<int>(monitor.rcWork.left),(std::min)(static_cast<int>(owner.left)+30,static_cast<int>(monitor.rcWork.right)-width));
            const int y=(std::max)(static_cast<int>(monitor.rcWork.top),(std::min)(static_cast<int>(owner.top)+30,static_cast<int>(monitor.rcWork.bottom)-height));
            SetWindowPos(dialog,nullptr,x,y,width,height,SWP_NOZORDER);
        }
        else SetWindowPos(dialog,nullptr,owner.left+30,owner.top+30,size.right-size.left,size.bottom-size.top,SWP_NOZORDER);
        layoutEditor(dialog,*editor);if(dialogTheme)dialogTheme->window(dialog);return TRUE;
    }
    if(!editor || !editor->body) return FALSE;
    if(message==WM_GETMINMAXINFO)
    {auto* size=reinterpret_cast<MINMAXINFO*>(lparam);size->ptMinTrackSize={editor->scale(280),editor->scale(140)};return TRUE;}
    if(message==WM_DPICHANGED)
    {
        editor->dpi=dialogTheme?dialogTheme->uiDpi(HIWORD(wparam)):static_cast<int>(HIWORD(wparam));
        const auto previous=editor->font;
        editor->font=CreateFontW(-editor->scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        EnumChildWindows(dialog,[](HWND child,LPARAM data)->BOOL{setNativeFont(child,reinterpret_cast<HFONT>(data));return TRUE;},reinterpret_cast<LPARAM>(editor->font?editor->font:GetStockObject(DEFAULT_GUI_FONT)));
        if(previous)DeleteObject(previous);editor->x=editor->y=0;
        const auto& next=*reinterpret_cast<RECT*>(lparam);SetWindowPos(dialog,nullptr,next.left,next.top,next.right-next.left,next.bottom-next.top,SWP_NOZORDER|SWP_NOACTIVATE);
        layoutEditor(dialog,*editor);return TRUE;
    }
    if(message==WM_SIZE){layoutEditor(dialog,*editor);return TRUE;}
    if(message==WM_VSCROLL || message==WM_HSCROLL || message==WM_MOUSEWHEEL)
    {
        const bool horizontal=message==WM_HSCROLL;const HWND bar=horizontal?editor->horizontal:editor->vertical;
        SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_ALL;GetScrollInfo(bar,SB_CTL,&scroll);int next=horizontal?editor->x:editor->y;
        if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*editor->scale(48);
        else switch(LOWORD(wparam)){case SB_LINEUP:next-=editor->scale(24);break;case SB_LINEDOWN:next+=editor->scale(24);break;case SB_PAGEUP:next-=static_cast<int>(scroll.nPage);break;case SB_PAGEDOWN:next+=static_cast<int>(scroll.nPage);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=scroll.nTrackPos;break;default:break;}
        scrollEditor(dialog,*editor,horizontal?next:editor->x,horizontal?editor->y:next);return TRUE;
    }
    if(message==WM_APP+2)
    {
        const HWND child=reinterpret_cast<HWND>(lparam);if(GetParent(child)!=editor->body)return TRUE;
        RECT r{},client{};GetWindowRect(child,&r);MapWindowPoints(nullptr,editor->body,reinterpret_cast<POINT*>(&r),2);GetClientRect(editor->body,&client);
        scrollEditor(dialog,*editor,editor->x+(r.left<0?r.left:r.right>client.right?r.right-client.right:0),editor->y+(r.top<0?r.top:r.bottom>client.bottom?r.bottom-client.bottom:0));return TRUE;
    }
    if(dialogTheme)
    {
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(dialog,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,dialogTheme->backBrush);return TRUE;}
        if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || message==WM_CTLCOLORBTN)return dialogTheme->color(message,wparam,lparam);
        if(message==WM_DRAWITEM && dialogTheme->button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))return TRUE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog,IDCANCEL); return TRUE; }
    if(message==WM_NOTIFY && !editor->pages.empty())
    {
        const auto* header=reinterpret_cast<NMHDR*>(lparam);
        if(header->idFrom==90 && header->code==TCN_SELCHANGE)
        { editor->page=TabCtrl_GetCurSel(header->hwndFrom);editor->x=editor->y=0;layoutEditor(dialog,*editor);return TRUE; }
    }
    if (message != WM_COMMAND) return FALSE;
    const unsigned int id = LOWORD(wparam);
    if (id >= 100 && id < 100 + editor->fields->size())
    {
        auto& field = (*editor->fields)[id-100];
        if (field.type == FieldType::Action && HIWORD(wparam) == BN_CLICKED && field.action) field.action(dialog);
        if (field.type == FieldType::Color && HIWORD(wparam) == BN_CLICKED)
        {
            static COLORREF custom[16]{}; const auto rgb = number(field);
            CHOOSECOLORW color{}; color.lStructSize = sizeof(color); color.hwndOwner = dialog; color.lpCustColors = custom;
            color.rgbResult = RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); color.Flags = CC_FULLOPEN|CC_RGBINIT;
            if (ChooseColorW(&color))
            { field.value = std::to_wstring((static_cast<unsigned int>(GetRValue(color.rgbResult))<<16)|(static_cast<unsigned int>(GetGValue(color.rgbResult))<<8)|GetBValue(color.rgbResult)); }
        }
        return TRUE;
    }
    if (id == IDCANCEL) { EndDialog(dialog,IDCANCEL); return TRUE; }
    if (id != IDOK) return FALSE;
    for (auto& field : *editor->fields)
    {
        if (field.type == FieldType::Color || field.type == FieldType::Action) continue;
        if (field.type == FieldType::Check) field.value = SendMessageW(field.control,BM_GETCHECK,0,0) == BST_CHECKED ? L"1" : L"0";
        else if (field.type == FieldType::Choice)
        {
            const auto selected = SendMessageW(field.control,CB_GETCURSEL,0,0);
            if (selected < 0) { SetFocus(field.control); return TRUE; }
            field.value = std::to_wstring(selected);
        }
        else
        {
            std::vector<wchar_t> buffer(static_cast<std::size_t>(GetWindowTextLengthW(field.control))+1);
            GetWindowTextW(field.control,buffer.data(),static_cast<int>(buffer.size())); field.value = buffer.data();
            if (field.type == FieldType::Number)
            {
                wchar_t* end = nullptr; const auto n = std::wcstoul(field.value.c_str(),&end,10);
                if (field.value.empty() || field.value.find_first_not_of(L"0123456789") != std::wstring::npos || !end || *end || n < field.minimum || n > field.maximum)
                { MessageBoxW(dialog,L"Enter a value in the displayed range.",field.label.c_str(),MB_OK|MB_ICONINFORMATION); SetFocus(field.control); return TRUE; }
            }
        }
    }
    EndDialog(dialog,IDOK); return TRUE;
}
inline bool edit(HWND owner,const wchar_t* title,std::vector<Field>& fields,std::vector<std::wstring> pages={})
{
    struct Template { DLGTEMPLATE dialog; WORD menu = 0, klass = 0, title = 0; } layout{};
    layout.dialog.style = WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|WS_THICKFRAME;
    layout.dialog.cx = 300; layout.dialog.cy = 200;
    Editor editor{title,&fields,nullptr,std::move(pages)};
    const bool accepted = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,owner,editorProc,reinterpret_cast<LPARAM>(&editor)) == IDOK;
    if (editor.font) DeleteObject(editor.font); // Dialog and its controls are already destroyed.
    return accepted;
}
}
#endif
