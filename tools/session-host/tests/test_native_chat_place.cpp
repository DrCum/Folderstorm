/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "chatui.h"
#include <cassert>
#include <iostream>
#include <string>

using fs_host_ui::Transcript;
using fs_session::ChatKey;

static void show(Transcript& view, const ChatKey& key, int first, int last)
{
    std::wstring text;
    std::vector<std::pair<std::uint64_t,LONG>> offsets;
    for (int event = first; event <= last; ++event)
    {
        offsets.emplace_back(event, static_cast<LONG>(text.size()));
        text += L"Message " + std::to_wstring(event) + L"\r\n";
    }
    view.update(key, std::move(text), std::move(offsets), last);
}

static std::uint64_t firstEvent(const Transcript& view)
{
    const auto line = SendMessageW(view.edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    const auto position = SendMessageW(view.edit, EM_LINEINDEX, line, 0);
    std::uint64_t event = 0;
    for (const auto& entry : view.offsets)
    {
        if (entry.second > position) break;
        event = entry.first;
    }
    return event;
}

int main()
{
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
    assert(InitCommonControlsEx(&common));
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 400, 200,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                0, 0, 300, 72, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(parent && edit); // The parent stays hidden for this entire check.
    Transcript view; view.attach(edit, nullptr);
    ChatKey a; a.worker[0] = 1; a.generation = 1; a.grid = "grid"; a.account = "avatar"; a.conversation = ""; // Nearby uses the empty conversation ID.
    ChatKey b = a; b.conversation = "two";
    show(view, a, 1, 90);
    SendMessageW(edit, EM_LINESCROLL, 0, -34);
    view.observe();
    const auto paused = firstEvent(view);
    assert(!view.following && paused > 12 && paused < 88);
    show(view, b, 1, 50);
    assert(view.following);
    // A differently selected string in another conversation is never restored here.
    SendMessageW(edit, EM_SETSEL, 5, 13);
    view.observe();
    view.update(a, L"", {}, 0); // The worker can deliver the old conversation on a later refresh.
    assert(!view.following);
    show(view, a, 10, 100);
    assert(!view.following && firstEvent(view) == paused);
    DWORD start = 0, end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    assert(start == end);
    view.update(a, L"", {}, 0); // Hiding monitor Chat clears its rendered history without changing its key.
    assert(!view.following && view.text.empty());
    show(view,a,10,102);
    assert(!view.following && firstEvent(view)==paused);
    show(view, b, 1, 75);
    show(view, a, 150, 200); // The remembered event has been evicted by bounded history.
    assert(!view.following && firstEvent(view) == 150);
    view.tail();show(view,b,1,60);show(view,a,10,105);
    assert(view.following); // Latest clears the pause.
    SendMessageW(edit,EM_LINESCROLL,0,-24);view.observe();assert(!view.following);
    show(view,b,1,60);
    ChatKey relog=a; ++relog.generation;
    show(view,relog,1,90);assert(view.following);
    DestroyWindow(parent);
    std::cout << "Native transcript conversation and login ownership passed.\n";
}
