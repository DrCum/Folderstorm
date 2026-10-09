/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
// Windows feasibility controller. Launch explicitly; never started by login.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <psapi.h>
#include "fssessionpipe.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

#ifndef FS_SESSION_VIEWER_FILENAME
#define FS_SESSION_VIEWER_FILENAME "firestorm-bin.exe"
#endif

namespace
{
using namespace fs_session;
constexpr int Launch1 = 101, Launch2 = 102, Switch1 = 103, Switch2 = 104, HostSurface = 105, DetachAll = 106;
constexpr int Standby1 = 107, Standby2 = 108;
constexpr UINT_PTR Timer = 1;
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE take() { const HANDLE result = value; value = INVALID_HANDLE_VALUE; return result; }
};
std::wstring wide(const std::string& text)
{
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) return L"?";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
std::wstring hexHandle(HANDLE handle)
{
    std::wostringstream result; result << std::hex << reinterpret_cast<std::uintptr_t>(handle); return result.str();
}
std::wstring hexId(const WorkerId& identity)
{
    std::wostringstream result;
    for (auto byte : identity) result << std::hex << std::setw(2) << std::setfill(L'0') << static_cast<unsigned int>(byte);
    return result.str();
}
struct EnvLess { bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; } };
std::vector<wchar_t> environment(const std::filesystem::path& profile, HANDLE pipe, HANDLE lease, const WorkerId& id)
{
    // Preserve proxy/trust/runtime variables. Read only to form the child block;
    // never log the block or mutate this controller's environment.
    std::map<std::wstring, std::wstring, EnvLess> entries;
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return {};
    for (const wchar_t* entry = block; *entry; entry += wcslen(entry) + 1)
    {
        const std::wstring text(entry);
        const auto equals = text.find(L'=', text[0] == L'=' ? 1 : 0);
        if (equals != std::wstring::npos) entries[text.substr(0, equals)] = text.substr(equals + 1);
    }
    FreeEnvironmentStringsW(block);
    entries[L"APPDATA"] = (profile / L"Roaming").wstring();
    entries[L"LOCALAPPDATA"] = (profile / L"Local").wstring();
    entries[L"FOLDERSTORM_SESSION_PIPE"] = hexHandle(pipe);
    entries[L"FOLDERSTORM_SESSION_LOCK"] = hexHandle(lease);
    entries[L"FOLDERSTORM_SESSION_ID"] = hexId(id);
    std::vector<wchar_t> result;
    for (const auto& entry : entries)
    {
        const auto line = entry.first + L"=" + entry.second;
        result.insert(result.end(), line.begin(), line.end()); result.push_back(0);
    }
    result.push_back(0);
    return result;
}
class PipePair
{
public:
    Handle server, client;
    bool create(const WorkerId& id)
    {
        Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return false;
        DWORD size = 0; GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        std::vector<std::uint8_t> user(size);
        if (!size || !GetTokenInformation(token.value, TokenUser, user.data(), size, &size)) return false;
        wchar_t* sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid)) return false;
        const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        const std::wstring name = L"\\\\.\\pipe\\FolderstormSession-" + hexId(id);
        server.value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, &security);
        LocalFree(descriptor);
        if (server.value == INVALID_HANDLE_VALUE) return false;
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) return false;
        OVERLAPPED connection{}; connection.hEvent = event.value;
        if (ConnectNamedPipe(server.value, &connection) || GetLastError() != ERROR_IO_PENDING) return false;
        SECURITY_ATTRIBUTES inheritance{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        client.value = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &inheritance, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        DWORD ignored = 0;
        if (client.value == INVALID_HANDLE_VALUE)
        {
            CancelIoEx(server.value, &connection); GetOverlappedResult(server.value, &connection, &ignored, TRUE);
            return false;
        }
        return GetOverlappedResult(server.value, &connection, &ignored, TRUE) != FALSE;
    }
};
struct Slot
{
    Pipe pipe;
    Handle process;
    WorkerId id{};
    DWORD pid = 0;
    std::uint64_t nextSequence = 0;
    ULONGLONG sentAt = 0, pollAt = 0, sampleAt = 0;
    Message snapshot, request, sample;
    std::string reservation;
    bool waiting = false, detached = false;
    double fps = 0, loops = 0;
    bool running() const { return process.value != INVALID_HANDLE_VALUE && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT; }
    HWND surface() const
    {
        HWND result = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(snapshot.surface));
        DWORD owner = 0;
        return result && IsWindow(result) && GetWindowThreadProcessId(result, &owner) && owner == pid ? result : nullptr;
    }
};
struct Host
{
    HWND window = nullptr, viewport = nullptr, status[2]{}, notice = nullptr, embedControl = nullptr;
    HWND launchButton[2]{}, switchButton[2]{};
    HWND standbyControl[2]{};
    Mode standby[2]{Mode::Warm, Mode::Warm};
    std::unique_ptr<Slot> slots[2];
    Handoff handoff;
    int active = -1;
    bool embedding = false, detaching = false, closing = false, selectFirst = true, focusRequested = false;
    std::filesystem::path viewer, profiles;

    void message(const std::wstring& text) { SetWindowTextW(notice, text.c_str()); }
    bool send(int index, Kind kind, Mode mode = Mode::Warm, std::uint64_t surface = 0)
    {
        auto& slot = *slots[index];
        if (slot.waiting || !slot.pipe.alive() || slot.pipe.writing()) return false;
        Message request;
        request.worker = slot.id; request.kind = kind; request.mode = mode; request.surface = surface;
        request.generation = slot.snapshot.generation; request.sequence = ++slot.nextSequence;
        if (kind == Kind::PermitLogin || kind == Kind::DenyLogin) { request.grid = slot.snapshot.grid; request.name = slot.snapshot.name; }
        if (!slot.pipe.send(request)) return false;
        slot.request = request; slot.waiting = true; slot.sentAt = GetTickCount64();
        return true;
    }
    void launch(int index)
    {
        if (slots[index] && slots[index]->running()) { message(L"This slot still has a viewer open. Close that viewer before relaunching."); return; }
        std::error_code error;
        if (!std::filesystem::is_regular_file(viewer, error) || error) { message(L"Viewer executable missing. Keep this controller beside the matching viewer."); return; }
        auto slot = std::make_unique<Slot>();
        if (BCryptGenRandom(nullptr, slot->id.data(), static_cast<ULONG>(slot->id.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        { message(L"Unable to create the private session identity."); return; }
        const auto profile = profiles / (index == 0 ? L"Character1" : L"Character2");
        std::filesystem::create_directories(profile / L"Roaming", error);
        if (!error) std::filesystem::create_directories(profile / L"Local", error);
        if (error) { message(L"Unable to create the separate character profile."); return; }
        SECURITY_ATTRIBUTES inheritance{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle lease(CreateFileW((profile / L"profile.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, &inheritance,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
        if (lease.value == INVALID_HANDLE_VALUE) { message(L"This character profile is already open in another controller or viewer."); return; }
        PipePair pair;
        if (!pair.create(slot->id)) { message(L"Unable to create the private worker connection."); return; }
        if (!slot->pipe.open(pair.server.take())) { message(L"Unable to initialize asynchronous worker control."); return; }
        auto childEnvironment = environment(profile, pair.client.value, lease.value, slot->id);
        if (childEnvironment.empty()) { message(L"Unable to prepare the worker environment."); return; }
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        std::vector<std::uint8_t> attributes(attributeBytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) { message(L"Unable to restrict inherited handles."); return; }
        HANDLE inherited[] = {pair.client.value, lease.value};
        const bool attributesReady = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = list;
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + viewer.wstring() + L"\"";
        const bool launched = attributesReady && CreateProcessW(viewer.c_str(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, childEnvironment.data(), viewer.parent_path().c_str(),
            &startup.StartupInfo, &process) != FALSE;
        DeleteProcThreadAttributeList(list);
        if (!launched) { message(L"Unable to launch the matching viewer."); return; }
        CloseHandle(process.hThread);
        slot->process.value = process.hProcess; slot->pid = process.dwProcessId;
        slot->snapshot.worker = slot->id;
        slots[index] = std::move(slot);
        message(L"Log in a different character in each viewer. The first ready character becomes active.");
    }
    bool busy() const { return handoff.step() != Handoff::Step::Idle || detaching; }
    bool waitingForLostOwner(int target) const
    {
        for (int index = 0; index < 2; ++index) if (index != target && slots[index] && !slots[index]->pipe.alive())
            if (auto surface = slots[index]->surface())
                if (GetPropW(surface, InputLeaseProperty) == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(GetCurrentProcessId()))) return true;
        return false;
    }
    void switchTo(int index)
    {
        if (busy() || !slots[index] || slots[index]->detached || !slots[index]->pipe.alive()) return;
        if (slots[index]->snapshot.state != State::Ready) { message(L"Finish login before switching this character."); return; }
        if (index == active)
        {
            if (embedding) { SetForegroundWindow(window); focusRequested = true; }
            else if (auto surface = slots[index]->surface()) SetForegroundWindow(surface);
            return;
        }
        const auto oldGeneration = active >= 0 && slots[active] ? slots[active]->snapshot.generation : 0;
        if (handoff.begin(active, index, oldGeneration, slots[index]->snapshot.generation, active < 0 ? Mode::Warm : standby[active]))
            message(L"Switching character; waiting for the viewer's ready frame…");
    }
    void failHandoff(int index, const std::wstring& error)
    {
        // Closing an uncertain promotion's capability prevents a delayed grant
        // from remaining a managed owner when the old worker is restored.
        if (handoff.step() == Handoff::Step::Promote && slots[index])
        { slots[index]->pipe.close(); slots[index]->detached = true; }
        handoff.fail();
        message(error);
        if (handoff.step() == Handoff::Step::Idle && active >= 0 && (!slots[active] || !slots[active]->pipe.alive())) active = -1;
    }
    void reply(int index, const Message& response)
    {
        auto& slot = *slots[index];
        if (!slot.waiting || response.kind != Kind::Status || response.worker != slot.id || response.pid != slot.pid || response.sequence != slot.request.sequence)
        { slot.pipe.close(); message(L"Worker identity or reply sequence changed; returning it to a separate viewer."); return; }
        const auto request = slot.request;
        slot.waiting = false; slot.pollAt = GetTickCount64();
        slot.snapshot = response;
        if (handoff.step() == Handoff::Step::Idle && active == index &&
            (response.state != State::Ready || response.mode != Mode::Active)) active = -1;
        if (slot.sampleAt && slot.pollAt > slot.sampleAt && response.generation == slot.sample.generation)
        {
            const double seconds = static_cast<double>(slot.pollAt - slot.sampleAt) / 1000.;
            slot.fps = static_cast<double>(response.frames - slot.sample.frames) / seconds;
            slot.loops = static_cast<double>(response.maintenance - slot.sample.maintenance) / seconds;
        }
        slot.sample = response; slot.sampleAt = slot.pollAt;
        if ((response.state == State::Login && !(response.flags & LoginPending)) || response.state == State::Disconnected) slot.reservation.clear();
        if (request.kind == Kind::Detach && !(response.flags & (Error | Embedded)))
        {
            slot.detached = true; slot.pipe.close();
            if (active == index) active = -1;
        }
        if (response.flags & Error)
        {
            if (request.kind == Kind::Detach) { detaching = closing = false; }
            if (request.kind == Kind::Embed) { embedding = false; SendMessageW(embedControl, BM_SETCHECK, BST_UNCHECKED, 0); }
            if (request.kind == Kind::SetMode && handoff.step() != Handoff::Step::Idle) failHandoff(index, wide(response.detail));
            else
            {
                // A blocked policy change needs a new deliberate choice, not
                // an automatic retry every timer tick while a modal is open.
                if (request.kind == Kind::SetMode && response.mode != Mode::Active)
                {
                    standby[index] = response.mode;
                    SendMessageW(standbyControl[index], CB_SETCURSEL, response.mode == Mode::Economy ? 1 : 0, 0);
                }
                message(wide(response.detail));
            }
            return;
        }
        if (request.kind == Kind::SetMode && handoff.step() != Handoff::Step::Idle)
        {
            const auto step = handoff.step();
            if (!handoff.accept(index, response)) { failHandoff(index, L"Session changed while switching; returning to the previous character."); return; }
            if (handoff.step() == Handoff::Step::Idle)
            {
                active = step == Handoff::Step::Rollback ? handoff.original() : handoff.target();
                message(step == Handoff::Step::Rollback ? L"Returned to the previous character." : L"Character ready. Other characters use their chosen standby mode.");
                if (embedding)
                {
                    // Promotion may have restored the standalone target first.
                    // Return its focus to the host without taking another app's.
                    const HWND foreground = GetForegroundWindow();
                    if (foreground == window || foreground == slots[active]->surface()) SetForegroundWindow(window);
                    focusRequested = true;
                }
                else if (auto surface = slots[active]->surface()) SetForegroundWindow(surface);
            }
            else active = -1; // Old input owner has explicitly acknowledged revoke.
        }
    }
    void describe(int index)
    {
        if (!slots[index]) { SetWindowTextW(status[index], index == 0 ? L"Character 1: not launched" : L"Character 2: not launched"); return; }
        const auto& slot = *slots[index];
        std::wostringstream text;
        text << (index == 0 ? L"1: " : L"2: ") << (slot.snapshot.name.empty() ? L"Login" : wide(slot.snapshot.name));
        text << (!slot.running() ? L" · closed" : slot.detached ? L" · separate viewer" : !slot.pipe.alive() ? L" · control lost" : slot.snapshot.state == State::Disconnected ? L" · disconnected" :
            slot.snapshot.state != State::Ready ? L" · connecting" : slot.snapshot.mode == Mode::Warm ? L" · warm" :
                slot.snapshot.mode == Mode::Economy ? L" · economy (experimental)" : L" · active");
        if (slot.snapshot.flags & EconomyTrimmed) text << L" · render targets released";
        if (slot.pipe.alive() && !slot.detached && slot.snapshot.state == State::Ready && slot.snapshot.mode == Mode::Active)
            text << ((slot.snapshot.flags & ClientFocused) ? L" · keyboard focused" : L" · keyboard unfocused");
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
        if (slot.running() && GetProcessMemoryInfo(slot.process.value, &memory, sizeof(memory)))
            text << L" · " << memory.WorkingSetSize / (1024 * 1024) << L" MiB";
        text << std::fixed << std::setprecision(1) << L" · " << slot.fps << L" draws/s · " << slot.loops << L" loops/s";
        SetWindowTextW(status[index], text.str().c_str());
    }
    void tick()
    {
        for (int index = 0; index < 2; ++index)
        {
            if (!slots[index]) continue;
            auto& slot = *slots[index];
            Message response;
            if (slot.pipe.receive(response)) reply(index, response);
            if (!slot.running() || !slot.pipe.alive())
            {
                slot.waiting = false;
                if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index) failHandoff(index, L"Character connection lost. Other connected characters can still be selected.");
                if (active == index) active = -1;
            }
            if (slot.waiting && GetTickCount64() - slot.sentAt > (slot.snapshot.state == State::Ready ? 12000ULL : 60000ULL))
            {
                slot.pipe.close(); slot.waiting = false; slot.detached = true;
                if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index) failHandoff(index, L"The viewer did not respond; returning it to an ordinary window.");
                else message(L"The viewer did not respond; its local watchdog will return it to an ordinary window.");
            }
            if (!slot.waiting && slot.pipe.alive() && !slot.detached)
            {
                if (detaching) send(index, Kind::Detach);
                else if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index)
                {
                    if (handoff.generation() != slot.snapshot.generation) failHandoff(index, L"Login changed while switching.");
                    else if (handoff.mode() == Mode::Active && waitingForLostOwner(index))
                    {
                        message(L"Waiting for the disconnected controller link to release its old input owner…");
                        if (GetTickCount64() - slot.pollAt >= 1000) send(index, Kind::Poll);
                    }
                    else send(index, Kind::SetMode, handoff.mode());
                }
                else if (slot.snapshot.flags & LoginPending)
                {
                    const int other = 1 - index;
                    const bool collision = slots[other] && slots[other]->running() && loginCollision(slot.snapshot, slots[other]->snapshot, slots[other]->reservation);
                    if (send(index, collision ? Kind::DenyLogin : Kind::PermitLogin))
                    {
                        if (!collision) slot.reservation = slot.snapshot.name;
                        else message(L"That character is already reserved. Use a different character in the second viewer.");
                    }
                }
                else if (index == active && slot.snapshot.state == State::Ready &&
                    embedding != ((slot.snapshot.flags & Embedded) != 0))
                {
                    send(index, embedding ? Kind::Embed : Kind::Unembed, Mode::Active,
                        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(viewport)));
                }
                else if (index == active && embedding && focusRequested &&
                    slot.snapshot.state == State::Ready && (slot.snapshot.flags & Embedded) &&
                    GetForegroundWindow() == window && !IsIconic(window))
                {
                    if (send(index, Kind::Focus)) focusRequested = false;
                }
                else if (!busy() && index != active && slot.snapshot.state == State::Ready && slot.snapshot.mode != standby[index])
                    send(index, Kind::SetMode, standby[index]);
                else if (GetTickCount64() - slot.pollAt >= 1000) send(index, Kind::Poll);
            }
            describe(index);
            EnableWindow(launchButton[index], !slot.running() && !busy());
            EnableWindow(switchButton[index], slot.running() && slot.pipe.alive() && !slot.detached && slot.snapshot.state == State::Ready && !busy());
        }
        if (!busy() && active < 0 && selectFirst)
            for (int i = 0; i < 2; ++i) if (slots[i] && slots[i]->pipe.alive() && !slots[i]->detached && slots[i]->snapshot.state == State::Ready) { switchTo(i); selectFirst = false; break; }
        if (detaching)
        {
            bool complete = true;
            for (const auto& slot : slots) if (slot)
            {
                if (slot->pipe.alive() && !slot->detached) complete = false;
                // Never destroy a live foreign child on normal controller close.
                if (auto surface = slot->surface()) if (IsChild(window, surface)) complete = false;
            }
            if (complete)
            {
                detaching = false; active = -1;
                if (closing) DestroyWindow(window);
                else message(L"Characters are back in ordinary viewer windows and remain logged in.");
            }
        }
    }
    void detach(bool close)
    {
        handoff.cancel(); detaching = true; closing = close; focusRequested = false;
        message(L"Returning characters to separate windows before closing their controller connection…");
    }
    void layout()
    {
        RECT rect{}; GetClientRect(window, &rect);
        const int width = static_cast<int>((std::max)(760L, rect.right));
        for (int i = 0; i < 2; ++i)
        {
            MoveWindow(status[i], 10, 48 + i * 24, width - 215, 20, TRUE);
            MoveWindow(standbyControl[i], width - 200, 46 + i * 24, 190, 130, TRUE);
        }
        MoveWindow(viewport, 0, 99, width, (std::max)(1L, rect.bottom - 126), TRUE);
        MoveWindow(notice, 10, (std::max)(102L, rect.bottom - 23), width - 20, 20, TRUE);
    }
};
Host* host = nullptr;
HWND control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0)
{
    HWND result = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return result;
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (!host) return DefWindowProcW(window, message, wparam, lparam);
    switch (message)
    {
    case WM_CREATE:
        host->window = window;
        host->launchButton[0] = control(window, L"BUTTON", L"Launch 1", BS_PUSHBUTTON, 10, 10, 80, 28, Launch1);
        host->switchButton[0] = control(window, L"BUTTON", L"Switch to 1", BS_PUSHBUTTON, 95, 10, 90, 28, Switch1);
        host->launchButton[1] = control(window, L"BUTTON", L"Launch 2", BS_PUSHBUTTON, 195, 10, 80, 28, Launch2);
        host->switchButton[1] = control(window, L"BUTTON", L"Switch to 2", BS_PUSHBUTTON, 280, 10, 90, 28, Switch2);
        host->embedControl = control(window, L"BUTTON", L"Host active viewer (experimental)", BS_AUTOCHECKBOX, 383, 10, 235, 28, HostSurface);
        control(window, L"BUTTON", L"Separate windows", BS_PUSHBUTTON, 630, 10, 125, 28, DetachAll);
        host->status[0] = control(window, L"STATIC", L"Character 1: not launched", SS_LEFT, 10, 48, 850, 20);
        host->status[1] = control(window, L"STATIC", L"Character 2: not launched", SS_LEFT, 10, 70, 850, 20);
        for (int i = 0; i < 2; ++i)
        {
            host->standbyControl[i] = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 760, 46 + i * 24, 190, 130, i == 0 ? Standby1 : Standby2);
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Background: Warm"));
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Economy (experimental)"));
            SendMessageW(host->standbyControl[i], CB_SETCURSEL, 0, 0);
        }
        host->viewport = control(window, L"STATIC", L"", SS_BLACKRECT | WS_CLIPCHILDREN, 0, 99, 880, 490);
        host->notice = control(window, L"STATIC", L"Prototype: warm standby, separate profiles. Voice and MCP are off.", SS_LEFT, 10, 600, 850, 20);
        EnableWindow(host->switchButton[0], FALSE); EnableWindow(host->switchButton[1], FALSE);
        SetTimer(window, Timer, 100, nullptr); return 0;
    case WM_TIMER: host->tick(); return 0;
    case WM_SETFOCUS:
        if (host->embedding) host->focusRequested = true;
        return 0;
    case WM_ACTIVATE:
        if (host->embedding && LOWORD(wparam) != WA_INACTIVE && !HIWORD(wparam)) host->focusRequested = true;
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_SIZE: if (host->viewport) host->layout(); return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {790, 400}; return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case Launch1: host->launch(0); break;
        case Launch2: host->launch(1); break;
        case Switch1: host->switchTo(0); break;
        case Switch2: host->switchTo(1); break;
        case Standby1:
        case Standby2:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                const int index = LOWORD(wparam) == Standby1 ? 0 : 1;
                host->standby[index] = SendMessageW(host->standbyControl[index], CB_GETCURSEL, 0, 0) == 1 ? Mode::Economy : Mode::Warm;
                host->message(L"Background choice applies to this character when inactive. Economy releases disposable render targets; measure savings on your GPU.");
            }
            break;
        case HostSurface:
            host->embedding = SendMessageW(host->embedControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            host->focusRequested = host->embedding;
            host->message(host->embedding ? L"Window-hosting experiment enabled. Use Separate windows if focus or rendering misbehaves." : L"Using separate viewer windows; warm standby remains enabled.");
            break;
        case DetachAll: host->detach(false); break;
        }
        return 0;
    case WM_CLOSE:
        if (host->closing) return 0;
        if (MessageBoxW(window, L"Close the controller and return the characters to ordinary viewer windows? They will stay logged in.",
            L"Folderstorm character sessions", MB_YESNO | MB_ICONQUESTION) == IDYES) host->detach(true);
        return 0;
    case WM_DESTROY: KillTimer(window, Timer); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    Host state;
    std::vector<wchar_t> executable(32768);
    const DWORD size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!size || size >= executable.size()) return 1;
    const auto directory = std::filesystem::path(std::wstring(executable.data(), size)).parent_path();
    std::string filename = FS_SESSION_VIEWER_FILENAME;
    std::ifstream guide(directory / L"folderstorm-session-viewer.txt", std::ios::binary);
    if (guide)
    {
        char bytes[257]{};
        guide.read(bytes, sizeof(bytes));
        if (guide.gcount() > 256 || !viewerFilename(std::string(bytes, static_cast<std::size_t>(guide.gcount())), filename))
        {
            MessageBoxW(nullptr, L"The matching viewer filename is invalid. Reinstall this controller with its viewer.", L"Folderstorm", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    guide.close();
    state.viewer = directory / wide(filename);
    wchar_t* local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) || !local) return 1;
    state.profiles = std::filesystem::path(local) / L"FolderstormSessions" / L"Prototype-v1";
    CoTaskMemFree(local);
    host = &state;
    WNDCLASSW klass{}; klass.lpfnWndProc = windowProc; klass.hInstance = instance;
    klass.lpszClassName = L"FolderstormSessionPrototype"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&klass)) return 1;
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"Folderstorm character sessions — prototype",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 980, 700, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    host = nullptr;
    return static_cast<int>(message.wParam);
}
