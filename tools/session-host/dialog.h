/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_DIALOG_H
#define FS_HOST_DIALOG_H
#include <windows.h>
#include "theme.h"
#include "fssessiondialoglayout.h"
#include "fssessionexperience.h"
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <functional>
#include <map>
namespace fs_host_ui
{
enum class FieldType { Text, Number, Choice, Check, Color, Action, Note };
struct Field
{
    std::wstring label, value;
    FieldType type = FieldType::Text;
    std::vector<std::wstring> choices;
    unsigned int minimum = 0, maximum = 64;
    HWND control = nullptr;
    unsigned int page = 0; HWND caption = nullptr;
    std::function<void(HWND)> action;
    std::function<void(HWND)> changed;
    bool enabled=true;
    std::wstring help;
    bool fixed=false, transient=false;
    int rowHeight=36;
};
struct EditorPage
{
    unsigned int category;
    std::wstring categoryName, title, help;
};
inline unsigned int number(const Field& field) { return static_cast<unsigned int>(std::wcstoul(field.value.c_str(), nullptr, 10)); }
constexpr int EditorApply=92,EditorTabs=91;
struct Editor
{
    const wchar_t* title; std::vector<Field>* fields; HFONT font=nullptr; std::vector<std::wstring> pages;
    int x=0,y=0,dpi=96,page=0; HWND body=nullptr,tabs=nullptr,vertical=nullptr,horizontal=nullptr;
    std::vector<int> heights, rows;
    bool sidebar=false;
    std::function<bool(HWND)> validate;
    std::vector<EditorPage> navigation;
    std::vector<std::wstring> categories;
    std::vector<unsigned int> categoryIds,tabPages;
    std::map<unsigned int,unsigned int> lastTabs;
    HWND subTabs=nullptr,introduction=nullptr,tooltips=nullptr,savedLabel=nullptr;
    std::function<bool(HWND)> apply;
    unsigned int* lastPage=nullptr;HWND positionNear=nullptr;
    bool dirty=false,updating=false,applying=false;
    Editor(const wchar_t* caption,std::vector<Field>& values,std::vector<std::wstring> labels)
        :title(caption),fields(&values),pages(std::move(labels)){}
    int scale(int value) const { return MulDiv(value,dpi,96); }
    bool structured() const {return !navigation.empty();}
    unsigned int category() const {return structured()?navigation[static_cast<std::size_t>(page)].category:static_cast<unsigned int>(page);}
    bool visible(const Field& field) const
    {return field.fixed && structured()?navigation[field.page].category==category():field.page==static_cast<unsigned int>(page);}
};
inline Editor* currentEditor(HWND dialog)
{return reinterpret_cast<Editor*>(GetWindowLongPtrW(dialog,DWLP_USER));}
struct EditorUpdate
{
    Editor* editor;bool previous=false;
    explicit EditorUpdate(HWND dialog):editor(dialog?currentEditor(dialog):nullptr)
    {if(editor){previous=editor->updating;editor->updating=true;}}
    ~EditorUpdate(){if(editor)editor->updating=previous;}
};
inline void dirtyEditor(HWND dialog)
{if(auto* e=currentEditor(dialog)){if(!e->updating){e->dirty=true;SetWindowTextW(e->savedLabel,L"");}}}
inline LRESULT CALLBACK editorBodyProc(HWND body,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_ERASEBKGND)
    {
        RECT r{};GetClientRect(body,&r);
        FillRect(reinterpret_cast<HDC>(wp),&r,dialogTheme?dialogTheme->backBrush:GetSysColorBrush(COLOR_BTNFACE));return 1;
    }
    if(message==WM_COMMAND || message==WM_NOTIFY || message==WM_DRAWITEM || message==WM_MEASUREITEM || message==WM_CTLCOLORSTATIC ||
       message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || message==WM_CTLCOLORBTN || message==WM_MOUSEWHEEL ||
       message==WM_VSCROLL || message==WM_HSCROLL)
        return SendMessageW(GetParent(body),message,wp,lp);
    return DefWindowProcW(body,message,wp,lp);
}
inline void positionEditorFields(Editor& editor)
{
    RECT client{};GetClientRect(editor.body,&client);
    const int rowWidth=(std::max)(editor.scale(380),static_cast<int>(client.right));
    // Move from the model coordinates; no copied child pixels or accumulated offsets.
    SendMessageW(editor.body,WM_SETREDRAW,FALSE,0);
    for(std::size_t i=0;i<editor.fields->size();++i)
    {
        auto& f=(*editor.fields)[i];if(f.fixed)continue;
        const int top=editor.scale(editor.rows[i])-editor.y;
        const bool full=f.type==FieldType::Check || f.type==FieldType::Note;
        const int height=editor.scale(f.type==FieldType::Note?f.rowHeight-8:25);
        SetWindowPos(f.caption,nullptr,-editor.x,top+editor.scale(4),editor.scale(180),editor.scale(25),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);
        SetWindowPos(f.control,nullptr,(full?0:editor.scale(184))-editor.x,top,full?rowWidth-editor.scale(2):rowWidth-editor.scale(184),f.type==FieldType::Choice?editor.scale(220):height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);
        ShowWindow(f.caption,!full && editor.visible(f)?SW_SHOW:SW_HIDE);
        ShowWindow(f.control,editor.visible(f)?SW_SHOW:SW_HIDE);
    }
    SendMessageW(editor.body,WM_SETREDRAW,TRUE,0);
    RedrawWindow(editor.body,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
inline void scrollEditor(HWND,Editor& editor,int x,int y)
{
    SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_RANGE|SIF_PAGE;
    GetScrollInfo(editor.horizontal,SB_CTL,&scroll);x=(std::max)(0,(std::min)(x,scroll.nMax-static_cast<int>(scroll.nPage)+1));
    GetScrollInfo(editor.vertical,SB_CTL,&scroll);y=(std::max)(0,(std::min)(y,scroll.nMax-static_cast<int>(scroll.nPage)+1));
    editor.x=x;editor.y=y;
    SetScrollPos(editor.horizontal,SB_CTL,x,FALSE);SetScrollPos(editor.vertical,SB_CTL,y,FALSE);
    positionEditorFields(editor);
    RedrawWindow(editor.horizontal,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    RedrawWindow(editor.vertical,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
}
inline void layoutEditor(HWND dialog,Editor& editor)
{
    RECT client{};GetClientRect(dialog,&client);
    const int gap=editor.scale(12),footer=editor.scale(48),nav=editor.sidebar?editor.scale(172):0;
    int head=editor.structured() || (!editor.pages.empty()&&!editor.sidebar)?editor.scale(42):0;
    const int width=(std::max)(1,static_cast<int>(client.right)-2*gap-nav);
    if(editor.subTabs)SetWindowPos(editor.subTabs,nullptr,gap+nav,editor.scale(6),width,editor.scale(30),SWP_NOZORDER|SWP_NOACTIVATE);
    const auto help=editor.structured()?editor.navigation[static_cast<std::size_t>(editor.page)].help:std::wstring{};
    if(editor.introduction)
    {
        SetWindowTextW(editor.introduction,help.c_str());ShowWindow(editor.introduction,help.empty()?SW_HIDE:SW_SHOW);
        SetWindowPos(editor.introduction,nullptr,gap+nav,head+editor.scale(3),width,editor.scale(36),SWP_NOZORDER|SWP_NOACTIVATE);
        if(!help.empty())head+=editor.scale(42);
    }
    for(auto& f:*editor.fields)if(f.fixed)
    {
        const bool show=editor.visible(f);
        ShowWindow(f.caption,show?SW_SHOW:SW_HIDE);ShowWindow(f.control,show?SW_SHOW:SW_HIDE);
        if(show)
        {
            SetWindowPos(f.caption,nullptr,gap+nav,head+editor.scale(4),editor.scale(100),editor.scale(25),SWP_NOZORDER|SWP_NOACTIVATE);
            SetWindowPos(f.control,nullptr,gap+nav+editor.scale(104),head,width-editor.scale(104),editor.scale(220),SWP_NOZORDER|SWP_NOACTIVATE);
            head+=editor.scale(36);
        }
    }
    const int height=(std::max)(1,static_cast<int>(client.bottom)-head-footer);
    const int contentWidth=editor.scale(380),contentHeight=editor.scale(editor.heights[static_cast<std::size_t>(editor.page)]),bar=editor.scale(16);
    const auto fit=fs_session::editorScrollLayout(contentWidth,contentHeight,width,height,bar);
    SetWindowPos(editor.body,nullptr,gap+nav,head,fit.width,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    if(editor.tabs)SetWindowPos(editor.tabs,nullptr,gap,editor.scale(6),editor.sidebar?editor.scale(160):width,editor.sidebar?static_cast<int>(client.bottom)-footer-editor.scale(12):editor.scale(30),SWP_NOZORDER|SWP_NOACTIVATE);
    const int buttons=editor.apply?3:2;
    for(int i=0;i<buttons;++i)
    {
        const int id=i==0?IDOK:i==1?IDCANCEL:EditorApply;
        SetWindowPos(GetDlgItem(dialog,id),nullptr,(std::max)(gap,static_cast<int>(client.right)-editor.scale(buttons*120+4)+editor.scale(i*120)),static_cast<int>(client.bottom)-editor.scale(36),editor.scale(110),editor.scale(28),SWP_NOZORDER|SWP_NOACTIVATE);
    }
    if(editor.savedLabel)SetWindowPos(editor.savedLabel,nullptr,gap,static_cast<int>(client.bottom)-editor.scale(31),(std::max)(1,static_cast<int>(client.right)-editor.scale(buttons*120+28)),editor.scale(22),SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(editor.horizontal,fit.horizontal?SW_SHOW:SW_HIDE);ShowWindow(editor.vertical,fit.vertical?SW_SHOW:SW_HIDE);
    SetWindowPos(editor.horizontal,nullptr,gap+nav,head+fit.height,fit.width,bar,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(editor.vertical,nullptr,gap+nav+fit.width,head,bar,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_RANGE|SIF_PAGE;scroll.nMax=contentWidth-1;scroll.nPage=static_cast<UINT>(fit.width);SetScrollInfo(editor.horizontal,SB_CTL,&scroll,FALSE);
    scroll.nMax=contentHeight-1;scroll.nPage=static_cast<UINT>(fit.height);SetScrollInfo(editor.vertical,SB_CTL,&scroll,FALSE);
    scrollEditor(dialog,editor,editor.x,editor.y);
}
inline void selectEditorPage(HWND dialog,Editor& e,unsigned int page)
{
    e.page=static_cast<int>((std::min)(page,static_cast<unsigned int>(e.heights.size()-1)));e.x=e.y=0;
    if(e.structured())
    {
        const auto group=e.category();e.lastTabs[group]=static_cast<unsigned int>(e.page);const auto found=std::find(e.categoryIds.begin(),e.categoryIds.end(),group);
        SendMessageW(e.tabs,LB_SETCURSEL,static_cast<WPARAM>(found-e.categoryIds.begin()),0);
        TabCtrl_DeleteAllItems(e.subTabs);e.tabPages.clear();
        for(unsigned int i=0;i<e.navigation.size();++i)if(e.navigation[i].category==group)
        {
            TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=e.navigation[i].title.data();
            TabCtrl_InsertItem(e.subTabs,static_cast<int>(e.tabPages.size()),&item);e.tabPages.push_back(i);
        }
        const auto selected=std::find(e.tabPages.begin(),e.tabPages.end(),static_cast<unsigned int>(e.page));
        TabCtrl_SetCurSel(e.subTabs,static_cast<int>(selected-e.tabPages.begin()));
    }
    else if(e.tabs){if(e.sidebar)SendMessageW(e.tabs,LB_SETCURSEL,e.page,0);else TabCtrl_SetCurSel(e.tabs,e.page);}
    if(e.lastPage)*e.lastPage=static_cast<unsigned int>(e.page);
    layoutEditor(dialog,e);
}
inline void revealEditorField(HWND dialog,Field& field)
{
    if(auto* e=currentEditor(dialog)){if(!e->visible(field))selectEditorPage(dialog,*e,field.page);SetFocus(field.control);SendMessageW(dialog,WM_APP+2,0,reinterpret_cast<LPARAM>(field.control));}
}
inline void editorTooltip(Editor& e,HWND dialog,Field& f)
{
    if(f.type==FieldType::Number)
    {if(!f.help.empty())f.help+=L" ";f.help+=std::to_wstring(f.minimum)+L"–"+std::to_wstring(f.maximum)+L".";}
    if(f.help.empty() || !e.tooltips)return;
    for(HWND child:{f.caption,f.control})
    {
        TOOLINFOW tip{};tip.cbSize=sizeof(tip);tip.hwnd=dialog;tip.uId=reinterpret_cast<UINT_PTR>(child);tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tip.lpszText=f.help.data();SendMessageW(e.tooltips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));
    }
}
inline bool applyEditor(HWND dialog,Editor& editor,bool close)
{
    if(editor.applying)return false;
    for(auto& f:*editor.fields)
    {
        if(f.type==FieldType::Color || f.type==FieldType::Action || f.type==FieldType::Note)continue;
        if(f.type==FieldType::Check)f.value=SendMessageW(f.control,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0";
        else if(f.type==FieldType::Choice)
        {const auto selected=SendMessageW(f.control,CB_GETCURSEL,0,0);if(selected<0){revealEditorField(dialog,f);return false;}f.value=std::to_wstring(selected);}
        else
        {
            std::vector<wchar_t> text(static_cast<std::size_t>(GetWindowTextLengthW(f.control))+1);GetWindowTextW(f.control,text.data(),static_cast<int>(text.size()));f.value=text.data();
            if(f.type==FieldType::Number && f.enabled)
            {
                wchar_t* end=nullptr;const auto n=std::wcstoul(f.value.c_str(),&end,10);
                if(f.value.empty() || f.value.find_first_not_of(L"0123456789")!=std::wstring::npos || !end || *end || n<f.minimum || n>f.maximum)
                {const auto message=L"Enter a number from "+std::to_wstring(f.minimum)+L" to "+std::to_wstring(f.maximum)+L".";MessageBoxW(dialog,message.c_str(),f.label.c_str(),MB_OK|MB_ICONINFORMATION);revealEditorField(dialog,f);return false;}
            }
        }
    }
    editor.applying=true;
    const bool valid=!editor.validate || editor.validate(dialog);
    const bool accepted=valid && (!editor.apply || editor.apply(dialog));
    editor.applying=false;
    if(!accepted){SetWindowTextW(editor.savedLabel,L"Not saved");return false;}
    editor.dirty=false;SetWindowTextW(editor.savedLabel,L"Saved");
    if(dialogTheme)dialogTheme->window(dialog);
    if(close)EndDialog(dialog,IDOK);
    return true;
}
// Explicit lifecycle/copy actions capture their target before leaving Preferences.
inline bool finishEditorAction(HWND dialog)
{
    auto* e=currentEditor(dialog);if(!e || e->applying)return false;
    if(e->dirty)
    {
        const int choice=MessageBoxW(dialog,L"Save Preferences changes before continuing?",L"Folderstorm",MB_YESNOCANCEL|MB_ICONQUESTION);
        if(choice==IDCANCEL)return false;
        if(choice==IDYES)return applyEditor(dialog,*e,true);
    }
    EndDialog(dialog,IDCANCEL);return true;
}
inline LRESULT CALLBACK editorFieldProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
{
    if(message==WM_SETFOCUS)SendMessageW(GetAncestor(child,GA_ROOT),WM_APP+2,0,reinterpret_cast<LPARAM>(child));
    if(message==WM_NCDESTROY)RemoveWindowSubclass(child,editorFieldProc,3);
    return DefSubclassProc(child,message,wp,lp);
}
inline INT_PTR CALLBACK editorProc(HWND dialog,UINT message,WPARAM wparam,LPARAM lparam)
{
    auto* e=currentEditor(dialog);
    if(message==WM_INITDIALOG)
    {
        e=reinterpret_cast<Editor*>(lparam);SetWindowLongPtrW(dialog,DWLP_USER,lparam);SetWindowTextW(dialog,e->title);e->updating=true;
        const HINSTANCE instance=GetModuleHandleW(nullptr);
        using WindowDpi=UINT(WINAPI*)(HWND);const auto windowDpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        e->dpi=dialogTheme?dialogTheme->uiDpi(windowDpi?windowDpi(dialog):96):windowDpi?static_cast<int>(windowDpi(dialog)):96;
        e->font=CreateFontW(-e->scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto font=e->font?e->font:reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        WNDCLASSW bodyClass{};bodyClass.hInstance=instance;bodyClass.lpfnWndProc=editorBodyProc;bodyClass.lpszClassName=L"FolderstormSettingsFields";bodyClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&bodyClass);
        e->body=CreateWindowExW(WS_EX_CONTROLPARENT,bodyClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,dialog,nullptr,instance,nullptr);
        e->vertical=CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|SBS_VERT,0,0,1,1,dialog,nullptr,instance,nullptr);
        e->horizontal=CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|SBS_HORZ,0,0,1,1,dialog,nullptr,instance,nullptr);
        e->tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,dialog,nullptr,instance,nullptr);SendMessageW(e->tooltips,TTM_SETMAXTIPWIDTH,0,e->scale(380));
        if(e->structured())
        {
            e->sidebar=true;e->pages.clear();
            for(const auto& page:e->navigation)
            {
                e->pages.push_back(page.title);
                if(std::find(e->categoryIds.begin(),e->categoryIds.end(),page.category)==e->categoryIds.end()){e->categoryIds.push_back(page.category);e->categories.push_back(page.categoryName);}
            }
            e->subTabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(EditorTabs)),instance,nullptr);
            e->introduction=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_LEFT|SS_NOPREFIX,0,0,1,1,dialog,nullptr,instance,nullptr);
            setNativeFont(e->subTabs,font);setNativeFont(e->introduction,font);
        }
        if(!e->pages.empty())
        {
            e->tabs=CreateWindowExW(0,e->sidebar?L"LISTBOX":WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(e->sidebar?LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS:0),0,0,1,1,dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(90)),instance,nullptr);
            setNativeFont(e->tabs,font);const auto& labels=e->structured()?e->categories:e->pages;
            for(std::size_t i=0;i<labels.size();++i){if(e->sidebar)SendMessageW(e->tabs,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(labels[i].c_str()));else{TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(labels[i].c_str());TabCtrl_InsertItem(e->tabs,static_cast<int>(i),&item);}}
        }
        e->heights.assign((std::max)(std::size_t{1},e->pages.size()),14);e->rows.clear();
        for(std::size_t i=0;i<e->fields->size();++i)
        {
            auto& f=(*e->fields)[i];const auto page=(std::min)(static_cast<std::size_t>(f.page),e->heights.size()-1);f.page=static_cast<unsigned int>(page);
            e->rows.push_back(e->heights[page]);if(!f.fixed)e->heights[page]+=f.rowHeight;
            const HWND parent=f.fixed?dialog:e->body;
            f.caption=CreateWindowExW(0,L"STATIC",f.label.c_str(),WS_CHILD|SS_LEFTNOWORDWRAP|SS_ENDELLIPSIS|SS_NOPREFIX,0,0,1,1,parent,nullptr,instance,nullptr);
            const bool check=f.type==FieldType::Check,choice=f.type==FieldType::Choice,color=f.type==FieldType::Color,action=f.type==FieldType::Action,note=f.type==FieldType::Note;
            const DWORD style=WS_CHILD|(note?SS_LEFT|SS_NOPREFIX:WS_TABSTOP)|(check?BS_AUTOCHECKBOX:choice?CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL:color?BS_OWNERDRAW:action?BS_PUSHBUTTON:note?0:WS_BORDER|ES_AUTOHSCROLL);
            f.control=CreateWindowExW(0,note?L"STATIC":check||color||action?L"BUTTON":choice?L"COMBOBOX":L"EDIT",check?f.label.c_str():color?L"Color":f.value.c_str(),style,0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),instance,nullptr);
            for(HWND child:{f.caption,f.control})setNativeFont(child,font);
            if(!note)SetWindowSubclass(f.control,editorFieldProc,3,0);EnableWindow(f.control,f.enabled);EnableWindow(f.caption,f.enabled);
            if(check)SendMessageW(f.control,BM_SETCHECK,number(f)?BST_CHECKED:BST_UNCHECKED,0);
            if(choice){for(const auto& label:f.choices)SendMessageW(f.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(f.control,CB_SETCURSEL,number(f),0);}
            if(!check&&!choice&&!color&&!action&&!note)SendMessageW(f.control,EM_SETLIMITTEXT,f.type==FieldType::Number?6:f.maximum,0);
            editorTooltip(*e,dialog,f);
        }
        for(const int id:{IDOK,IDCANCEL,EditorApply})
        {
            if(id==EditorApply&&!e->apply)continue;
            const HWND child=CreateWindowExW(0,L"BUTTON",id==IDOK?(e->apply?L"OK":L"Apply"):id==IDCANCEL?L"Cancel":L"Apply",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(id==IDOK?BS_DEFPUSHBUTTON:BS_PUSHBUTTON),0,0,1,1,dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);setNativeFont(child,font);
        }
        e->savedLabel=CreateWindowExW(0,L"STATIC",e->apply?L"Applied changes save automatically":L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS|SS_NOPREFIX,0,0,1,1,dialog,nullptr,instance,nullptr);setNativeFont(e->savedLabel,font);
        const int contentHeight=*std::max_element(e->heights.begin(),e->heights.end());
        RECT size{0,0,e->scale(e->sidebar?740:520),e->scale(e->sidebar?(std::min)(620,(std::max)(360,contentHeight+132)):(contentHeight+48+(e->pages.empty()?0:42)))};AdjustWindowRectEx(&size,static_cast<DWORD>(GetWindowLongPtrW(dialog,GWL_STYLE)),FALSE,0);
        const HWND anchor=e->positionNear&&IsWindow(e->positionNear)?e->positionNear:GetParent(dialog);RECT owner{};GetWindowRect(anchor,&owner);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(anchor,MONITOR_DEFAULTTONEAREST),&monitor))
        {
            const int width=(std::min)(static_cast<int>(size.right-size.left),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)),height=(std::min)(static_cast<int>(size.bottom-size.top),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
            const int x=(std::max)(static_cast<int>(monitor.rcWork.left),(std::min)(static_cast<int>(owner.left)+30,static_cast<int>(monitor.rcWork.right)-width)),y=(std::max)(static_cast<int>(monitor.rcWork.top),(std::min)(static_cast<int>(owner.top)+30,static_cast<int>(monitor.rcWork.bottom)-height));SetWindowPos(dialog,nullptr,x,y,width,height,SWP_NOZORDER);
        }
        else SetWindowPos(dialog,nullptr,owner.left+30,owner.top+30,size.right-size.left,size.bottom-size.top,SWP_NOZORDER);
        selectEditorPage(dialog,*e,static_cast<unsigned int>(e->page));SendMessageW(dialog,DM_SETDEFID,IDOK,0);if(dialogTheme)dialogTheme->window(dialog);e->updating=false;return TRUE;
    }
    if(!e || !e->body)return FALSE;
    if(message==WM_GETMINMAXINFO){reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize={e->scale(e->sidebar?620:280),e->scale(e->sidebar?300:140)};return TRUE;}
    if(message==WM_DPICHANGED)
    {
        e->dpi=dialogTheme?dialogTheme->uiDpi(HIWORD(wparam)):static_cast<int>(HIWORD(wparam));const auto previous=e->font;
        e->font=CreateFontW(-e->scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const HFONT nextFont=e->font?e->font:reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));EnumChildWindows(dialog,[](HWND child,LPARAM data)->BOOL{setNativeFont(child,reinterpret_cast<HFONT>(data));return TRUE;},reinterpret_cast<LPARAM>(nextFont));if(previous)DeleteObject(previous);
        const auto& next=*reinterpret_cast<RECT*>(lparam);SetWindowPos(dialog,nullptr,next.left,next.top,next.right-next.left,next.bottom-next.top,SWP_NOZORDER|SWP_NOACTIVATE);layoutEditor(dialog,*e);return TRUE;
    }
    if(message==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight=static_cast<UINT>(e->scale(22));return TRUE;}
    if(message==WM_SIZE){layoutEditor(dialog,*e);return TRUE;}
    if(message==WM_SETTINGCHANGE){if(dialogTheme)dialogTheme->window(dialog);layoutEditor(dialog,*e);return TRUE;}
    if(message==WM_VSCROLL || message==WM_HSCROLL || message==WM_MOUSEWHEEL)
    {
        const bool horizontal=message==WM_HSCROLL;const HWND bar=horizontal?e->horizontal:e->vertical;SCROLLINFO scroll{};scroll.cbSize=sizeof(scroll);scroll.fMask=SIF_ALL;GetScrollInfo(bar,SB_CTL,&scroll);int next=horizontal?e->x:e->y;
        if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*e->scale(48);
        else switch(LOWORD(wparam)){case SB_LINEUP:next-=e->scale(24);break;case SB_LINEDOWN:next+=e->scale(24);break;case SB_PAGEUP:next-=static_cast<int>(scroll.nPage);break;case SB_PAGEDOWN:next+=static_cast<int>(scroll.nPage);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=(!dialogTheme || !dialogTheme->highContrast) && GetPropW(bar,L"FolderstormCustomScroll")?scroll.nPos:scroll.nTrackPos;break;default:break;}
        scrollEditor(dialog,*e,horizontal?next:e->x,horizontal?e->y:next);return TRUE;
    }
    if(message==WM_APP+2)
    {
        const HWND child=reinterpret_cast<HWND>(lparam);if(GetParent(child)!=e->body)return TRUE;RECT r{},client{};GetWindowRect(child,&r);MapWindowPoints(nullptr,e->body,reinterpret_cast<POINT*>(&r),2);GetClientRect(e->body,&client);
        // A combo's native height includes its closed dropdown. Reveal only its selection box.
        wchar_t klass[32]{};GetClassNameW(child,klass,32);if(!_wcsicmp(klass,L"COMBOBOX"))r.bottom=r.top+e->scale(25);
        scrollEditor(dialog,*e,e->x+(r.left<0?r.left:r.right>client.right?r.right-client.right:0),e->y+(r.top<0?r.top:r.bottom>client.bottom?r.bottom-client.bottom:0));return TRUE;
    }
    if(message==WM_DRAWITEM)
    {
        const auto& draw=*reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if(draw.CtlID>=100 && draw.CtlID<100+e->fields->size() && (*e->fields)[draw.CtlID-100].type==FieldType::Color)
        {
            const auto& f=(*e->fields)[draw.CtlID-100];const auto rgb=number(f);if(dialogTheme)dialogTheme->button(draw);else FillRect(draw.hDC,&draw.rcItem,GetSysColorBrush(COLOR_BTNFACE));
            RECT swatch=draw.rcItem;swatch.left+=e->scale(5);swatch.top+=e->scale(4);swatch.bottom-=e->scale(4);swatch.right=swatch.left+e->scale(35);const auto brush=CreateSolidBrush(RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));FillRect(draw.hDC,&swatch,brush);FrameRect(draw.hDC,&swatch,GetSysColorBrush(COLOR_WINDOWTEXT));DeleteObject(brush);return TRUE;
        }
        if(e->sidebar && draw.CtlID==90)
        {
            const bool selected=(draw.itemState&ODS_SELECTED)!=0;const COLORREF background=dialogTheme?(selected?dialogTheme->accent:dialogTheme->background):GetSysColor(selected?COLOR_HIGHLIGHT:COLOR_WINDOW);
            const auto rgb=[](COLORREF c){return (static_cast<unsigned int>(GetRValue(c))<<16)|(static_cast<unsigned int>(GetGValue(c))<<8)|GetBValue(c);};
            const COLORREF foreground=dialogTheme?selected&&dialogTheme->highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):selected?(fs_session::colorContrast(rgb(background),0)>fs_session::colorContrast(rgb(background),0xffffff)?RGB(0,0,0):RGB(255,255,255)):dialogTheme->text:GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT);
            const auto brush=CreateSolidBrush(background);FillRect(draw.hDC,&draw.rcItem,brush);DeleteObject(brush);const auto& labels=e->structured()?e->categories:e->pages;
            if(draw.itemID<labels.size()){RECT r=draw.rcItem;r.left+=e->scale(8);SetTextColor(draw.hDC,foreground);SetBkMode(draw.hDC,TRANSPARENT);DrawTextW(draw.hDC,labels[draw.itemID].c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}if(draw.itemState&ODS_FOCUS)DrawFocusRect(draw.hDC,&draw.rcItem);return TRUE;
        }
    }
    if(dialogTheme)
    {
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(dialog,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,dialogTheme->backBrush);return TRUE;}
        if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX || message==WM_CTLCOLORBTN)return dialogTheme->color(message,wparam,lparam);
        if(message==WM_DRAWITEM && (dialogTheme->button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)) || dialogTheme->drawCombo(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))))return TRUE;
    }
    if(message==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
    if(message==WM_NOTIFY)
    {
        const auto* header=reinterpret_cast<NMHDR*>(lparam);
        if(header->code==TCN_SELCHANGE)
        {if(header->idFrom==EditorTabs){const int tab=TabCtrl_GetCurSel(e->subTabs);if(tab>=0 && static_cast<std::size_t>(tab)<e->tabPages.size())selectEditorPage(dialog,*e,e->tabPages[static_cast<std::size_t>(tab)]);return TRUE;}if(header->idFrom==90){selectEditorPage(dialog,*e,static_cast<unsigned int>(TabCtrl_GetCurSel(header->hwndFrom)));return TRUE;}}
    }
    if(message!=WM_COMMAND)return FALSE;
    const unsigned int id=LOWORD(wparam);
    if(id==90 && e->sidebar && HIWORD(wparam)==LBN_SELCHANGE)
    {
        const auto selected=SendMessageW(e->tabs,LB_GETCURSEL,0,0);if(selected>=0)
        {unsigned int page=static_cast<unsigned int>(selected);if(e->structured()){const auto category=e->categoryIds[page];const auto found=std::find_if(e->navigation.begin(),e->navigation.end(),[category](const EditorPage& p){return p.category==category;});const auto prior=e->lastTabs.find(category);page=prior==e->lastTabs.end()?static_cast<unsigned int>(found-e->navigation.begin()):prior->second;}selectEditorPage(dialog,*e,page);}return TRUE;
    }
    if(id>=100 && id<100+e->fields->size())
    {
        auto& f=(*e->fields)[id-100];const bool choice=f.type==FieldType::Choice && (HIWORD(wparam)==CBN_SELENDOK || (HIWORD(wparam)==CBN_SELCHANGE&&!SendMessageW(f.control,CB_GETDROPPEDSTATE,0,0)));
        const bool check=f.type==FieldType::Check && HIWORD(wparam)==BN_CLICKED;
        if(!e->updating && !f.transient && (choice||check||((f.type==FieldType::Text||f.type==FieldType::Number)&&HIWORD(wparam)==EN_CHANGE)))dirtyEditor(dialog);
        if(!e->updating && f.changed && (choice||check))f.changed(dialog);
        if(f.type==FieldType::Action && HIWORD(wparam)==BN_CLICKED && f.action)f.action(dialog);
        if(f.type==FieldType::Color && HIWORD(wparam)==BN_CLICKED)
        {
            static COLORREF custom[16]{};const auto rgb=number(f);CHOOSECOLORW color{};color.lStructSize=sizeof(color);color.hwndOwner=dialog;color.lpCustColors=custom;color.rgbResult=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);color.Flags=CC_FULLOPEN|CC_RGBINIT;
            if(ChooseColorW(&color)){f.value=std::to_wstring((static_cast<unsigned int>(GetRValue(color.rgbResult))<<16)|(static_cast<unsigned int>(GetGValue(color.rgbResult))<<8)|GetBValue(color.rgbResult));InvalidateRect(f.control,nullptr,TRUE);dirtyEditor(dialog);}return TRUE;
        }
        return TRUE;
    }
    if(id==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
    if(id==IDOK || id==EditorApply){applyEditor(dialog,*e,id==IDOK);return TRUE;}
    return FALSE;
}
inline bool edit(HWND owner,const wchar_t* title,std::vector<Field>& fields,std::vector<std::wstring> pages={},unsigned int page=0,bool sidebar=false,std::function<bool(HWND)> validate={},std::vector<EditorPage> navigation={},std::function<bool(HWND)> apply={},unsigned int* lastPage=nullptr,HWND positionNear=nullptr)
{
    struct Template{DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;}layout{};layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|WS_THICKFRAME|WS_CLIPCHILDREN;layout.dialog.cx=300;layout.dialog.cy=200;
    Editor editor(title,fields,std::move(pages));editor.sidebar=sidebar;editor.validate=std::move(validate);editor.navigation=std::move(navigation);editor.apply=std::move(apply);editor.lastPage=lastPage;editor.positionNear=positionNear;
    const auto count=editor.structured()?editor.navigation.size():editor.pages.size();editor.page=static_cast<int>((std::min)(page,count?static_cast<unsigned int>(count-1):0u));
    const bool accepted=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,owner,editorProc,reinterpret_cast<LPARAM>(&editor))==IDOK;if(editor.font)DeleteObject(editor.font);return accepted;
}
}
#endif
