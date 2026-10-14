/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_CHAT_UI_H
#define FS_HOST_CHAT_UI_H
#include <windows.h>
#include <commctrl.h>
#include <imm.h>
#include <functional>
#include <vector>
#include <algorithm>
#include "fssessionchatmodel.h"
#include "fssessionchatbehavior.h"
namespace fs_host_ui
{
inline bool selectAllChatText(HWND edit,UINT message,WPARAM key)
{
    if(message!=WM_KEYDOWN && message!=WM_CHAR)return false;
    if(!(GetKeyState(VK_CONTROL)&0x8000) || (GetKeyState(VK_MENU)&0x8000))return false;
    if(message==WM_KEYDOWN && key=='A'){SendMessageW(edit,EM_SETSEL,0,-1);return true;}
    return message==WM_CHAR && key==1; // Consume Ctrl+A's control character.
}
struct Transcript
{
    HWND edit = nullptr, latest = nullptr;
    fs_session::ChatKey source;
    std::wstring text;
    std::vector<std::pair<std::uint64_t,LONG>> offsets;
    std::uint64_t watermark = 0, painted = 0;
    bool bound = false, following = true, changing = false;
    bool atTail() const
    {
        SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_ALL;
        if (!GetScrollInfo(edit,SB_VERT,&info)) return true;
        return info.nPos >= info.nMax-static_cast<int>(info.nPage)+1;
    }
    bool visible() const
    {
        const HWND root = GetAncestor(edit,GA_ROOT); RECT rect{};
        return edit && IsWindowVisible(edit) && root && !IsIconic(root) && GetClientRect(edit,&rect) && rect.right > 0 && rect.bottom > 0;
    }
    bool readReady() const { return visible() && following && atTail() && painted == watermark; }
    void latestVisibility()
    { if (latest) ShowWindow(latest,visible() && !following ? SW_SHOWNOACTIVATE : SW_HIDE); }
    void tail()
    {
        changing = true; following = true;
        const auto end = GetWindowTextLengthW(edit);
        SendMessageW(edit,EM_SETSEL,static_cast<WPARAM>(end),static_cast<LPARAM>(end));
        SendMessageW(edit,EM_SCROLLCARET,0,0); SendMessageW(edit,WM_VSCROLL,SB_BOTTOM,0);
        changing = false; latestVisibility(); InvalidateRect(edit,nullptr,FALSE);
    }
    void observe()
    {
        if (changing) return;
        DWORD start = 0, end = 0; SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        following = atTail() && start == end; latestVisibility();
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)
    {
        auto& view = *reinterpret_cast<Transcript*>(data);
        if(selectAllChatText(window,message,wp)){view.observe();return 0;}
        const auto result = DefSubclassProc(window,message,wp,lp);
        if (message == WM_PAINT && view.visible() && view.following && view.atTail()) view.painted = view.watermark;
        if (message == WM_VSCROLL || message == WM_MOUSEWHEEL || message == WM_KEYUP || message == WM_LBUTTONUP) view.observe();
        if (message == WM_SIZE || message == WM_SHOWWINDOW)
        { if (view.following && view.visible()) view.tail(); else view.latestVisibility(); }
        if (message == WM_NCDESTROY) { RemoveWindowSubclass(window,proc,5); view.edit = nullptr; }
        return result;
    }
    void attach(HWND control,HWND jump)
    { edit = control; latest = jump; SetWindowSubclass(edit,proc,5,reinterpret_cast<DWORD_PTR>(this)); }
    void update(const fs_session::ChatKey& key,std::wstring next,std::vector<std::pair<std::uint64_t,LONG>> positions,std::uint64_t through)
    {
        const bool changed = !bound || source.worker != key.worker || source.generation != key.generation ||
            source.account != key.account || source.grid != key.grid || source.conversation != key.conversation;
        if (changed) { source = key; bound = true; following = true; painted = 0; }
        if (next.empty()) { following = true; latestVisibility(); }
        if (next == text && !changed)
        { if (watermark != through) { watermark = through; InvalidateRect(edit,nullptr,FALSE); } return; }
        DWORD start = 0, end = 0; SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        const LONG firstLine = static_cast<LONG>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0));
        const LONG firstChar = static_cast<LONG>(SendMessageW(edit,EM_LINEINDEX,static_cast<WPARAM>(firstLine),0));
        const auto fitted = [&](LONG position)
        { return fs_session::fitTranscriptAnchor(position,offsets,positions,static_cast<LONG>(next.size())); };
        const LONG anchor = fitted(firstChar), selectionStart = fitted(static_cast<LONG>(start)), selectionEnd = fitted(static_cast<LONG>(end));
        changing = true;
        if (!changed && next.size() >= text.size() && next.compare(0,text.size(),text) == 0)
        {
            const auto length = static_cast<WPARAM>(text.size()); SendMessageW(edit,EM_SETSEL,length,static_cast<LPARAM>(length));
            SendMessageW(edit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(next.c_str()+text.size()));
        }
        else SetWindowTextW(edit,next.c_str());
        text = std::move(next); offsets = std::move(positions); watermark = through; painted = 0;
        if (!following)
        {
            SendMessageW(edit,EM_SETSEL,static_cast<WPARAM>(selectionStart),selectionEnd);
            const LONG desired = static_cast<LONG>(SendMessageW(edit,EM_LINEFROMCHAR,static_cast<WPARAM>(anchor),0));
            const LONG current = static_cast<LONG>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0));
            SendMessageW(edit,EM_LINESCROLL,0,desired-current);
        }
        changing = false;
        if (following) tail(); else { latestVisibility(); InvalidateRect(edit,nullptr,FALSE); }
    }
};
struct Composer
{
    HWND edit = nullptr;
    std::function<void()> send,enter;
    std::function<bool()> acceptsInput;
    fs_session::ComposerReturn keys;
    static bool inputMessage(UINT message)
    {
        return message==WM_KEYDOWN || message==WM_KEYUP || message==WM_CHAR || message==WM_UNICHAR ||
            message==WM_SYSKEYDOWN || message==WM_SYSKEYUP || message==WM_SYSCHAR ||
            message==WM_PASTE || message==WM_CUT || message==WM_CLEAR ||
            message==WM_IME_STARTCOMPOSITION || message==WM_IME_COMPOSITION ||
            message==WM_IME_ENDCOMPOSITION || message==WM_IME_CHAR;
    }
    bool allowsInput(UINT message) const
    {return !inputMessage(message) || !acceptsInput || acceptsInput();}
    void releaseFocus(HWND target)
    {
        keys={};
        if(edit && GetFocus()==edit)
        {
            const HIMC context=ImmGetContext(edit);
            if(context){ImmNotifyIME(context,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(edit,context);}
            SetFocus(target);
        }
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)
    {
        auto& composer = *reinterpret_cast<Composer*>(data);
        if(message==WM_SETFOCUS && composer.enter)composer.enter();
        if(!composer.allowsInput(message)){composer.keys={};return 0;}
        if(selectAllChatText(window,message,wp))return 0;
        if (message == WM_GETDLGCODE)
        {
            const auto result = DefSubclassProc(window,message,wp,lp);
            const auto* key = reinterpret_cast<const MSG*>(lp);
            return key && key->wParam == VK_RETURN && (key->message == WM_KEYDOWN || key->message == WM_CHAR) ? result|DLGC_WANTMESSAGE : result;
        }
        if (message == WM_IME_STARTCOMPOSITION) composer.keys.ime = true;
        if (message == WM_IME_ENDCOMPOSITION) composer.keys.ime = false;
        if (message == WM_KEYDOWN && wp == VK_RETURN)
        {
            composer.keys.down((lp & (1LL<<30)) != 0,(GetKeyState(VK_SHIFT)&0x8000) != 0);
            if (lp & (1LL<<30)) return 0;
            if (!composer.keys.imeReturn) return 0;
        }
        if (message == WM_CHAR && wp == L'\r')
        {
            const auto action = composer.keys.character((lp & (1LL<<30)) != 0);
            if (action == fs_session::ReturnAction::Ignore) return 0;
            if (action == fs_session::ReturnAction::Newline) return DefSubclassProc(window,message,wp,lp);
            if (IsWindowEnabled(window) && composer.send) composer.send();
            return 0;
        }
        if (message == WM_KEYUP && wp == VK_RETURN) composer.keys.up();
        if (message == WM_NCDESTROY) { RemoveWindowSubclass(window,proc,6); composer.edit = nullptr; }
        return DefSubclassProc(window,message,wp,lp);
    }
    void attach(HWND control,std::function<void()> action)
    { edit = control; send = std::move(action); SetWindowSubclass(edit,proc,6,reinterpret_cast<DWORD_PTR>(this)); }
};
}
#endif
