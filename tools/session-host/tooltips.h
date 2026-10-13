/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_TOOLTIPS_H
#define FS_HOST_TOOLTIPS_H
#include "theme.h"
#include <map>
namespace fs_host_ui
{
// Each root panel owns its tooltip window; entries outlive borrowed Win32 text pointers.
class Tooltips
{
    struct Entry {HWND parent=nullptr,tip=nullptr;std::wstring text;};
    struct Window {HWND tip=nullptr;HFONT font=nullptr;};
    std::map<HWND,Entry> entries;
    std::map<HWND,Window> windows;
    bool owns(HWND root,HWND tip) const
    {return GetPropW(tip,L"FolderstormTooltipOwner")==reinterpret_cast<HANDLE>(const_cast<Tooltips*>(this)) && GetWindow(tip,GW_OWNER)==root;}
    static void colors(HWND tip,const Theme& theme)
    {
        SetWindowTheme(tip,theme.highContrast?nullptr:L"",theme.highContrast?nullptr:L"");
        SendMessageW(tip,TTM_SETTIPBKCOLOR,theme.background,0);SendMessageW(tip,TTM_SETTIPTEXTCOLOR,theme.text,0);
    }
    void erase(HWND child)
    {
        const auto found=entries.find(child);if(found==entries.end())return;
        TOOLINFOW tool{};tool.cbSize=sizeof(tool);tool.hwnd=found->second.parent;tool.uId=reinterpret_cast<UINT_PTR>(child);
        if(GetPropW(found->second.tip,L"FolderstormTooltipOwner")==reinterpret_cast<HANDLE>(this))SendMessageW(found->second.tip,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&tool));entries.erase(found);
    }
    static LRESULT CALLBACK childProc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)
    {
        if(message==WM_NCDESTROY){reinterpret_cast<Tooltips*>(data)->erase(child);RemoveWindowSubclass(child,childProc,8);}
        return DefSubclassProc(child,message,wp,lp);
    }
public:
    Tooltips()=default;
    Tooltips(const Tooltips&)=delete;Tooltips& operator=(const Tooltips&)=delete;
    ~Tooltips(){for(const auto& entry:entries)if(IsWindow(entry.first))RemoveWindowSubclass(entry.first,childProc,8);for(const auto& window:windows)if(owns(window.first,window.second.tip))DestroyWindow(window.second.tip);}
    void style(const Theme& theme){for(auto i=windows.begin();i!=windows.end();){if(!owns(i->first,i->second.tip))i=windows.erase(i);else{colors(i->second.tip,theme);++i;}}}
    void set(HWND child,const wchar_t* text,const Theme& theme)
    {
        if(!child || !IsWindow(child) || !text)return;
        if(windows.size()>12)for(auto i=windows.begin();i!=windows.end();){if(!owns(i->first,i->second.tip))i=windows.erase(i);else ++i;}
        const HWND parent=GetParent(child),root=GetAncestor(child,GA_ROOT);auto& window=windows[root];
        if(!owns(root,window.tip))
        {
            window.tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
                CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,root,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!window.tip)return;
            SetPropW(window.tip,L"FolderstormTooltipOwner",reinterpret_cast<HANDLE>(this));window.font=nullptr;
            SendMessageW(window.tip,TTM_SETMAXTIPWIDTH,0,420);SendMessageW(window.tip,TTM_SETDELAYTIME,TTDT_INITIAL,500);
            SendMessageW(window.tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,12000);colors(window.tip,theme);
        }
        const auto font=reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0));
        if(font && font!=window.font){SendMessageW(window.tip,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);window.font=font;}
        auto found=entries.find(child);
        if(found!=entries.end() && (found->second.parent!=parent || found->second.tip!=window.tip)){erase(child);found=entries.end();}
        if(found!=entries.end() && found->second.text==text)return; // Preserve a running hover timer.
        const bool exists=found!=entries.end();auto& entry=entries[child];entry.parent=parent;entry.tip=window.tip;entry.text=text;
        TOOLINFOW tool{};tool.cbSize=sizeof(tool);tool.hwnd=parent;tool.uId=reinterpret_cast<UINT_PTR>(child);
        tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.lpszText=entry.text.data();
        SendMessageW(window.tip,exists?TTM_UPDATETIPTEXTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
        SetWindowSubclass(child,childProc,8,reinterpret_cast<DWORD_PTR>(this));
    }
    void reparent(HWND child,const Theme& theme)
    {const auto found=entries.find(child);if(found!=entries.end()){const auto text=found->second.text;set(child,text.c_str(),theme);}}
};
}
#endif
