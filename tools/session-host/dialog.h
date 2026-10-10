/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_DIALOG_H
#define FS_HOST_DIALOG_H
#include <windows.h>
#include "theme.h"
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
namespace fs_host_ui
{
enum class FieldType { Text, Number, Choice, Check, Color };
struct Field
{
    std::wstring label, value;
    FieldType type = FieldType::Text;
    std::vector<std::wstring> choices;
    unsigned int minimum = 0, maximum = 64;
    HWND control = nullptr;
    unsigned int page = 0; HWND caption = nullptr;
};
inline unsigned int number(const Field& field) { return static_cast<unsigned int>(std::wcstoul(field.value.c_str(), nullptr, 10)); }
struct Editor { const wchar_t* title; std::vector<Field>* fields; HFONT font = nullptr; std::vector<std::wstring> pages; int x=0,y=0; };
inline void scrollEditor(HWND dialog,Editor& editor,int x,int y)
{
    const auto clamp=[dialog](int bar,int value){SCROLLINFO s{};s.cbSize=sizeof(s);s.fMask=SIF_RANGE|SIF_PAGE;GetScrollInfo(dialog,bar,&s);return (std::max)(0,(std::min)(value,s.nMax-static_cast<int>(s.nPage)+1));};
    x=clamp(SB_HORZ,x);y=clamp(SB_VERT,y);const POINT delta{editor.x-x,editor.y-y};
    EnumChildWindows(dialog,[](HWND child,LPARAM value)->BOOL
    {
        if(GetParent(child)!=GetAncestor(child,GA_ROOT))return TRUE;
        const auto& shift=*reinterpret_cast<const POINT*>(value);const HWND parent=GetParent(child);RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);
        SetWindowPos(child,nullptr,r.left+shift.x,r.top+shift.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return TRUE;
    },reinterpret_cast<LPARAM>(&delta));
    editor.x=x;editor.y=y;SetScrollPos(dialog,SB_HORZ,x,TRUE);SetScrollPos(dialog,SB_VERT,y,TRUE);InvalidateRect(dialog,nullptr,TRUE);
}
inline LRESULT CALLBACK editorFieldProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
{
    if(message==WM_SETFOCUS)SendMessageW(GetParent(child),WM_APP+2,0,reinterpret_cast<LPARAM>(child));
    if(message==WM_NCDESTROY)RemoveWindowSubclass(child,editorFieldProc,3);
    return DefSubclassProc(child,message,wp,lp);
}
inline INT_PTR CALLBACK editorProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        editor = reinterpret_cast<Editor*>(lparam); SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetWindowTextW(dialog, editor->title);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        using WindowDpi = UINT(WINAPI*)(HWND);
        const auto windowDpi = reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        const int dpi = dialogTheme?dialogTheme->uiDpi(windowDpi?windowDpi(dialog):96):windowDpi?static_cast<int>(windowDpi(dialog)):96;
        editor->font = CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto font = editor->font ? editor->font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const auto scale = [dpi](int n) { return MulDiv(n, dpi, 96); };
        int y = 14;
        if(!editor->pages.empty())
        {
            HWND tabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,scale(12),scale(8),scale(494),scale(28),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(90)),instance,nullptr);
            SendMessageW(tabs,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            for(std::size_t i=0;i<editor->pages.size();++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=editor->pages[i].data();TabCtrl_InsertItem(tabs,static_cast<int>(i),&item);}
            y=48;
        }
        const int firstY=y; std::vector<int> positions((std::max)(std::size_t{1},editor->pages.size()),firstY);
        for (std::size_t i = 0; i < editor->fields->size(); ++i)
        {
            auto& field = (*editor->fields)[i];
            y=positions[(std::min)(static_cast<std::size_t>(field.page),positions.size()-1)];
            HWND label = field.caption = CreateWindowExW(0,L"STATIC",field.label.c_str(),WS_CHILD|WS_VISIBLE,scale(12),scale(y+4),scale(180),scale(25),dialog,nullptr,instance,nullptr);
            const bool check = field.type == FieldType::Check, choice = field.type == FieldType::Choice, color = field.type == FieldType::Color;
            const DWORD style = WS_CHILD|WS_VISIBLE|WS_TABSTOP | (check ? BS_AUTOCHECKBOX : choice ? CBS_DROPDOWNLIST|WS_VSCROLL : color ? BS_PUSHBUTTON : WS_BORDER|ES_AUTOHSCROLL);
            field.control = CreateWindowExW(0, check || color ? L"BUTTON" : choice ? L"COMBOBOX" : L"EDIT", color ? L"Choose color…" : field.value.c_str(),
                style,scale(196),scale(y),scale(310),scale(choice ? 220 : 25),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),instance,nullptr);
            SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); SendMessageW(field.control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            SetWindowSubclass(field.control,editorFieldProc,3,0);
            if (check) SendMessageW(field.control,BM_SETCHECK,number(field) ? BST_CHECKED : BST_UNCHECKED,0);
            if (choice)
            { for (const auto& text : field.choices) SendMessageW(field.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str())); SendMessageW(field.control,CB_SETCURSEL,number(field),0); }
            if (!check && !choice && !color) SendMessageW(field.control,EM_SETLIMITTEXT,field.type == FieldType::Number ? 6 : field.maximum,0);
            if(field.page){ShowWindow(label,SW_HIDE);ShowWindow(field.control,SW_HIDE);}
            positions[(std::min)(static_cast<std::size_t>(field.page),positions.size()-1)]=y+36;
        }
        y=*std::max_element(positions.begin(),positions.end());
        const auto button = [&](const wchar_t* text,int x,int id)
        {
            HWND child = CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|(id == IDOK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                scale(x),scale(y+8),scale(110),scale(28),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            SetWindowSubclass(child,editorFieldProc,3,0);
        };
        button(L"Apply",276,IDOK); button(L"Cancel",396,IDCANCEL);
        RECT size{0,0,scale(520),scale(y+50)}; AdjustWindowRectEx(&size,static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_STYLE)),FALSE,0);
        RECT owner{}; GetWindowRect(GetParent(dialog),&owner);
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(GetParent(dialog),MONITOR_DEFAULTTONEAREST),&monitor))
        {
            const int width = (std::min)(static_cast<int>(size.right-size.left),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left));
            const int height = (std::min)(static_cast<int>(size.bottom-size.top),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
            const int x = (std::max)(static_cast<int>(monitor.rcWork.left),(std::min)(static_cast<int>(owner.left)+30,static_cast<int>(monitor.rcWork.right)-width));
            const int y = (std::max)(static_cast<int>(monitor.rcWork.top),(std::min)(static_cast<int>(owner.top)+30,static_cast<int>(monitor.rcWork.bottom)-height));
            SetWindowPos(dialog,nullptr,x,y,width,height,SWP_NOZORDER);
        }
        else SetWindowPos(dialog,nullptr,owner.left+30,owner.top+30,size.right-size.left,size.bottom-size.top,SWP_NOZORDER);
        RECT client{};GetClientRect(dialog,&client);SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_RANGE|SIF_PAGE;scroll.nMax=scale(520)-1;scroll.nPage=static_cast<UINT>(client.right);SetScrollInfo(dialog,SB_HORZ,&scroll,TRUE);scroll.nMax=scale(y+50)-1;scroll.nPage=static_cast<UINT>(client.bottom);SetScrollInfo(dialog,SB_VERT,&scroll,TRUE);
        if(dialogTheme)dialogTheme->window(dialog);
        return TRUE;
    }
    if (!editor) return FALSE;
    if(message==WM_VSCROLL || message==WM_HSCROLL || message==WM_MOUSEWHEEL)
    {
        const bool horizontal=message==WM_HSCROLL;const int bar=horizontal?SB_HORZ:SB_VERT;
        SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_ALL;GetScrollInfo(dialog,bar,&scroll);int next=horizontal?editor->x:editor->y;
        if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*48;
        else switch(LOWORD(wparam)){case SB_LINEUP:next-=24;break;case SB_LINEDOWN:next+=24;break;case SB_PAGEUP:next-=static_cast<int>(scroll.nPage);break;case SB_PAGEDOWN:next+=static_cast<int>(scroll.nPage);break;case SB_THUMBTRACK:next=scroll.nTrackPos;break;default:break;}
        scrollEditor(dialog,*editor,horizontal?next:editor->x,horizontal?editor->y:next);return TRUE;
    }
    if(message==WM_APP+2)
    {
        RECT r{},client{};const HWND child=reinterpret_cast<HWND>(lparam);GetWindowRect(child,&r);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&r),2);GetClientRect(dialog,&client);
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
        { const int page=TabCtrl_GetCurSel(header->hwndFrom);for(auto& field:*editor->fields){ShowWindow(field.caption,field.page==static_cast<unsigned int>(page)?SW_SHOW:SW_HIDE);ShowWindow(field.control,field.page==static_cast<unsigned int>(page)?SW_SHOW:SW_HIDE);}return TRUE; }
    }
    if (message != WM_COMMAND) return FALSE;
    const unsigned int id = LOWORD(wparam);
    if (id >= 100 && id < 100 + editor->fields->size())
    {
        auto& field = (*editor->fields)[id-100];
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
        if (field.type == FieldType::Color) continue;
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
    layout.dialog.style = WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|WS_VSCROLL|WS_HSCROLL;
    layout.dialog.cx = 300; layout.dialog.cy = 200;
    Editor editor{title,&fields,nullptr,std::move(pages)};
    const bool accepted = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,owner,editorProc,reinterpret_cast<LPARAM>(&editor)) == IDOK;
    if (editor.font) DeleteObject(editor.font); // Dialog and its controls are already destroyed.
    return accepted;
}
}
#endif
