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
#include <shellapi.h>
#include <psapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <cmath>
#include "fssessionalerts.h"
#include "fssessionshortcuts.h"
#include <imm.h>
#include "dialog.h"
#include "resource.h"
#include "fssessionpresentation.h"
#include "fssessionusability.h"
#include "fssessionpipe.h"
#include "fssessionchatmodel.h"
#include "fssessionhostoptions.h"
#include "fssessionmonitormodel.h"
#include "fssessionmonitorchat.h"
#include "fssessionprofileseed.h"
#include "fssessionframepipe.h"
#include "fssessionrestart.h"
#include "fssessionrecovery.h"
#include "fssessionexperience.h"
#include "fssessionattentionnav.h"
#include "fssessionsets.h"
#include "theme.h"
#include "fssessionlifecycle.h"
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
constexpr int LaunchNext = 121, LaunchBase = 400, MonitorBase = 410, StandbyBase = 500;
constexpr int HostSurface = 105, DetachAll = 106;
constexpr int ChatAccount = 109, ChatConversation = 110, ChatCompose = 111, ChatSend = 112, ChatReview = 113;
constexpr int VoicePolicy = 114, BackgroundMute = 115, ChatRead = 116;
constexpr int ActiveCharacter = 117, ShowChat = 118;
constexpr int SessionOptions = 119, ViewportFocus = 120;
constexpr int ChatAccent = 122, PinActions = 123, PinBase = 600, AttentionList = 130, AttentionReview = 131, AttentionDismiss = 132;
constexpr int RestoreControls = 140, RecoverWindows = 141;
constexpr int CardBase=700, CardActionBase=710, QuickBase=730, PanelPin=165, MonitorExchange=166, MonitorChatToggle=167, MonitorOptions=168;
constexpr UINT TrayMessage = WM_APP + 20;
constexpr UINT_PTR Timer = 1;
LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK monitorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
struct ComApartment
{
    HRESULT result = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
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
// One bounded offscreen paint, including image/letterbox/footer, then present.
class PaintBuffer
{
public:
    PaintBuffer(HDC destination, int width, int height) : mDestination(destination), mWidth(width), mHeight(height)
    {
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 16u * 1024u * 1024u) return;
        mMemory = CreateCompatibleDC(destination);
        if (mMemory) mBitmap = CreateCompatibleBitmap(destination, width, height);
        if (mBitmap) mOriginal = SelectObject(mMemory, mBitmap);
    }
    ~PaintBuffer()
    {
        if (mOriginal && mOriginal != HGDI_ERROR) SelectObject(mMemory, mOriginal);
        if (mBitmap) DeleteObject(mBitmap);
        if (mMemory) DeleteDC(mMemory);
    }
    PaintBuffer(const PaintBuffer&) = delete;
    PaintBuffer& operator=(const PaintBuffer&) = delete;
    HDC dc() const { return valid() ? mMemory : mDestination; }
    void present() const { if (valid()) BitBlt(mDestination, 0, 0, mWidth, mHeight, mMemory, 0, 0, SRCCOPY); }
private:
    bool valid() const { return mMemory && mBitmap && mOriginal && mOriginal != HGDI_ERROR; }
    HDC mDestination = nullptr, mMemory = nullptr;
    HBITMAP mBitmap = nullptr;
    HGDIOBJ mOriginal = nullptr;
    int mWidth = 0, mHeight = 0;
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
// Refresh only changed choices; an open list owns its selection until close.
bool syncChoices(HWND control, const std::vector<std::wstring>& choices, int selected, bool force = false)
{
    if (!force && SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)) return false;
    bool changed = SendMessageW(control, CB_GETCOUNT, 0, 0) != static_cast<LRESULT>(choices.size());
    for (std::size_t i = 0; !changed && i < choices.size(); ++i)
    {
        const auto length = SendMessageW(control, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0);
        if (length == CB_ERR) { changed = true; break; }
        std::vector<wchar_t> text(static_cast<std::size_t>(length) + 1);
        SendMessageW(control, CB_GETLBTEXT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(text.data()));
        changed = choices[i] != text.data();
    }
    if (changed)
    {
        if (force) SendMessageW(control, CB_SHOWDROPDOWN, FALSE, 0);
        SendMessageW(control, WM_SETREDRAW, FALSE, 0);
        SendMessageW(control, CB_RESETCONTENT, 0, 0);
        for (const auto& choice : choices) SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str()));
        SendMessageW(control, WM_SETREDRAW, TRUE, 0); InvalidateRect(control, nullptr, FALSE);
    }
    if (SendMessageW(control, CB_GETCURSEL, 0, 0) != selected)
        SendMessageW(control, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    return true;
}
struct EnvLess { bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; } };
std::vector<wchar_t> environment(const std::filesystem::path& profile, HANDLE pipe, HANDLE lease, HANDLE frames, HANDLE frameMutex, const WorkerId& id, const std::filesystem::path& settingsSource)
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
    entries[L"FOLDERSTORM_SESSION_FRAMES"] = hexHandle(frames);
    entries[L"FOLDERSTORM_SESSION_FRAME_MUTEX"] = hexHandle(frameMutex);
    entries.erase(L"FOLDERSTORM_SESSION_SETTINGS_SOURCE");
    if (!settingsSource.empty()) entries[L"FOLDERSTORM_SESSION_SETTINGS_SOURCE"] = settingsSource.wstring();
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
    Restart restart;
    PeriodicPoller periodic;
    FrameLane preview;
    std::uint32_t previewWidth = 0, previewHeight = 0, previewRate = 0;
    ChatView chat;
    Message pendingSend, pendingTyping;
    Message pendingAction;
    Message pendingSkip;
    bool hasPendingSkip = false;
    Message pendingReview;
    bool hasPendingReview = false;
    Message closingAction;
    bool hasPendingAction = false;
    std::string workspace;
    bool workspaceModified = false, preferencesOpen = false;
    ULONGLONG workspaceAt = 0;
    bool hasPendingSend = false, hasPendingTyping = false;
    std::uint32_t audioPolicy = 2, catalogIndex = 0;
    int shortcutPolicy = -1;
    ULONGLONG eventsAt = 0, catalogAt = 0, typingAt = 0;
    Pipe pipe;
    Handle process;
    WorkerId id{};
    DWORD pid = 0;
    std::uint64_t nextSequence = 0;
    ULONGLONG sentAt = 0, pollAt = 0, sampleAt = 0;
    Message snapshot, request, sample;
    std::string reservation;
    bool waiting = false, detached = false, importReported = false;
    double fps = 0, loops = 0;
    bool running() const { return process.value != INVALID_HANDLE_VALUE && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT; }
    HWND surface() const
    {
        HWND result = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(snapshot.surface));
        DWORD owner = 0;
        return result && IsWindow(result) && GetWindowThreadProcessId(result, &owner) && owner == pid ? result : nullptr;
    }
};
struct MonitorWindow
{
    HWND window = nullptr;
    UINT dpi = 96;
    bool dirty = true;
    std::wstring title,previewStatus=L"Waiting for first frame";
    Message frame;
    std::vector<std::uint8_t> pixels;
    bool showChat = false, updatingChat = false;
    MonitorChat chat;
    HWND label = nullptr, choices = nullptr, history = nullptr, compose = nullptr, send = nullptr, read = nullptr;
    HWND exchange=nullptr,chatToggle=nullptr,pin=nullptr,options=nullptr;
    HFONT font = nullptr;
    std::vector<std::string> conversationIds;
    std::wstring chatStatus;
    std::string historyConversation;
    std::uint64_t historyCursor = 0;
    std::size_t historyLines = 0;
    bool historyVisible = false;
    COLORREF color = 0;
};
struct Host
{
    HWND window = nullptr, viewport = nullptr, status[MaxCharacters]{}, notice = nullptr, embedControl = nullptr;
    HWND launchButton = nullptr;
    HWND standbyControl[MaxCharacters]{};
    HWND characterControl = nullptr, showChatControl = nullptr, detachControl = nullptr;
    HWND optionsControl = nullptr, compactInfo = nullptr, revealControl = nullptr;
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    bool trayPresent = false;
    HostExperience experience; fs_host_ui::Theme theme;
    HWND viewMenu=nullptr,settingsMenu=nullptr,attentionButton=nullptr,nextButton=nullptr,restartButton=nullptr;
    HWND cardButtons[MaxCharacters]{},cardActions[MaxCharacters]{},quickButtons[12]{};
    bool commandOverflow=false;
    std::uint64_t layoutState=0;
    AttentionNavigator attentionNavigation;
    SessionSetStore setStore;SessionSetOpen setOpen;
    std::map<std::tuple<std::string,std::string,std::string>,std::uint64_t> unreadSince;
    unsigned int chrome = 0;
    HWND chatWindow = nullptr, controlsWindow = nullptr, tooltips = nullptr;
    bool chatDetached = false, controlsDetached = false;
    UINT chatDpi = 96, controlsDpi = 96, attentionDpi = 96;
    HFONT chatFont = nullptr, controlsFont = nullptr, attentionFont = nullptr;
    std::wstring descriptions[MaxCharacters], lastNotice,chatSenderTip;
    Handle optionsLease;
    bool logoutOnClose = false;
    std::uintptr_t focusIntent = 0;
    MonitorSet monitorSet;
    std::array<MonitorWindow, MaxMonitors> monitors{};
    unsigned int monitorSize = 0, monitorRate = 1; // Saved defaults, not assignments.
    bool showChat = true;
    PresentationStore presentation;
    PinBook pins;
    AttentionBook attention;
    AlertGate alertGate;
    std::vector<std::uint8_t> alertTone;
    HWND pinMenu = nullptr, pinButtons[6]{}, attentionWindow = nullptr, attentionList = nullptr;
    std::vector<Message> attentionRows;
    std::vector<std::wstring> attentionLabels;
    std::wstring pinLabels[6], pinTips[6];
    bool menuOpen = false;

    HWND chatAccent = nullptr;
    int accentSlot = -1;
    COLORREF accentColor = 0;
    unsigned int handoffStyle = 0, handoffDuration = 1100, handoffHeight = 64;
    bool cinematic = false, handoffAnimated = false, escapeHeld = false;
    UINT dpi = 96;
    HFONT font = nullptr;
    HWND chatAccount = nullptr, chatConversation = nullptr, chatHistory = nullptr, chatCompose = nullptr;
    HWND chatSend = nullptr, chatReview = nullptr, chatRead = nullptr, voiceControl = nullptr, muteControl = nullptr, chatLabel = nullptr;
    int chatIndex = 0;
    std::string conversation;
    std::vector<std::string> conversationIds;
    bool updatingChat = false, voice = false, muteBackground = true;
    std::array<Mode, MaxCharacters> standby = warmModes();
    std::unique_ptr<Slot> slots[MaxCharacters];
    Handoff handoff;
    HostLifecycle lifecycle;
    int active = -1;
    bool embedding = false, detaching = false, closing = false, selectFirst = true, focusRequested = false;
    std::filesystem::path viewer, profiles;
    ~Host() { PlaySoundW(nullptr,nullptr,0); if (font) DeleteObject(font); if (chatFont) DeleteObject(chatFont); if (controlsFont) DeleteObject(controlsFont); if (attentionFont) DeleteObject(attentionFont); for (auto& monitor : monitors) if (monitor.font) DeleteObject(monitor.font); }

    bool saveStore(const wchar_t* filename,const std::string& data)
    {
        if(data.empty())return false;
        const auto target=profiles/filename;const auto temporary=target.wstring()+L".tmp";
        std::ofstream out(std::filesystem::path(temporary),std::ios::binary|std::ios::trunc);out.write(data.data(),static_cast<std::streamsize>(data.size()));out.close();
        return out && MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    void applyTheme()
    {
        theme.set(experience.theme);fs_host_ui::dialogTheme=&theme;
        for(HWND panel:{window,chatWindow,controlsWindow,attentionWindow})theme.window(panel);
        for(auto& m:monitors)if(m.window){theme.window(m.window);m.dirty=true;}
    }
    std::wstring quickLabel(unsigned int id) const
    {
        const auto* c=shellCommand(id);if(!c)return L"";
        return experience.labels==2 ? c->icon : experience.labels==1 ? c->label : std::wstring(c->icon)+L" "+c->label;
    }
    void editHostSettings(unsigned int page=0)
    {
        using namespace fs_host_ui;
        std::vector<Field> fields{
            {L"Theme",std::to_wstring(experience.theme),FieldType::Choice,{L"Folderstorm Midnight",L"Folderstorm Daylight",L"Follow Windows"}},
            {L"Control density",std::to_wstring(experience.density),FieldType::Choice,{L"Compact",L"Comfortable"}},
            {L"Character cards",experience.cards?L"1":L"0",FieldType::Check},
            {L"Quick button labels",std::to_wstring(experience.labels),FieldType::Choice,{L"Icon and text",L"Text",L"Icon (tooltips)"}},
            {L"Show diagnostics",experience.diagnostics?L"1":L"0",FieldType::Check},
            {L"Reset appearance",L"0",FieldType::Check},
            {L"Voice follows active",voice?L"1":L"0",FieldType::Check},
            {L"Mute background sound",muteBackground?L"1":L"0",FieldType::Check},
            {L"Alert volume (0–100)",std::to_wstring(presentation.volume),FieldType::Number,{},0,100},
            {L"Transition",std::to_wstring(presentation.transition),FieldType::Choice,{L"Instant",L"Bird's-eye",L"Fade"}},
            {L"Duration (250–2000 ms)",std::to_wstring(presentation.duration),FieldType::Number,{},250,2000},
            {L"Height (16–96 m)",std::to_wstring(presentation.height),FieldType::Number,{},16,96}};
        for(std::size_t i=6;i<9;++i)fields[i].page=1;for(std::size_t i=9;i<fields.size();++i)fields[i].page=2;
        (void)page;
        if(!edit(window,L"Host settings · Apply for this session; Save host choices to keep",fields,{L"Appearance",L"Audio",L"Switching"}))return;
        auto next=experience;next.theme=number(fields[0]);next.density=number(fields[1]);next.cards=number(fields[2])!=0;next.labels=number(fields[3]);next.diagnostics=number(fields[4])!=0;
        if(number(fields[5])){next.theme=0;next.density=0;next.cards=true;next.labels=0;next.diagnostics=false;}
        if(!next.valid())return;
        experience=std::move(next);voice=number(fields[6])!=0;muteBackground=number(fields[7])!=0;
        presentation.volume=number(fields[8]);presentation.transition=number(fields[9]);presentation.duration=number(fields[10]);presentation.height=number(fields[11]);
        SendMessageW(voiceControl,BM_SETCHECK,voice?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(muteControl,BM_SETCHECK,muteBackground?BST_CHECKED:BST_UNCHECKED,0);
        applyTheme();layout();message(L"Host settings applied for this session. Save host choices to keep them.");
    }
    void customizeQuick()
    {
        using namespace fs_host_ui;
        std::vector<std::wstring> labels{L"Unused"};for(const auto& c:shellCommands())labels.push_back(c.label);
        std::vector<Field> fields;
        for(std::size_t i=0;i<12;++i)
        {
            unsigned int choice=0;if(i<experience.quick.size())for(std::size_t j=0;j<shellCommands().size();++j)if(shellCommands()[j].id==experience.quick[i])choice=static_cast<unsigned int>(j)+1;
            Field field{L"Position "+std::to_wstring(i+1),std::to_wstring(choice),FieldType::Choice,labels};field.page=i<6?0u:1u;fields.push_back(std::move(field));
        }
        Field reset{L"Restore default quick actions",L"0",FieldType::Check};reset.page=1;fields.push_back(reset);
        if(!edit(window,L"Quick actions · choose their order",fields,{L"Positions 1–6",L"Positions 7–12"}))return;
        auto next=experience;next.quick.clear();
        for(std::size_t i=0;i<12;++i)if(number(fields[i]))next.quick.push_back(shellCommands()[number(fields[i])-1].id);
        if(number(fields[12]))next.quick.clear();
        if(!next.valid()){message(L"Choose each quick action once; changes were not applied.");return;}
        experience=std::move(next);layout();applyTheme();
    }
    unsigned int windowKey(HWND panel) const
    {
        if(panel==chatWindow)return 1;if(panel==controlsWindow)return 2;if(panel==attentionWindow)return 7;
        for(unsigned int i=0;i<MaxMonitors;++i)if(panel==monitors[i].window)return i+3;
        return 0;
    }
    void pinWindow(HWND panel,bool toggle=true)
    {
        const auto id=windowKey(panel);if(!id)return;
        if(toggle)experience.pinned[id]=!experience.pinned[id];
        SetWindowPos(panel,experience.pinned[id]?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
        CheckMenuItem(GetSystemMenu(panel,FALSE),0xA160,MF_BYCOMMAND|(experience.pinned[id]?MF_CHECKED:MF_UNCHECKED));
        HWND pin=GetDlgItem(panel,PanelPin);if(pin)SetWindowTextW(pin,experience.pinned[id]?L"Unpin":L"Pin");
    }
    void installPanelMenuOnce(HWND panel){if(!GetPropW(panel,L"FolderstormPanelMenu")){installPanelMenu(panel);SetPropW(panel,L"FolderstormPanelMenu",reinterpret_cast<HANDLE>(1));}}
    void installPanelMenu(HWND panel)
    {
        SetPropW(panel,L"FolderstormPanelMenu",reinterpret_cast<HANDLE>(1));recoveryMenu(panel);AppendMenuW(GetSystemMenu(panel,FALSE),MF_STRING,0xA160,L"Always on top");
        pinWindow(panel,false);theme.window(panel);
    }
    std::string utf8(const std::wstring& value) const
    {
        const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        if(n<=0)return {};std::string result(static_cast<std::size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);return result;
    }
    bool persistSets(const SessionSetStore& next)
    {
        if(!saveStore(L"session-sets.dat",next.encode())){message(L"Session sets could not be saved; the previous file and live sessions were retained.");return false;}
        setStore=next;return true;
    }
    ShellRect liveRect(HWND panel,UINT local)
    {
        if(!panel)return {};WINDOWPLACEMENT placement{};placement.length=sizeof(placement);if(!GetWindowPlacement(panel,&placement))return {};
        auto r=placement.rcNormalPosition;
        if(!(GetWindowLongPtrW(panel,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)){MONITORINFO m{};m.cbSize=sizeof(m);if(GetMonitorInfoW(MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST),&m))OffsetRect(&r,m.rcWork.left-m.rcMonitor.left,m.rcWork.top-m.rcMonitor.top);}
        return {static_cast<int>(r.left),static_cast<int>(r.top),static_cast<int>(r.right-r.left),static_cast<int>(r.bottom-r.top),local};
    }
    void fitSetRect(HWND panel,const ShellRect& r,UINT local)
    {
        if(!panel || !r.valid())return;RECT target{r.x,r.y,r.x+r.width,r.y+r.height};MONITORINFO m{};m.cbSize=sizeof(m);
        if(!GetMonitorInfoW(MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST),&m))return;const auto& w=m.rcWork;
        const auto fit=fitShellRect(r,{static_cast<int>(w.left),static_cast<int>(w.top),static_cast<int>(w.right-w.left),static_cast<int>(w.bottom-w.top),96},local);
        if(fit.valid())SetWindowPos(panel,nullptr,fit.x,fit.y,fit.width,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void captureSessionSet(const SessionSet* current=nullptr)
    {
        using namespace fs_host_ui;
        std::vector<Field> fields{{L"Set name",current?wide(current->name):L"New session set"},
            {L"Capture selected profiles",!current || current->captureProfiles?L"1":L"0",FieldType::Check},
            {L"Capture shell arrangement",!current || current->captureLayout?L"1":L"0",FieldType::Check},
            {L"Reopen saved monitors",current && current->captureMonitors?L"1":L"0",FieldType::Check}};
        for(unsigned int i=0;i<MaxCharacters;++i)
        {
            const bool selected=current?std::any_of(current->profiles.begin(),current->profiles.end(),[i](const SetProfile& p){return p.slot==i;}):slots[i] && slots[i]->running();
            Field f{characterName(static_cast<int>(i)),selected?L"1":L"0",FieldType::Check};f.page=1;fields.push_back(f);
        }
        if(!edit(window,L"Save session set · profiles remain separate; no login credentials copied",fields,{L"Capture options",L"Profiles"}))return;
        SessionSet next;next.name=utf8(fields[0].value);next.captureProfiles=number(fields[1])!=0;next.captureLayout=number(fields[2])!=0;next.captureMonitors=number(fields[3])!=0;
        next.chrome=chrome;next.hosted=embedding;next.chat=showChat;next.chatDetached=chatDetached;next.controlsDetached=controlsDetached;next.order=experience.order;
        if(next.captureProfiles)for(auto i:experience.order)if(number(fields[i+4]))
        {
            SetProfile profile;profile.slot=i;profile.standby=standby[i];
            if(slots[i] && slots[i]->snapshot.state==State::Ready)profile.expected=AccountKey::from(slots[i]->snapshot);
            else if(current)for(const auto& old:current->profiles)if(old.slot==i)profile.expected=old.expected;
            std::error_code error;if(!std::filesystem::is_directory(profiles/(L"Character"+std::to_wstring(i+1)),error) || error){message(L"Selected profile does not exist yet. Open its login once before saving it in a set.");return;}
            next.profiles.push_back(profile);
        }
        if(next.captureLayout)for(unsigned int i=0;i<3;++i){const auto r=liveRect(i==0?window:i==1?chatWindow:controlsWindow,i==0?dpi:i==1?chatDpi:controlsDpi);if(r.valid())next.rectangles[i]=r;}
        if(next.captureMonitors)for(int id=0;id<MaxMonitors;++id)
        {
            const auto& b=monitorSet.bindings[id];if(!b.enabled || !validSlot(b.slot) || !slots[b.slot] || !b.key.owns(slots[b.slot]->snapshot) || (next.captureProfiles && !number(fields[static_cast<std::size_t>(b.slot)+4])))continue;
            next.monitors.push_back({static_cast<unsigned int>(b.slot),b.size,b.rate,monitors[id].showChat,AccountKey::from(slots[b.slot]->snapshot),liveRect(monitors[id].window,monitors[id].dpi)});
        }
        if(!next.valid()){message(L"Session-set name or capture choices are invalid (name: up to 64 UTF-8 bytes).");return;}
        auto store=setStore;
        const bool replace=store.sets.count(next.name)!=0;
        if(replace && MessageBoxW(window,(L"Replace session set “"+wide(next.name)+L"”? Profiles and viewers will not be deleted.").c_str(),L"Update session set",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
        if(current && current->name!=next.name)store.sets.erase(current->name);
        if(!store.put(next,replace)){message(L"Session-set capacity or capture validation failed.");return;}
        if(persistSets(store))message(L"Session set saved. Use Open to explicitly launch its normal login windows.");
    }
    void startSessionSet(const SessionSet& selected)
    {
        if(busy() || closing || detaching){message(L"Finish switching or stopping before opening a session set.");return;}
        const bool replaced=setOpen.active;
        setOpen.cancel(); // Revoke the previous operation before a modal loop can poll it.
        const auto confirmation=setOpen.token;
        if(replaced)message(L"Previous set opening cancelled. Already opened login windows remain available.");
        std::wstring summary=L"Open “"+wide(selected.name)+L"”? Missing profiles open normal login; already-running characters stay logged in.\n";
        for(const auto& p:selected.profiles)
        {
            summary+=L"\nCharacter "+std::to_wstring(p.slot+1)+L": ";
            const auto* slot=slots[p.slot].get();summary+=!slot || !slot->running()?L"open login":slot->detached || !slot->pipe.alive()?L"unavailable controller; skip":slot->snapshot.state==State::Ready && !p.expected.account.empty() && !(p.expected==AccountKey::from(slot->snapshot))?L"different account; skip":L"reuse current profile";
        }
        summary+=selected.captureLayout?L"\n\nApply saved shell arrangement.":L"\n\nKeep current shell arrangement.";
        summary+=selected.captureMonitors?L"\nOpen saved monitors only after their expected accounts are ready.":L"\nNo monitor opening.";
        if(MessageBoxW(window,summary.c_str(),L"Open logins and apply session set",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
        if(setOpen.token!=confirmation || busy() || closing || detaching)
        {message(L"This set confirmation expired; no pending opening was started.");return;}
        setOpen.begin(selected,GetTickCount64());
        for(const auto& m:selected.monitors)if(slots[m.slot] && slots[m.slot]->running()){setOpen.workers[m.slot]=slots[m.slot]->id;setOpen.generations[m.slot]=slots[m.slot]->snapshot.state==State::Ready?slots[m.slot]->snapshot.generation:0;}
        message(L"Opening the set's normal login windows. Session sets → Cancel opening stops remaining work.");
    }
    void sessionSetsMenu()
    {
        using namespace fs_host_ui;
        std::vector<std::wstring> names;std::vector<std::string> ids;for(const auto& entry:setStore.sets){ids.push_back(entry.first);names.push_back(wide(entry.first));}
        if(names.empty())names.push_back(L"No saved sets");
        std::vector<Field> fields{{L"Action",L"0",FieldType::Choice,{L"Open selected set",L"Save current as…",L"Update selected set…",L"Rename selected set…",L"Duplicate selected set…",L"Delete selected set…",L"Cancel pending opening"}},
            {L"Saved set",L"0",FieldType::Choice,names}};
        if(!edit(window,L"Session sets · Save/Open/Update/Manage",fields))return;
        const auto action=number(fields[0]);if(action==1){captureSessionSet();return;}if(action==6){setOpen.cancel();message(L"Set opening cancelled. Already opened login windows remain available.");return;}
        if(ids.empty())return;const auto found=setStore.sets.find(ids[number(fields[1])]);if(found==setStore.sets.end())return;const auto selected=found->second;
        if(action==0){startSessionSet(selected);return;}if(action==2){captureSessionSet(&selected);return;}
        auto store=setStore;
        if(action==5){if(MessageBoxW(window,L"Delete this set? Its profiles and running viewers remain untouched.",L"Delete session set",MB_OKCANCEL)!=IDOK)return;store.sets.erase(selected.name);}
        else
        {
            std::vector<Field> rename{{L"Set name",wide(selected.name)+(action==4?L" copy":L"")}};if(!edit(window,action==4?L"Duplicate session set":L"Rename session set",rename))return;
            auto next=selected;next.name=utf8(rename[0].value);
            if(store.sets.count(next.name) && next.name!=selected.name){message(L"A set already has that name. Choose another name.");return;}
            if(action==3)store.sets.erase(selected.name);
            if(!store.put(next,false)){message(L"Name or session-set capacity is invalid.");return;}
        }
        persistSets(store);
    }
    void pollSessionSet()
    {
        if(!setOpen.active)return;const auto token=setOpen.token;const auto now=GetTickCount64();
        if(!setOpen.owns(token,now) || closing || detaching){setOpen.cancel();message(L"Session-set opening ended; pending login/monitor actions were discarded.");return;}
        if(busy())return;
        const auto skip=[this](unsigned int i,const char* why){setOpen.skip(i,why);};
        for(const auto& p:setOpen.selected.profiles)
        {
            auto* slot=slots[p.slot].get();
            if(setOpen.attempted[p.slot] && setOpen.sourceChanged(p.slot,slot?slot->id:WorkerId{},slot?slot->snapshot.generation:0,slot && slot->running() && !slot->detached && slot->pipe.alive()))
            {setOpen.cancel();message(L"A session-set source ended or changed; remaining actions were cancelled.");return;}
            if(setOpen.finished[p.slot])continue;
            if(!setOpen.attempted[p.slot])
            {
                setOpen.attempted[p.slot]=true;
                if(slot && slot->running() && (slot->detached || !slot->pipe.alive()))
                {skip(p.slot,"existing viewer has no managed controller");continue;}
                if(!slot || !slot->running())
                {
                    std::error_code error;if(!std::filesystem::is_directory(profiles/(L"Character"+std::to_wstring(p.slot+1)),error) || error){skip(p.slot,"profile missing");continue;}
                    if(!setOpen.owns(token,GetTickCount64()))return;
                    launch(static_cast<int>(p.slot));slot=slots[p.slot].get();
                    if(!slot || !slot->running()){skip(p.slot,"login could not open; profile may be locked");continue;}
                    setOpen.workers[p.slot]=slot->id;return; // At most one launch in a timer pass.
                }
                setOpen.workers[p.slot]=slot->id;
                if(slot->snapshot.state==State::Ready)setOpen.generations[p.slot]=slot->snapshot.generation;
            }
            if(!slot || !slot->running() || slot->detached || !slot->pipe.alive()){skip(p.slot,"viewer closed or control unavailable");continue;}
            if(slot->id!=setOpen.workers[p.slot] || (setOpen.generations[p.slot] && slot->snapshot.generation!=setOpen.generations[p.slot])){setOpen.cancel();message(L"A set profile's session was replaced; remaining actions were cancelled.");return;}
            if(slot->snapshot.state==State::Disconnected){skip(p.slot,"disconnected; restart manually");continue;}
            if(slot->snapshot.state!=State::Ready)continue;
            if(!setOpen.accept(p,slot->snapshot)){skip(p.slot,"expected account/grid did not log in");continue;}
            if(!setOpen.owns(token,GetTickCount64()))return;
            setOpen.generations[p.slot]=slot->snapshot.generation;standby[p.slot]=p.standby;SendMessageW(standbyControl[p.slot],CB_SETCURSEL,p.standby==Mode::Economy?1:0,0);setOpen.finished[p.slot]=true;
        }
        if(setOpen.selected.captureLayout && !setOpen.layoutApplied)
        {
            if(!setOpen.owns(token,GetTickCount64()))return;setOpen.layoutApplied=true;const auto& v=setOpen.selected;
            embedding=v.hosted;SendMessageW(embedControl,BM_SETCHECK,embedding?BST_CHECKED:BST_UNCHECKED,0);showChat=v.chat;SendMessageW(showChatControl,BM_SETCHECK,showChat?BST_CHECKED:BST_UNCHECKED,0);
            experience.order=v.order;setPanelDetached(true,v.chatDetached,false);setPanelDetached(false,v.controlsDetached,false);setChrome(v.chrome);
            for(const auto& r:v.rectangles)fitSetRect(r.first==0?window:r.first==1?chatWindow:controlsWindow,r.second,r.first==0?dpi:r.first==1?chatDpi:controlsDpi);layout();
        }
        bool complete=true;for(const auto& p:setOpen.selected.profiles)if(!setOpen.finished[p.slot])complete=false;
        for(std::size_t n=0;n<setOpen.selected.monitors.size();++n)
        {
            if(setOpen.monitorDone[n])continue;const auto& m=setOpen.selected.monitors[n];auto* slot=slots[m.slot].get();
            if(!slot || !slot->running() || slot->detached || !slot->pipe.alive()){setOpen.monitorDone[n]=true;setOpen.skipped.push_back("Monitor: profile unavailable");continue;}
            if(slot->snapshot.state==State::Disconnected){setOpen.monitorDone[n]=true;setOpen.skipped.push_back("Monitor: disconnected; restart manually");continue;}
            if(slot->snapshot.state!=State::Ready){complete=false;continue;}
            setOpen.monitorDone[n]=true;
            if((setOpen.workers[m.slot]!=WorkerId{} && (slot->id!=setOpen.workers[m.slot] || (setOpen.generations[m.slot] && slot->snapshot.generation!=setOpen.generations[m.slot]))) || !(m.expected==AccountKey::from(slot->snapshot)) || (slot->snapshot.flags&ChatRestricted) || static_cast<int>(m.slot)==active){setOpen.skipped.push_back("Monitor: expected account unavailable, restricted or active");continue;}
            if(!setOpen.owns(token,GetTickCount64()))return;
            const unsigned int previousSize=monitorSize,previousRate=monitorRate;monitorSize=m.size;monitorRate=m.rate;openMonitor(static_cast<int>(m.slot));monitorSize=previousSize;monitorRate=previousRate;
            const int id=monitorSet.find(static_cast<int>(m.slot));if(id<0){setOpen.skipped.push_back("Monitor: no available preview slot");continue;}setMonitorChat(id,m.chat);fitSetRect(monitors[id].window,m.rect,monitors[id].dpi);
        }
        if(complete)
        {
            const auto skipped=setOpen.skipped;setOpen.cancel();message(skipped.empty()?L"Session set ready.":L"Session set completed with "+std::to_wstring(skipped.size())+L" skipped entries.");
            if(!skipped.empty()){std::wstring details;for(const auto& why:skipped)details+=wide(why)+L"\n";MessageBoxW(window,details.c_str(),L"Session-set restore report",MB_OK|MB_ICONINFORMATION);}
        }
    }
    void alert(const Message& item,int index)
    {
        if (!presentation.volume || !slots[index]) return;
        const bool eligible = slots[index]->snapshot.state == State::Ready && !slots[index]->detached && slots[index]->snapshot.mode != Mode::Active &&
            muteBackground && !(slots[index]->snapshot.flags & (ChatRestricted|Promoting|Error));
        if (!alertGate.admit(item,appearance(index).alerts,GetTickCount64(),eligible)) return;
        constexpr std::uint32_t samples = 3528, rate = 22050;
        PlaySoundW(nullptr,nullptr,0); // End any owned playback before replacing its backing buffer.
        alertTone.assign(44+samples*2,0);
        const auto number = [this](std::size_t offset,std::uint32_t value,unsigned int size)
        { for (unsigned int i = 0; i < size; ++i) alertTone[offset+i] = static_cast<std::uint8_t>((value>>(8*i))&255); };
        const auto text = [this](std::size_t offset,const char* value) { for (int i = 0; i < 4; ++i) alertTone[offset+static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value[i]); };
        text(0,"RIFF"); number(4,36+samples*2,4); text(8,"WAVE"); text(12,"fmt "); number(16,16,4); number(20,1,2); number(22,1,2);
        number(24,rate,4); number(28,rate*2,4); number(32,2,2); number(34,16,2); text(36,"data"); number(40,samples*2,4);
        for (std::uint32_t i = 0; i < samples; ++i)
        {
            const double envelope = std::sin(3.141592653589793*static_cast<double>(i)/samples);
            const auto sample = static_cast<std::int16_t>(std::sin(6.283185307179586*880.0*static_cast<double>(i)/rate)*envelope*6000.0*presentation.volume/100.0);
            number(44+static_cast<std::size_t>(i)*2,static_cast<std::uint16_t>(sample),2);
        }
        PlaySoundW(reinterpret_cast<LPCWSTR>(alertTone.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);
    }
    unsigned int shortcutPolicy() const {return (presentation.shortcuts?1u:0u)|(experience.viewerShellKeys?2u:0u);}
    void dispatchShortcut(unsigned int command)
    {
        if (!presentation.shortcuts || busy()) return; // Drop during handoff; never replay a queued key.
        const int target = characterShortcutTarget(command,active,[this](int i)
        { return validSlot(i) && slots[i] && slots[i]->running() && !slots[i]->detached && slots[i]->pipe.alive() && slots[i]->snapshot.state == State::Ready; });
        if (target < 0) { message(L"That character shortcut has no ready target."); return; }
        switchTo(target);
    }
    bool hostShortcut(const MSG& event)
    {
        if (menuOpen || (event.message != WM_KEYDOWN && event.message != WM_SYSKEYDOWN) || (event.lParam & (1LL<<30))) return false;
        const HWND focus = GetFocus(); if (!focus) return false;
        const HWND root = GetAncestor(focus,GA_ROOT);
        if (root != window && root != chatWindow && root != controlsWindow && root != attentionWindow && !isMonitorRoot(root)) return false;
        wchar_t klass[32]{}; GetClassNameW(focus,klass,32);
        if (_wcsicmp(klass,L"EDIT") == 0 || (_wcsicmp(klass,L"COMBOBOX") == 0 && SendMessageW(focus,CB_GETDROPPEDSTATE,0,0))) return false;
        const HIMC context = ImmGetContext(focus);
        const bool composing = context && ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0) > 0;
        if (context) ImmReleaseContext(focus,context); if (composing) return false;
        const unsigned int modifiers = ((GetKeyState(VK_MENU)&0x8000) ? 1u : 0u) | ((GetKeyState(VK_CONTROL)&0x8000) ? 2u : 0u) | ((GetKeyState(VK_SHIFT)&0x8000) ? 4u : 0u);
        for(std::size_t i=0;i<experience.keys.size();++i)if(experience.keys[i].key==event.wParam && experience.keys[i].modifiers==modifiers)
        {if(i==0)recover();else navigateAttention(i==1?1:-1);return true;}
        if(!presentation.shortcuts)return false;
        for (std::size_t i = 0; i < presentation.bindings.size(); ++i) if (presentation.bindings[i].key == event.wParam && presentation.bindings[i].modifiers == modifiers)
        { dispatchShortcut(static_cast<unsigned int>(i)); return true; }
        return false;
    }
    bool isMonitorRoot(HWND root) const {for(const auto& m:monitors)if(m.window && m.window==root)return true;return false;}
    void editShortcuts()
    {
        using namespace fs_host_ui;
        const unsigned int modifiers[] = {5,6,3,7};
        const std::vector<unsigned int> keys{0,49,50,51,52,53,39,37,112,113,114,115,116,117,118,119,120,121,122,123};
        const std::vector<std::wstring> labels{L"Disabled",L"1",L"2",L"3",L"4",L"5",L"Right",L"Left",L"F1",L"F2",L"F3",L"F4",L"F5",L"F6",L"F7",L"F8",L"F9",L"F10",L"F11",L"F12"};
        unsigned int modifier = 0; for (unsigned int i = 0; i < 4; ++i) if (presentation.bindings[0].modifiers == modifiers[i]) modifier = i;
        std::vector<Field> fields{{L"Enable character shortcuts",presentation.shortcuts ? L"1" : L"0",FieldType::Check},
            {L"Host modifier",std::to_wstring(modifier),FieldType::Choice,{L"Alt + Shift",L"Ctrl + Shift",L"Ctrl + Alt",L"Ctrl + Alt + Shift"}}};
        for (unsigned int i = 0; i < 7; ++i)
        {
            const auto found = std::find(keys.begin(),keys.end(),presentation.bindings[i].key);
            const auto selected = found == keys.end() ? 0 : static_cast<unsigned int>(found-keys.begin());
            fields.push_back({i < 5 ? L"Host: character "+std::to_wstring(i+1) : i == 5 ? L"Host: next ready" : L"Host: previous ready",std::to_wstring(selected),FieldType::Choice,labels});
        }
        for(unsigned int i=0;i<3;++i)
        {const auto found=std::find(keys.begin(),keys.end(),experience.keys[i].key);Field field{i==0?L"Host: restore controls":i==1?L"Host: next attention":L"Host: previous attention",std::to_wstring(found==keys.end()?0:static_cast<unsigned int>(found-keys.begin())),FieldType::Choice,labels};field.page=1;fields.push_back(std::move(field));}
        for(std::size_t i=6;i<9;++i)fields[i].page=1;
        Field viewerKeys{L"Enable viewer shell actions",experience.viewerShellKeys?L"1":L"0",FieldType::Check};viewerKeys.page=1;fields.push_back(viewerKeys);
        if (!edit(window,L"Host keys; viewer keys: Preferences → Controls",fields,{L"Character keys",L"Cycle and shell keys"})) return;
        auto draft = presentation; draft.shortcuts = number(fields[0]);
        for (unsigned int i = 0; i < 7; ++i) draft.bindings[i] = {keys[number(fields[i+2])],modifiers[number(fields[1])]};
        if (!draft.valid()) { message(L"Two character actions use the same host shortcut; choices were not applied."); return; }
        auto next=experience;next.viewerShellKeys=number(fields[12])!=0;for(unsigned int i=0;i<3;++i)next.keys[i]={keys[number(fields[i+9])],modifiers[number(fields[1])]};
        if(!next.valid() || bindingsConflict(next,draft)){message(L"Host shell keys conflict; changes were not applied.");return;}
        experience=std::move(next);presentation = std::move(draft);
        message(L"Host shortcuts applied. Assign viewer Character actions through Preferences > Controls; typing is excluded. Save host choices to keep.");
    }

    void editTransition()
    {
        using namespace fs_host_ui;
        std::vector<Field> fields{{L"Transition style",std::to_wstring(presentation.transition),FieldType::Choice,{L"Instant",L"Bird's-eye",L"Fade"}},
            {L"Duration per leg (250–2000 ms)",std::to_wstring(presentation.duration),FieldType::Number,{},250,2000},
            {L"Height above avatar (16–96 m)",std::to_wstring(presentation.height),FieldType::Number,{},16,96},
            {L"Reset transition defaults",L"0",FieldType::Check}};
        if (!edit(window,L"Character transition (Save host choices to keep)",fields)) return;
        if (number(fields[3])) { presentation.transition = 0; presentation.duration = 1100; presentation.height = 64; }
        else { presentation.transition = number(fields[0]); presentation.duration = number(fields[1]); presentation.height = number(fields[2]); }
        message(L"Transition choices apply to the next switch. Escape skips the effect and completes switching.");
    }
    void editAlertVolume()
    {
        using namespace fs_host_ui;
        std::vector<Field> fields{{L"Alert volume (0–100)",std::to_wstring(presentation.volume),FieldType::Number,{},0,100}};
        if (edit(window,L"Host alert volume (Save host choices to keep)",fields)) presentation.volume = number(fields[0]);
    }

    int sourceSlot(const Message& item) const
    {
        for (int i = 0; i < MaxCharacters; ++i) if (slots[i] && slots[i]->chat.identity.owns(item)) return i;
        return -1;
    }
    int pinSlot(const ChatPin& pin) const
    {
        for (int i = 0; i < MaxCharacters; ++i) if (slots[i] && slots[i]->running() && !slots[i]->detached && slots[i]->pipe.alive() && slots[i]->snapshot.state == State::Ready &&
            !(slots[i]->snapshot.flags & ChatRestricted) && pin.live(slots[i]->chat)) return i;
        return -1;
    }
    void selectPin(std::size_t id)
    {
        if (id >= pins.entries.size()) return;
        const auto& pin = pins.entries[id]; const int index = pinSlot(pin);
        if (index < 0) { message(L"Pinned conversation is unavailable. Open it in its owning viewer."); return; }
        saveDraft(true); chatIndex = index; conversation = pin.conversation; refreshChat(true,true);
        showChat = true; SendMessageW(showChatControl,BM_SETCHECK,BST_CHECKED,0); layout(); SetFocus(chatCompose);
    }
    void pinActions()
    {
        const int selected = slots[chatIndex] ? pins.find(slots[chatIndex]->chat,conversation) : -1;
        const auto source = boundChat(Kind::Poll);
        HMENU menu = CreatePopupMenu(); if (!menu) return;
        AppendMenuW(menu,MF_STRING,1,selected < 0 ? L"Pin current conversation" : L"Unpin current conversation");
        AppendMenuW(menu,MF_STRING | (selected > 0 ? 0 : MF_GRAYED),2,L"Move current pin left");
        AppendMenuW(menu,MF_STRING | (selected >= 0 && static_cast<std::size_t>(selected+1) < pins.entries.size() ? 0 : MF_GRAYED),3,L"Move current pin right");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        HMENU remove = CreatePopupMenu(), left = CreatePopupMenu(), right = CreatePopupMenu();
        for (std::size_t i = 0; i < pins.entries.size(); ++i)
        {
            const auto& pin = pins.entries[i]; const int owner = pinSlot(pin);
            const auto name = owner >= 0 ? characterName(owner) : wide(presentation.appearance(pin.owner).alias.empty() ? pin.owner.account : presentation.appearance(pin.owner).alias);
            const auto text = name + L" · " + wide(pin.title) + (owner < 0 ? L" · unavailable" : L"");
            AppendMenuW(menu,MF_STRING,100+static_cast<UINT>(i),text.c_str());
            AppendMenuW(remove,MF_STRING,200+static_cast<UINT>(i),text.c_str());
            AppendMenuW(left,MF_STRING|(i > 0 ? 0 : MF_GRAYED),300+static_cast<UINT>(i),text.c_str());
            AppendMenuW(right,MF_STRING|(i+1 < pins.entries.size() ? 0 : MF_GRAYED),400+static_cast<UINT>(i),text.c_str());
        }
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(remove),L"Remove pin");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(left),L"Move pin left");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(right),L"Move pin right");
        POINT point{}; GetCursorPos(&point); const auto choice = TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr); DestroyMenu(menu);
        if (choice >= 100 && choice < 100 + pins.entries.size()) selectPin(choice-100);
        else if (choice >= 200 && choice < 200 + pins.entries.size()) pins.entries.erase(pins.entries.begin()+(choice-200));
        else if (choice >= 300 && choice < 300 + pins.entries.size()) pins.move(choice-300,-1);
        else if (choice >= 400 && choice < 400 + pins.entries.size()) pins.move(choice-400,1);
        else if (slots[chatIndex] && slots[chatIndex]->chat.identity.owns(source) && source.conversation == conversation)
        {
            if (choice == 1)
            {
                if (selected >= 0) pins.entries.erase(pins.entries.begin()+selected);
                else if (!pins.add(slots[chatIndex]->chat,conversation)) message(L"Cannot pin this conversation: unavailable, duplicate or pin limit reached.");
            }
            else if (choice == 2 && selected >= 0) pins.move(static_cast<std::size_t>(selected),-1);
            else if (choice == 3 && selected >= 0) pins.move(static_cast<std::size_t>(selected),1);
        }
        refreshPins(); layout();
    }
    void refreshPins()
    {
        for (int i = 0; i < MaxCharacters; ++i) if (slots[i]) pins.bind(slots[i]->chat,(slots[i]->snapshot.flags & ChatRestricted) != 0);
        for (std::size_t i = 0; i < 6; ++i)
        {
            if (!pinButtons[i]) continue;
            if (i >= pins.entries.size()) { pinLabels[i].clear(); continue; }
            const auto& pin = pins.entries[i]; const int owner = pinSlot(pin);
            const auto ownerAlias = owner >= 0 ? appearance(owner).alias : std::string{};
            std::wstring label = wide(pin.title) + L" · " + (owner >= 0 ? ownerAlias.empty() ? wide(slots[owner]->snapshot.name) : wide(ownerAlias) : L"Unavailable");
            if (owner >= 0)
            {
                const auto& current = slots[owner]->chat.conversations.at(pin.conversation);
                if (current.unread) label += L" · " + std::to_wstring(current.unread);
            }
            if (label != pinLabels[i])
            {
                pinLabels[i] = std::move(label); SetWindowTextW(pinButtons[i],pinLabels[i].c_str()); InvalidateRect(pinButtons[i],nullptr,FALSE);
                pinTips[i] = (owner >= 0 ? characterName(owner) : L"Unavailable") + L" · " + wide(pin.title);
                TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.uFlags = TTF_IDISHWND|TTF_SUBCLASS; tool.hwnd = GetParent(pinButtons[i]); tool.uId = reinterpret_cast<UINT_PTR>(pinButtons[i]);
                if (tooltips)
                {
                    const bool exists = SendMessageW(tooltips,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&tool)) != 0;
                    tool.lpszText = pinTips[i].data(); SendMessageW(tooltips,exists ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
                }
            }
        }
    }
    void reviewChat()
    {
        if (!slots[chatIndex] || busy()) { message(L"Finish the current switch before reviewing chat."); return; }
        const auto action = boundChat(Kind::ReviewChat);
        slots[chatIndex]->pendingReview = action; slots[chatIndex]->hasPendingReview = true; switchTo(chatIndex);
    }
    std::vector<Message> collectAttention()
    {
        std::vector<Message> rows;std::set<std::tuple<std::string,std::string,std::string>> liveUnread;
        for(int i=0;i<MaxCharacters;++i)if(slots[i] && slots[i]->running() && !slots[i]->detached && slots[i]->pipe.alive() && slots[i]->snapshot.state==State::Ready && !(slots[i]->snapshot.flags&ChatRestricted) && (experience.accountMask&(1u<<i)))
        {
            const auto& view=slots[i]->chat;
            for(const auto& entry:view.conversations)if(entry.second.unread && !entry.second.restricted)
            {
                Message item=slots[i]->snapshot;item.topic=entry.second.topic;item.conversation=entry.first;item.eventType=EventType::Conversation;item.cursor=entry.second.lastIncomingEvent;item.unread=entry.second.unread;item.title=entry.second.title;
                if(attention.hiddenUnread(item) || !(experience.attentionMask&attentionCategoryMask(item)))continue;
                const auto key=std::make_tuple(item.account,item.grid,item.conversation);liveUnread.insert(key);
                if(!unreadSince.count(key))unreadSince[key]=GetTickCount64();item.eventAt=unreadSince[key];rows.push_back(item);
            }
        }
        for(auto it=unreadSince.begin();it!=unreadSince.end();)if(!liveUnread.count(it->first))it=unreadSince.erase(it);else ++it;
        for(const auto& item:attention.entries){const int i=sourceSlot(item);if(i>=0 && slots[i]->running() && !slots[i]->detached && slots[i]->pipe.alive() && slots[i]->snapshot.state==State::Ready && !(slots[i]->snapshot.flags&ChatRestricted) && (experience.accountMask&(1u<<i)) && (experience.attentionMask&attentionCategoryMask(item)))rows.push_back(item);}
        attentionNavigation.update(std::move(rows),experience.oldestFirst);return attentionNavigation.rows;
    }
    void editAttentionFilters()
    {
        using namespace fs_host_ui;std::vector<Field> fields;
        const wchar_t* labels[]={L"Unread IM/group chat",L"Nearby unread",L"Teleport offers",L"Permission requests",L"Inventory offers"};
        for(unsigned int i=0;i<5;++i)fields.push_back({labels[i],experience.attentionMask&(1u<<i)?L"1":L"0",FieldType::Check});
        fields.push_back({L"Oldest first across categories",experience.oldestFirst?L"1":L"0",FieldType::Check});
        for(unsigned int i=0;i<MaxCharacters;++i){Field field{characterName(static_cast<int>(i)),experience.accountMask&(1u<<i)?L"1":L"0",FieldType::Check};field.page=1;fields.push_back(field);}
        if(!edit(window,L"Attention navigation filters · account choices follow profile slots",fields,{L"Categories",L"Characters"}))return;
        experience.attentionMask=experience.accountMask=0;for(unsigned int i=0;i<5;++i)if(number(fields[i]))experience.attentionMask|=1u<<i;
        experience.oldestFirst=number(fields[5])!=0;for(unsigned int i=0;i<MaxCharacters;++i)if(number(fields[i+6]))experience.accountMask|=1u<<i;
        refreshAttention();
    }
    void refreshAttention()
    {
        auto rows=collectAttention();
        SetWindowTextW(attentionButton,(L"Attention · "+std::to_wstring(rows.size())).c_str());
        if(!attentionList)return;
        Message previous;const auto selected=SendMessageW(attentionList,LB_GETCURSEL,0,0);if(selected>=0 && static_cast<std::size_t>(selected)<attentionRows.size())previous=attentionRows[static_cast<std::size_t>(selected)];
        std::vector<std::wstring> labels;for(const auto& m:rows){const int i=sourceSlot(m);labels.push_back((i>=0?characterName(i):L"Unavailable")+L" · "+wide(m.title)+(m.eventType==EventType::Conversation?L" · "+std::to_wstring(m.unread)+L" unread":L""));}
        if(labels!=attentionLabels)
        {
            SendMessageW(attentionList,WM_SETREDRAW,FALSE,0);SendMessageW(attentionList,LB_RESETCONTENT,0,0);
            for(const auto& label:labels)SendMessageW(attentionList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            attentionLabels=std::move(labels);attentionRows=std::move(rows);int chosen=attentionRows.empty()?-1:0;
            for(std::size_t i=0;i<attentionRows.size();++i)if(AttentionBook::same(previous,attentionRows[i]))chosen=static_cast<int>(i);
            SendMessageW(attentionList,LB_SETCURSEL,chosen,0);SendMessageW(attentionList,WM_SETREDRAW,TRUE,0);InvalidateRect(attentionList,nullptr,FALSE);
        }else{attentionRows=std::move(rows);InvalidateRect(attentionList,nullptr,FALSE);}
        SetWindowTextW(attentionWindow,attention.gap?L"Attention · earlier items remain in native viewers":L"Attention · review is explicit; navigation does not mark read");
    }
    void navigateAttention(int direction)
    {
        collectAttention();const auto* found=attentionNavigation.step(direction);
        if(!found){message(L"No attention matches the current filters.");return;}
        const auto item=*found;const int index=sourceSlot(item);
        if(index<0 || !slots[index] || (slots[index]->snapshot.flags&ChatRestricted))return;
        if(item.eventType==EventType::Conversation)
        {
            const auto conversationIt=slots[index]->chat.conversations.find(item.conversation);
            if(conversationIt==slots[index]->chat.conversations.end() || conversationIt->second.restricted)return;
            saveDraft(true);chatIndex=index;conversation=item.conversation;showChat=true;
            if(chrome==2 && !chatDetached)setChrome(1);
            SendMessageW(showChatControl,BM_SETCHECK,BST_CHECKED,0);refreshChat(true,true);layout();SetFocus(chatCompose);
        }
        else
        {
            openAttention();for(std::size_t i=0;i<attentionRows.size();++i)if(AttentionBook::same(item,attentionRows[i])){SendMessageW(attentionList,LB_SETCURSEL,i,0);break;}
            SetFocus(attentionList);
        }
    }
    void layoutAttention()
    {
        if (!attentionWindow || !attentionList) return;
        RECT r{}; GetClientRect(attentionWindow,&r);
        const auto scale = [this](int n) { return theme.scale(n,attentionDpi); };
        MoveWindow(GetDlgItem(attentionWindow,PanelPin),r.right-scale(58),scale(8),scale(50),scale(24),TRUE);
        MoveWindow(attentionList,scale(8),scale(36),(std::max)(1,static_cast<int>(r.right)-scale(16)),(std::max)(1,static_cast<int>(r.bottom)-scale(82)),TRUE);
        MoveWindow(GetDlgItem(attentionWindow,AttentionReview),scale(8),r.bottom-scale(38),scale(180),scale(28),TRUE);
        MoveWindow(GetDlgItem(attentionWindow,AttentionDismiss),scale(200),r.bottom-scale(38),scale(180),scale(28),TRUE);
        SendMessageW(attentionList,LB_SETITEMHEIGHT,0,scale(24));
    }
    void openAttention()
    {
        const bool created=!attentionWindow;
        if (!attentionWindow)
        {
            attentionWindow = CreateWindowExW(WS_EX_TOOLWINDOW,L"FolderstormSessionPanel",L"Attention inbox",WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,CW_USEDEFAULT,scaled(650),scaled(360),window,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (!attentionWindow) return;
            attentionList = CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|WS_BORDER|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,
                8,8,610,260,attentionWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(AttentionList)),GetModuleHandleW(nullptr),nullptr);
            CreateWindowExW(0,L"BUTTON",L"Pin",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,40,24,attentionWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(PanelPin)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(attentionList,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            CreateWindowExW(0,L"BUTTON",L"Review in viewer",WS_CHILD|WS_VISIBLE|WS_TABSTOP,8,280,180,28,attentionWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(AttentionReview)),GetModuleHandleW(nullptr),nullptr);
            CreateWindowExW(0,L"BUTTON",L"Dismiss reminder",WS_CHILD|WS_VISIBLE|WS_TABSTOP,200,280,180,28,attentionWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(AttentionDismiss)),GetModuleHandleW(nullptr),nullptr);
        }
        installPanelMenuOnce(attentionWindow);
        using WindowDpi=UINT(WINAPI*)(HWND);
        const auto windowDpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        panelFont(attentionWindow,windowDpi?windowDpi(attentionWindow):attentionDpi);
        if(created)fitSetRect(attentionWindow,experience.attentionRect,attentionDpi);
        else fitOpenWindow(attentionWindow,attentionDpi,false);
        layoutAttention();
        pinWindow(attentionWindow,false);theme.window(attentionWindow);ShowWindow(attentionWindow,SW_SHOWNORMAL); refreshAttention(); SetForegroundWindow(attentionWindow);
    }
    void attentionAction(bool dismiss)
    {
        const auto selected = SendMessageW(attentionList,LB_GETCURSEL,0,0);
        if (selected < 0 || static_cast<std::size_t>(selected) >= attentionRows.size()) return;
        const auto item = attentionRows[static_cast<std::size_t>(selected)]; const int index = sourceSlot(item);
        if (index < 0) { message(L"The attention source changed; refresh before reviewing."); return; }
        if (dismiss)
        {
            attention.dismiss(item);
            refreshAttention(); return;
        }
        if (busy()) { message(L"Finish the current switch before reviewing attention."); return; }
        if (slots[index]->hasPendingReview) { message(L"This character already has a pending Review action."); return; }
        if (slots[index]->detached || !slots[index]->pipe.alive() || slots[index]->snapshot.state != State::Ready || (slots[index]->snapshot.flags & ChatRestricted))
        { message(L"This character's attention is unavailable. Review its native viewer."); return; }
        if (item.eventType == EventType::Attention)
        {
            auto action = item; action.kind = Kind::ReviewAttention;
            slots[index]->pendingReview = action; slots[index]->hasPendingReview = true;
            switchTo(index);
        }
        else
        {
            saveDraft(true); chatIndex = index; conversation = item.conversation; refreshChat(true,true);
            reviewChat();
        }
    }

    CharacterAppearance appearance(int index) const
    { return validSlot(index) && slots[index] ? presentation.appearance(AccountKey::from(slots[index]->snapshot)) : CharacterAppearance{}; }
    std::wstring characterName(int index) const
    {
        if (!validSlot(index) || !slots[index] || slots[index]->snapshot.name.empty()) return L"Character " + std::to_wstring(index + 1);
        const auto alias = appearance(index).alias;
        return alias.empty() ? wide(slots[index]->snapshot.name) : wide(alias) + L" (" + wide(slots[index]->snapshot.name) + L")";
    }
    COLORREF accountColor(int index) const
    {
        HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) return GetSysColor(COLOR_HIGHLIGHT);
        const auto rgb=[](COLORREF c){return (static_cast<unsigned int>(GetRValue(c))<<16)|(static_cast<unsigned int>(GetGValue(c))<<8)|GetBValue(c);};
        const auto c=visibleAccent(appearance(index).color,rgb(theme.background),rgb(theme.text));
        return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
    }
    void editAppearance(int index)
    {
        if (!validSlot(index) || !slots[index] || slots[index]->snapshot.state != State::Ready) { message(L"Log in before editing character appearance."); return; }
        const auto identity = slots[index]->snapshot; const auto key = AccountKey::from(identity); const auto current = presentation.appearance(key);
        using namespace fs_host_ui;
        std::vector<Field> fields{{L"Friendly name",wide(current.alias)}, {L"Character color",std::to_wstring(current.color),FieldType::Color},
            {L"Background alert sounds",std::to_wstring(current.alerts),FieldType::Choice,{L"Off",L"IM alerts",L"IM and supported attention alerts"}},
            {L"Reset name and color",L"0",FieldType::Check}};
        if (!edit(window,L"Character appearance (Save host choices to keep)",fields)) return;
        if (!slots[index] || !slots[index]->chat.identity.owns(identity)) { message(L"Login changed; appearance edits were discarded."); return; }
        const auto& text = fields[0].value;
        const int size = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        if ((!text.empty() && size == 0) || size > 64) { message(L"Friendly name must fit in 64 UTF-8 bytes."); return; }
        std::string alias(static_cast<std::size_t>(size),'\0');
        if (size) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),alias.data(),size,nullptr,nullptr);
        CharacterAppearance next{alias,number(fields[1]),number(fields[2])};
        if (number(fields[3])) { next.alias.clear(); next.color = defaultAccountColor(key); }
        if (!presentation.set(key,next)) { message(L"Character appearance could not be applied; profile capacity or text is invalid."); return; }
        for (auto& monitor : monitors) monitor.dirty = true;
        for (auto button : pinButtons) InvalidateRect(button,nullptr,FALSE);
        InvalidateRect(characterControl,nullptr,FALSE); InvalidateRect(chatAccount,nullptr,FALSE); InvalidateRect(status[index],nullptr,FALSE);
        refreshCharacters(); refreshChat(); refreshAttention(); if (attentionList) InvalidateRect(attentionList,nullptr,FALSE); updateMonitors(); InvalidateRect(window,nullptr,FALSE);
        if (chatWindow) InvalidateRect(chatWindow,nullptr,FALSE); if (controlsWindow) InvalidateRect(controlsWindow,nullptr,FALSE);
    }
    void capturePlacement(HWND panel, unsigned int id, UINT localDpi)
    {
        if (!panel) return;
        WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
        if (!GetWindowPlacement(panel,&placement)) return;
        auto r = placement.rcNormalPosition;
        if (!(GetWindowLongPtrW(panel,GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        {
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST),&monitor))
                OffsetRect(&r,monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top);
        }
        ShellRect rect{static_cast<int>(r.left),static_cast<int>(r.top),static_cast<int>(r.right-r.left),static_cast<int>(r.bottom-r.top),localDpi};
        if (rect.valid()) presentation.rectangles[id] = rect;
    }
    void restorePlacement(HWND panel, unsigned int id, UINT localDpi)
    {
        const auto found = presentation.rectangles.find(id); if (!panel || found == presentation.rectangles.end()) return;
        const auto& r = found->second; RECT target{r.x,r.y,r.x+r.width,r.y+r.height};
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST),&monitor)) return;
        const auto& w = monitor.rcWork;
        const auto fit = fitShellRect(r,{static_cast<int>(w.left),static_cast<int>(w.top),static_cast<int>(w.right-w.left),static_cast<int>(w.bottom-w.top),96},localDpi);
        if (fit.valid()) SetWindowPos(panel,nullptr,fit.x,fit.y,fit.width,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    bool savePresentation()
    {
        capturePlacement(window,0,dpi); capturePlacement(chatWindow,1,chatDpi); capturePlacement(controlsWindow,2,controlsDpi);
        for (unsigned int i = 0; i < MaxMonitors; ++i) capturePlacement(monitors[i].window,i+3,monitors[i].dpi);
        presentation.pins = pins.saved();
        const auto data = presentation.encode();
        if (data.empty()) { message(L"Host appearance choices failed validation."); return false; }
        const auto temp = profiles / L"host-presentation.tmp";
        std::ofstream out(temp,std::ios::binary|std::ios::trunc); out.write(data.data(),static_cast<std::streamsize>(data.size())); out.close();
        if (!out || !MoveFileExW(temp.c_str(),(profiles / L"host-presentation.dat").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        { message(L"Host appearance choices could not be saved; session choices remain active."); return false; }
        return true;
    }
    bool drawIdentity(const DRAWITEMSTRUCT& draw)
    {
        if (draw.CtlID >= PinBase && draw.CtlID < PinBase+6)
        {
            const auto id = static_cast<std::size_t>(draw.CtlID-PinBase); FillRect(draw.hDC,&draw.rcItem,theme.backBrush);
            if (id >= pins.entries.size()) return true;
            const int owner = pinSlot(pins.entries[id]); RECT accent = draw.rcItem; accent.bottom = accent.top + scaled(4);
            const HBRUSH brush = CreateSolidBrush(owner >= 0 ? accountColor(owner) : GetSysColor(COLOR_GRAYTEXT)); FillRect(draw.hDC,&accent,brush); DeleteObject(brush);
            RECT text = draw.rcItem; text.left += scaled(5); text.right -= scaled(5); text.top += scaled(4);
            SetTextColor(draw.hDC,owner>=0?theme.text:theme.muted); SetBkMode(draw.hDC,TRANSPARENT);
            DrawTextW(draw.hDC,pinLabels[id].c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            if (draw.itemState & ODS_FOCUS) DrawFocusRect(draw.hDC,&draw.rcItem); return true;
        }
        if (draw.CtlID == AttentionList)
        {
            theme.selection(draw);
            if (draw.itemID >= attentionRows.size()) return true;
            const int owner = sourceSlot(attentionRows[draw.itemID]); RECT accent = draw.rcItem; accent.right = accent.left + scaled(6);
            const HBRUSH brush = CreateSolidBrush(owner >= 0 ? accountColor(owner) : GetSysColor(COLOR_GRAYTEXT)); FillRect(draw.hDC,&accent,brush); DeleteObject(brush);
            RECT text = draw.rcItem; text.left += scaled(12); SetBkMode(draw.hDC,TRANSPARENT);
            DrawTextW(draw.hDC,attentionLabels[draw.itemID].c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            theme.selectionFocus(draw);return true;
        }
        if (draw.CtlID == ChatAccent)
        { const HBRUSH brush = CreateSolidBrush(accountColor(chatIndex)); FillRect(draw.hDC,&draw.rcItem,brush); DeleteObject(brush); return true; }
        for (int i = 0; i < MaxCharacters; ++i) if (draw.hwndItem == status[i])
        {
            FillRect(draw.hDC,&draw.rcItem,theme.backBrush); RECT accent = draw.rcItem; accent.right = accent.left + scaled(5);
            const HBRUSH brush = CreateSolidBrush(accountColor(i)); FillRect(draw.hDC,&accent,brush); DeleteObject(brush);
            RECT text = draw.rcItem; text.left += scaled(10); SetTextColor(draw.hDC,theme.text); SetBkMode(draw.hDC,TRANSPARENT);
            DrawTextW(draw.hDC,descriptions[i].c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS); return true;
        }
        if(draw.CtlID>=CardBase && draw.CtlID<CardBase+MaxCharacters)
        {
            const int owner=static_cast<int>(draw.CtlID)-CardBase;theme.button(draw);
            if(owner==lifecycle.selection()){const HBRUSH selected=CreateSolidBrush(theme.accent);FrameRect(draw.hDC,&draw.rcItem,selected);DeleteObject(selected);}
            RECT stripe=draw.rcItem;stripe.right=stripe.left+scaled(4);
            const HBRUSH brush=CreateSolidBrush(accountColor(owner));FillRect(draw.hDC,&stripe,brush);DeleteObject(brush);return true;
        }
        if(theme.button(draw))return true;
        if (draw.CtlType != ODT_COMBOBOX || (draw.CtlID != ActiveCharacter && draw.CtlID != ChatAccount)) return false;
        const int index = draw.itemID == static_cast<UINT>(-1) ? (draw.CtlID == ActiveCharacter ? lifecycle.selection() : chatIndex) : static_cast<int>(draw.itemID);
        theme.selection(draw);
        RECT accent = draw.rcItem; accent.right = accent.left + scaled(5);
        const HBRUSH brush = CreateSolidBrush(accountColor(index)); FillRect(draw.hDC,&accent,brush); DeleteObject(brush);
        RECT text = draw.rcItem; text.left += scaled(10);
        SetBkMode(draw.hDC,TRANSPARENT);
        std::wstring name = characterName(index);
        if (draw.CtlID == ActiveCharacter) name = std::to_wstring(index+1) + L": " + name + (index == active ? L" · active" : L"");
        DrawTextW(draw.hDC,name.c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        theme.selectionFocus(draw);return true;
    }

    int scaled(int value) const { return theme.scale(value,dpi); }
    void updateDpi(UINT value)
    {
        dpi = (std::max)(96u, value);
        HFONT replacement = CreateFontW(-scaled(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (replacement)
        {
            EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL
            { fs_host_ui::setNativeFont(child,reinterpret_cast<HFONT>(data));return TRUE; }, reinterpret_cast<LPARAM>(replacement));
            if (font) DeleteObject(font);
            font = replacement;
        }
    }
    int selectedSlot(HWND choices) const
    {
        const auto selected = SendMessageW(choices, CB_GETCURSEL, 0, 0);
        if (selected >= 0 && selected < MaxCharacters) return static_cast<int>(selected);
        return choices == characterControl ? lifecycle.selection() : chatIndex;
    }
    void launchNext()
    {
        for (int i = 0; i < MaxCharacters; ++i) if (!slots[i] || !slots[i]->running()) { launch(i); return; }
        message(L"All character slots are occupied. Use Session to restart or close a selected character.");
    }
    void refreshCharacters()
    {
        const auto selected = SendMessageW(characterControl, CB_GETCURSEL, 0, 0);
        std::vector<std::wstring> choices;
        for (int i = 0; i < MaxCharacters; ++i)
        {
            std::wstring text = std::to_wstring(i + 1) + L": ";
            text += slots[i] && !slots[i]->snapshot.name.empty() ? characterName(i) : L"not logged in";
            if (i == active) text += L" · active";
            choices.push_back(std::move(text));
        }
        const int management = lifecycle.selection();
        (void)selected;
        syncChoices(characterControl, choices, management);
        EnableWindow(characterControl,TRUE); // Management/recovery remain reachable during handoff.
    }

    void message(const std::wstring& text)
    {
        lastNotice = text; SetWindowTextW(notice, text.c_str());
        if (chrome == 2) SetWindowTextW(revealControl,(L"Show controls · "+text).c_str());
    }
    void tray(bool add)
    {
        NOTIFYICONDATAW icon{}; icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = 1;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; icon.uCallbackMessage = TrayMessage;
        icon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_SESSION_HOST),IMAGE_ICON,16,16,LR_SHARED));
        wcscpy_s(icon.szTip,L"Folderstorm character sessions — restore controls");
        trayPresent = Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE,&icon) != FALSE && add;
    }
    void fitOpenWindow(HWND panel, UINT localDpi, bool nearHost)
    {
        if (!panel) return;
        const auto currentPlacement=liveRect(panel,localDpi);
        RECT rect{currentPlacement.x,currentPlacement.y,currentPlacement.x+currentPlacement.width,currentPlacement.y+currentPlacement.height};
        MONITORINFO info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(nearHost ? window : panel,MONITOR_DEFAULTTONEAREST),&info)) return;
        const auto& work = info.rcWork;
        ShellRect current{static_cast<int>(rect.left),static_cast<int>(rect.top),static_cast<int>(rect.right-rect.left),static_cast<int>(rect.bottom-rect.top),localDpi};
        if (nearHost) { current.x = static_cast<int>(work.left)+20; current.y = static_cast<int>(work.top)+20; }
        const auto fit = reachableRect(current,{static_cast<int>(work.left),static_cast<int>(work.top),static_cast<int>(work.right-work.left),static_cast<int>(work.bottom-work.top),96},localDpi);
        if (fit.valid()) SetWindowPos(panel,nullptr,fit.x,fit.y,fit.width,fit.height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void recover(bool positions = false)
    {
        if (IsIconic(window)) ShowWindow(window,SW_RESTORE);
        setChrome(recoveredChrome(chrome)); fitOpenWindow(window,dpi,false);
        if (positions)
        {
            for (HWND panel : {chatWindow,controlsWindow,attentionWindow}) if (panel)
            { experience.pinned[windowKey(panel)]=false;pinWindow(panel,false);if(IsIconic(panel))ShowWindow(panel,SW_SHOWNOACTIVATE); fitOpenWindow(panel,panel == chatWindow ? chatDpi : panel == controlsWindow ? controlsDpi : attentionDpi,true); }
            for (auto& monitor : monitors) if (monitor.window)
            { experience.pinned[windowKey(monitor.window)]=false;pinWindow(monitor.window,false);if(IsIconic(monitor.window))ShowWindow(monitor.window,SW_SHOWNOACTIVATE); fitOpenWindow(monitor.window,monitor.dpi,true); }
        }
        ShowWindow(window,SW_SHOW); SetForegroundWindow(window); layout();
        message(positions ? L"Controls and window positions recovered. Window pins were cleared for this session." : L"Host controls restored.");
    }
    void refitWindows()
    {
        fitOpenWindow(window,dpi,false);
        for (HWND panel : {chatWindow,controlsWindow,attentionWindow}) if (panel) fitOpenWindow(panel,panel == chatWindow ? chatDpi : panel == controlsWindow ? controlsDpi : attentionDpi,false);
        for (auto& monitor : monitors) if (monitor.window) fitOpenWindow(monitor.window,monitor.dpi,false);
        layout();
    }
    void recoveryMenu(HWND panel)
    {
        auto menu = GetSystemMenu(panel,FALSE);
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,0xA140,L"Restore host controls");
        AppendMenuW(menu,MF_STRING,0xA150,L"Recover window positions");
    }
    int closeAction(bool alreadyClosing)
    {
        // Native command links make the lifecycle choices explicit. Older
        // common-controls runtimes use the same choices in the regular editor.
        using TaskDialog=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
        const auto dialog=reinterpret_cast<TaskDialog>(GetProcAddress(GetModuleHandleW(L"comctl32.dll"),"TaskDialogIndirect"));
        const TASKDIALOG_BUTTON buttons[]={{1001,L"Keep characters logged in\nClose this host and return to separate viewer windows."},
            {1002,L"Log out characters\nAsk the native viewers to log out, then close this host."}};
        if(dialog)
        {
            TASKDIALOGCONFIG options{};options.cbSize=sizeof(options);options.hwndParent=window;
            options.dwFlags=TDF_USE_COMMAND_LINKS|TDF_ALLOW_DIALOG_CANCELLATION;options.dwCommonButtons=TDCBF_CANCEL_BUTTON;
            options.pszWindowTitle=L"Close Folderstorm character sessions";
            options.pszMainInstruction=alreadyClosing?L"Leave the remaining characters logged in?":L"What should happen to your characters?";
            options.pButtons=buttons;options.cButtons=alreadyClosing?1u:2u;options.nDefaultButton=IDCANCEL;
            int result=IDCANCEL;if(SUCCEEDED(dialog(&options,&result,nullptr,nullptr)))return result==1001?1:result==1002?2:0;
        }
        using namespace fs_host_ui;
        std::vector<Field> fields{{L"When this host closes",L"0",FieldType::Choice,{L"Keep characters logged in in separate windows"}}};
        if(!alreadyClosing)fields[0].choices.push_back(L"Log out characters, then close this host");
        return edit(window,L"Close host · Cancel keeps working",fields)?static_cast<int>(number(fields[0]))+1:0;
    }
    void setChrome(unsigned int mode)
    {
        if (mode > 2) return;
        chrome = mode;
        for (unsigned int i = 0; i < 3; ++i)
            CheckMenuItem(GetSystemMenu(window, FALSE), 0xA100u + i * 0x10u, MF_BYCOMMAND | (chrome == i ? MF_CHECKED : MF_UNCHECKED));
        SetWindowTextW(window, (L"Folderstorm · " + std::to_wstring(MaxCharacters) + L" character slots").c_str());
        layout();
    }
    bool send(int index, Kind kind, Mode mode = Mode::Warm, std::uint64_t surface = 0, const Message* bound = nullptr)
    {
        if (!validSlot(index) || !slots[index]) return false;
        auto& slot = *slots[index];
        if (slot.waiting || !slot.pipe.alive() || slot.pipe.writing()) return false;
        Message request;
        if (bound)
        {
            if (!slot.chat.identity.owns(*bound) || !slot.chat.identity.owns(slot.snapshot) || (slot.snapshot.state != State::Ready && kind != Kind::Quit))
            { message(L"The selected character/session changed. The action was not sent."); return false; }
            request = *bound;
        }
        request.worker = slot.id; request.kind = kind; request.mode = mode; request.surface = surface;
        if (!bound) request.generation = slot.snapshot.generation;
        request.sequence = ++slot.nextSequence;
        if (kind == Kind::Events || kind == Kind::Conversations)
        {
            request.account = slot.snapshot.account; request.grid = slot.snapshot.grid;
            request.cursor = kind == Kind::Events ? slot.chat.cursor : slot.catalogIndex;
        }
        if (kind == Kind::WorkspaceInfo) { request.account = slot.snapshot.account; request.grid = slot.snapshot.grid; }
        if (kind == Kind::SetMode)
        {
            request.account = slot.snapshot.account; request.grid = slot.snapshot.grid;
            request.unread = handoff.step() != Handoff::Step::Idle && handoff.step() != Handoff::Step::Rollback && handoffAnimated ? handoffStyle : 0u;
            request.width = handoffDuration; request.height = handoffHeight;
        }
        if (kind == Kind::MonitorPolicy)
        {
            request.account = slot.snapshot.account; request.grid = slot.snapshot.grid;
            const auto policy = monitorPolicy(index);
            request.width = policy.width; request.height = policy.height; request.unread = policy.unread;
        }
        if (kind == Kind::ShortcutPolicy) { request.account = slot.snapshot.account; request.grid = slot.snapshot.grid; request.unread = shortcutPolicy(); }
        if (kind == Kind::AudioPolicy) request.unread = (voice ? 1u : 0u) | (muteBackground ? 2u : 0u);
        DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if ((kind == Kind::ReviewChat || kind == Kind::ReviewAttention) && foregroundPid == GetCurrentProcessId()) AllowSetForegroundWindow(slot.pid);
        if ((kind == Kind::Focus || (kind == Kind::SetMode && mode == Mode::Active)) && GetForegroundWindow() == window)
            AllowSetForegroundWindow(slot.pid);
        if (kind == Kind::PermitLogin || kind == Kind::DenyLogin) { request.grid = slot.snapshot.grid; request.name = slot.snapshot.name; }
        if (!slot.pipe.send(request)) return false;
        slot.request = request; slot.waiting = true; slot.sentAt = GetTickCount64();
        return true;
    }
    HostOptions options() const
    {
        HostOptions result; result.standby = standby;
        result.chrome = chrome; result.chatDetached = chatDetached; result.controlsDetached = controlsDetached;
        result.voice = voice; result.muteBackground = muteBackground; result.hosted = embedding; result.chat = showChat;
        result.previewSize = monitorSize; result.previewRate = monitorRate;
        result.cinematic = presentation.transition == 1;
        return result;
    }
    void saveOptions()
    {
        const auto temporary = profiles / L"host-options.tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto data = options().encode(); output.write(data.data(), static_cast<std::streamsize>(data.size())); output.close();
        if (!output || !MoveFileExW(temporary.c_str(), (profiles / L"host-options.txt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        { message(L"Host choices could not be saved. Current session choices remain active."); return; }
        if (!savePresentation()) return;
        if(attentionWindow)experience.attentionRect=liveRect(attentionWindow,attentionDpi);
        if(!saveStore(L"host-experience.dat",experience.encode())){message(L"Earlier host choices saved, but appearance/quick actions could not be saved. Session changes remain active.");return;}
        message(L"Host choices saved. Character credentials, chats and workspace contents are not in this file.");
    }
    void sessionMenu(unsigned int group=0,UINT direct=0,int target=-1)
    {
        focusRequested=false;RemovePropW(window,L"FolderstormViewportFocusIntent");
        const int index = validSlot(target)?target:selectedSlot(characterControl);
        auto* slot = slots[index].get();
        Message action;
        if (slot)
        {
            action.worker = slot->chat.identity.worker; action.generation = slot->chat.identity.generation;
            action.account = slot->chat.identity.account; action.grid = slot->chat.identity.grid;
        }
        HMENU menu=CreatePopupMenu();if(!menu)return;
        HMENU characters=CreatePopupMenu(),view=CreatePopupMenu(),settings=CreatePopupMenu(),login=CreatePopupMenu(),mon=CreatePopupMenu();
        const auto add=[](HMENU m,UINT id,const std::wstring& text,bool enabled=true,bool checked=false){AppendMenuW(m,MF_STRING|(enabled?0:MF_GRAYED)|(checked?MF_CHECKED:0),id,text.c_str());};
        const std::wstring name=characterName(index);
        add(characters,201,L"Workspaces: "+name+ (index==active?L"":L" (switch then open)…"),slot && slot->snapshot.state==State::Ready && !busy());
        add(characters,MonitorSelected,L"Monitor: "+name,slot && slot->snapshot.state==State::Ready && index!=active && !busy());
        add(characters,430,L"Background: Warm",slot!=nullptr,standby[index]==Mode::Warm);add(characters,431,L"Background: Economy",slot!=nullptr,standby[index]==Mode::Economy);
        add(characters,273,L"Name, color and alerts…",slot && slot->snapshot.state==State::Ready);
        add(characters,432,L"Move character card earlier");add(characters,433,L"Move character card later");
        AppendMenuW(characters,MF_SEPARATOR,0,nullptr);
        add(characters,240,L"Restart: "+name+L"…",!busy());add(characters,241,L"Cancel pending restart",slot && slot->restart.phase()!=Restart::Phase::Idle);
        add(characters,202,L"Close: "+name+L"…",slot && slot->pipe.alive() && !slot->detached && !busy());
        add(characters,203,L"Open selected profile folder");add(characters,277,L"Copy main settings and open login…",!busy() && (!slot || !slot->running()));
        add(characters,SetActions,L"Session sets…");
        HMENU switches=CreatePopupMenu();for(int i=0;i<MaxCharacters;++i)add(switches,440+static_cast<UINT>(i),characterName(i),slots[i] && slots[i]->snapshot.state==State::Ready && !busy(),i==active);
        AppendMenuW(characters,MF_POPUP,reinterpret_cast<UINT_PTR>(switches),L"Switch character");
        AppendMenuW(characters,MF_SEPARATOR,0,nullptr);add(characters,209,L"Stop managing; leave viewers running separately…");add(characters,434,L"Close host…");
        add(login,LaunchNext,L"Open next available login",!busy());
        for(int i=0;i<MaxCharacters;++i){add(login,LaunchBase+static_cast<UINT>(i),L"Open Character "+std::to_wstring(i+1)+L" login",!busy() && (!slots[i] || !slots[i]->running()));add(mon,MonitorBase+static_cast<UINT>(i),characterName(i),slots[i] && slots[i]->snapshot.state==State::Ready && i!=active && !busy());}
        add(mon,212,L"Stop all monitors");
        add(view,250,L"Expanded controls",true,chrome==0);add(view,251,L"Condensed controls",true,chrome==1);add(view,252,L"Minimal with Show controls",true,chrome==2);
        add(view,270,chatDetached?L"Dock shared Chat":L"Pop out shared Chat");add(view,271,controlsDetached?L"Dock character controls":L"Pop out character controls");add(view,272,L"Show shared Chat",true,showChat);
        add(view,435,L"Host active viewer",true,embedding);add(view,436,L"Show active viewer separately");AppendMenuW(view,MF_POPUP,reinterpret_cast<UINT_PTR>(mon),L"Monitors");
        add(view,Details,L"Show diagnostics",true,experience.diagnostics);add(view,RestoreShell,L"Restore host controls");add(view,RecoverShell,L"Recover window positions");
        add(settings,AppearanceSettings,L"Host settings: appearance, audio and switching…");add(settings,QuickSettings,L"Customize quick actions…");add(settings,AttentionSettings,L"Attention filters and ordering…");add(settings,276,L"Keyboard shortcuts…");add(settings,206,L"Save host choices");
        if(group==1){AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(characters),L"Selected character actions");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(login),L"Open login");}
        else if(group==2)AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"View");
        else if(group==3)AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(settings),L"Settings");
        else if(group==4)AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(login),L"Open login");
        else if(group!=5){AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(characters),L"Characters");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"View");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(settings),L"Settings");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(login),L"Open login");}
        if(group==0 || group==5)
        {
            HMENU attentionMenu=group==5?menu:CreatePopupMenu();
            add(attentionMenu,274,L"Open attention inbox");add(attentionMenu,NextAttention,L"Next attention");add(attentionMenu,PreviousAttention,L"Previous attention");
            add(attentionMenu,AttentionSettings,L"Filters and ordering…");
            if(group==0)AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(attentionMenu),L"Attention");
        }
        // Destroy unattached menus independently; menu owns all attached descendants.
        if(group==1){DestroyMenu(view);DestroyMenu(settings);}else if(group==2){DestroyMenu(characters);DestroyMenu(login);DestroyMenu(settings);}else if(group==3){DestroyMenu(characters);DestroyMenu(login);DestroyMenu(view);}else if(group==4){DestroyMenu(characters);DestroyMenu(view);DestroyMenu(settings);}
        else if(group==5){DestroyMenu(characters);DestroyMenu(login);DestroyMenu(view);DestroyMenu(settings);}
        if(group==2){RemoveMenu(menu,0,MF_BYPOSITION);DestroyMenu(menu);menu=view;}
        else if(group==3){RemoveMenu(menu,0,MF_BYPOSITION);DestroyMenu(menu);menu=settings;}
        else if(group==4){RemoveMenu(menu,0,MF_BYPOSITION);DestroyMenu(menu);menu=login;}
        else if(group==1){RemoveMenu(menu,0,MF_BYPOSITION);RemoveMenu(menu,0,MF_BYPOSITION);DestroyMenu(menu);menu=characters;AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(login),L"Open login");}
        POINT point{};GetCursorPos(&point);menuOpen=true;
        const UINT choice=direct?direct:TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr);
        menuOpen=false;DestroyMenu(menu);
        const bool scoped=choice==201 || choice==202 || choice==240 || choice==241 || choice==273 || choice==277 || choice==MonitorSelected || (choice>=430 && choice<=433);
        if(scoped && (slots[index].get()!=slot || (slot && !slot->chat.identity.owns(action)))){message(L"Character changed while choosing an action; choose it again.");return;}
        if(choice==RestoreShell){recover();return;}if(choice==RecoverShell){recover(true);return;}
        if(choice==AppearanceSettings){editHostSettings();return;}if(choice==QuickSettings){customizeQuick();return;}
        if(choice==NextAttention || choice==PreviousAttention){navigateAttention(choice==NextAttention?1:-1);return;}
        if(choice==AttentionSettings){editAttentionFilters();return;}
        if(choice==SetActions){sessionSetsMenu();return;}
        if(choice==Details){experience.diagnostics=!experience.diagnostics;layout();return;}
        if(choice==MonitorSelected){openMonitor(index);return;}
        if(choice==430 || choice==431){standby[index]=choice==430?Mode::Warm:Mode::Economy;SendMessageW(standbyControl[index],CB_SETCURSEL,choice==430?0:1,0);return;}
        if(choice==432 || choice==433){const auto at=std::find(experience.order.begin(),experience.order.end(),static_cast<unsigned int>(index));if(at!=experience.order.end() && (choice==432?at!=experience.order.begin():at+1!=experience.order.end()))std::iter_swap(at,choice==432?at-1:at+1);layout();return;}
        if(choice>=440 && choice<440+MaxCharacters){switchTo(static_cast<int>(choice)-440);return;}
        if(choice==434){SendMessageW(window,WM_CLOSE,0,0);return;}
        if(choice==435 || choice==436){embedding=choice==435?!embedding:false;SendMessageW(embedControl,BM_SETCHECK,embedding?BST_CHECKED:BST_UNCHECKED,0);return;}
        if (choice == 201 || choice == 202)
        {
            // Timer replies may have changed login while the popup was open.
            if (!slots[index] || slots[index].get() != slot || !slot->chat.identity.owns(action) || slot->hasPendingAction)
            { message(L"Character changed or has a pending action. Select it again."); return; }
            if(choice==201 && index!=active)
            {
                if(MessageBoxW(window,(L"Switch to "+characterName(index)+L" and open workspace controls?").c_str(),L"Workspaces",MB_OKCANCEL)!=IDOK)return;
                if(!slots[index] || slots[index].get()!=slot || !slot->chat.identity.owns(action))return;
                switchTo(index);
            }
            action.kind = choice == 201 ? Kind::WorkspaceMenu : Kind::Quit;
            slot->pendingAction = action; slot->hasPendingAction = true;
            message(choice == 201 ? L"Opening this character's workspace controls…" : L"Returning this character to its native window for the normal logout confirmation…");
        }
        else if (choice >= 280 && choice <= 286) dispatchShortcut(choice-280);
        else if (choice == 276) editShortcuts();
        else if (choice == 275) editAlertVolume();
        else if (choice == 274) openAttention();
        else if (choice == 273) editAppearance(index);
        else if (choice == 270) setPanelDetached(true, !chatDetached);
        else if (choice == 271) setPanelDetached(false, !controlsDetached);
        else if (choice == 272)
        { saveDraft(true); showChat = !showChat; SendMessageW(showChatControl, BM_SETCHECK, showChat ? BST_CHECKED : BST_UNCHECKED, 0); layout(); }
        else if (choice >= 250 && choice <= 252) setChrome(choice - 250);
        else if (choice == 240)
        {
            if (slots[index].get() != slot || (slot && !slot->chat.identity.owns(action)))
            { message(L"Character changed while its menu was open; restart was discarded."); return; }
            restartCharacter(index);
        }
        else if (choice == 241 && slots[index] && slots[index].get() == slot && slot->chat.identity.owns(action)) { slots[index]->restart.cancel(); message(L"Restart cancelled; no new viewer will launch."); }
        else if (choice == 203)
        {
            const auto path = profiles / (L"Character" + std::to_wstring(index + 1));
            ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        else if (choice == 277) copyProfileSettings(index);
        else if (choice == 209) {if(MessageBoxW(window,L"Stop managing all characters and leave their viewers running separately?",L"Stop managing",MB_OKCANCEL)==IDOK)detach(false);}
        else if (choice == 204) { voice = !voice; SendMessageW(voiceControl, BM_SETCHECK, voice ? BST_CHECKED : BST_UNCHECKED, 0); }
        else if (choice == 205) { muteBackground = !muteBackground; SendMessageW(muteControl, BM_SETCHECK, muteBackground ? BST_CHECKED : BST_UNCHECKED, 0); }
        else if (choice == 206) saveOptions();
        else if (choice == 207) editTransition();
        else if (choice == 208) skipTransition();
        else if(choice==LaunchNext)launchNext();
        else if (choice >= LaunchBase && choice < LaunchBase + MaxCharacters) launch(static_cast<int>(choice) - LaunchBase);
        else if (choice >= MonitorBase && choice < MonitorBase + MaxCharacters) openMonitor(static_cast<int>(choice) - MonitorBase);
        else if (choice == 212) stopMonitors();
        else if (choice >= 220 && choice <= 222)
        {
            monitorSize = choice - 220;
            const int id = monitorSet.find(index); if (id >= 0 && monitorSet.bindings[id].key.owns(action)) monitorSet.bindings[id].size = monitorSize;
        }
        else if (choice >= 230 && choice <= 233)
        {
            monitorRate = choice - 230;
            const int id = monitorSet.find(index); if (id >= 0 && monitorSet.bindings[id].key.owns(action)) monitorSet.bindings[id].rate = monitorRate;
        }
    }
    void clearMonitor(int id)
    {
        auto& monitor = monitors[id];
        if (!monitor.pixels.empty()) { monitor.pixels.clear(); monitor.dirty = true; }
    }
    void stopMonitor(int id, bool hide = false)
    {
        if (id < 0 || id >= MaxMonitors) return;
        monitorSet.stop(id); clearMonitor(id); monitors[id].dirty = true;
        refreshMonitorChat(id);
        if (hide && monitors[id].window) ShowWindow(monitors[id].window, SW_HIDE);
    }
    void stopMonitors()
    { for (int i = 0; i < MaxMonitors; ++i) stopMonitor(i, true); }
    bool wantsMonitor(int id) const
    {
        const auto& binding = monitorSet.bindings[id]; const auto& monitor = monitors[id];
        if (!binding.enabled || !validSlot(binding.slot)) return false;
        const auto* slot = slots[binding.slot].get();
        return slot && slot->running() && !slot->detached && slot->pipe.alive() && binding.key.owns(slot->snapshot) &&
            slot->snapshot.state == State::Ready && !(slot->snapshot.flags & ChatRestricted) &&
            binding.slot != active && !busy() && monitor.window && IsWindowVisible(monitor.window) && !IsIconic(monitor.window) && !IsIconic(window);
    }
    unsigned int visibleMonitors() const
    { unsigned int count = 0; for (int i = 0; i < MaxMonitors; ++i) if (wantsMonitor(i)) ++count; return count; }
    Message monitorPolicy(int index) const
    {
        Message policy; const int id = monitorSet.find(index);
        if (id >= 0 && wantsMonitor(id))
        {
            const auto& binding = monitorSet.bindings[id];
            policy.width = 320u + binding.size * 160u; policy.height = 180u + binding.size * 90u;
            policy.unread = previewBudget(binding.rate, visibleMonitors());
        }
        return policy;
    }
    bool admitMonitorPolicy(int index, const Message& policy) const
    {
        if (slots[index] && policy.unread <= slots[index]->previewRate) return true;
        return previewAdmission(index, policy.unread, [this](int i)
        {
            const auto* slot = slots[i].get();
            if (!slot) return 0u;
            // A lost worker may still be capturing until its watchdog runs.
            // Keep its last acknowledged rate reserved while its process lives.
            if (!slot->running()) return 0u;
            const auto pending = slot->waiting && slot->request.kind == Kind::MonitorPolicy ? slot->request.unread : 0u;
            return (std::max)(slot->previewRate, pending);
        });
    }
    void monitorMenu(int id)
    {
        if (id < 0 || id >= MaxMonitors || !monitorSet.bindings[id].enabled) return;
        const auto captured = monitorSet.bindings[id]; const auto popup = CreatePopupMenu();
        if (!popup) return;
        const wchar_t* rates[] = {L"Target 0.5 FPS", L"Target 1 FPS", L"Target 2 FPS", L"Target 5 FPS"};
        for (unsigned int i = 0; i < 3; ++i)
        {
            const auto label = std::to_wstring(320u + i * 160u) + L" × " + std::to_wstring(180u + i * 90u);
            AppendMenuW(popup, MF_STRING | (captured.size == i ? MF_CHECKED : 0), i + 1, label.c_str());
        }
        for (unsigned int i = 0; i < 4; ++i) AppendMenuW(popup, MF_STRING | (captured.rate == i ? MF_CHECKED : 0), i + 11, rates[i]);
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING | MF_GRAYED, 19, L"All monitors share a 10 FPS budget; image is read only");
        AppendMenuW(popup, MF_STRING | (monitors[id].showChat ? MF_CHECKED : 0), 21, L"Show chat");
        AppendMenuW(popup, MF_STRING, 22, L"Use this size and frame rate as monitor defaults");
        AppendMenuW(popup, MF_STRING, 20, L"Close this monitor");
        POINT point{}; GetCursorPos(&point);
        menuOpen = true;
        const auto choice = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, monitors[id].window, nullptr);
        menuOpen = false;
        DestroyMenu(popup);
        const auto& current = monitorSet.bindings[id];
        if (!current.enabled || current.slot != captured.slot || !validSlot(current.slot) || !slots[current.slot] ||
            !captured.key.owns(slots[current.slot]->snapshot) || !current.key.owns(slots[current.slot]->snapshot))
        { message(L"Monitor session changed; its old menu choice was discarded."); return; }
        if (choice >= 1 && choice <= 3) monitorSet.bindings[id].size = static_cast<unsigned int>(choice - 1);
        else if (choice >= 11 && choice <= 14) monitorSet.bindings[id].rate = static_cast<unsigned int>(choice - 11);
        else if (choice == 20) stopMonitor(id, true);
        else if (choice == 21) setMonitorChat(id,!monitors[id].showChat);
        else if (choice == 22)
        {monitorSize=current.size;monitorRate=current.rate;message(L"Monitor defaults updated for this session. Save host choices to keep them.");}
    }
    void openMonitor(int index)
    {
        if (!validSlot(index) || !slots[index] || !slots[index]->running() || slots[index]->detached || !slots[index]->pipe.alive() || slots[index]->snapshot.state != State::Ready)
        { message(L"Log in this character before opening its read-only monitor."); return; }
        if (busy() || index == active) { message(L"Choose an inactive character after the switch finishes to open its monitor."); return; }
        const int id = monitorSet.open(index, slots[index]->snapshot, monitorSize, monitorRate);
        if (id < 0) { message(L"All inactive-monitor slots are occupied, or this session changed. Close a monitor before opening another."); return; }
        auto& monitor = monitors[id];
        monitor.frame = {}; clearMonitor(id); monitor.dirty = true;
        const bool newMonitor = !monitor.window;
        if (newMonitor)
            monitor.window = CreateWindowExW(WS_EX_TOOLWINDOW, L"FolderstormSessionMonitor", L"Character monitor (read only)",
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, scaled(380), scaled(270), window, nullptr, GetModuleHandleW(nullptr), &monitor);
        if (!monitor.window) { stopMonitor(id); message(L"Unable to open the monitor window."); return; }
        using WindowDpi = UINT(WINAPI*)(HWND);
        const auto windowDpi = reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (windowDpi) monitor.dpi = (std::max)(96u, windowDpi(monitor.window));
        if (newMonitor) { installPanelMenu(monitor.window); restorePlacement(monitor.window,static_cast<unsigned int>(id)+3,monitor.dpi); }
        createMonitorChat(id); layoutMonitorChat(id);
        pinWindow(monitor.window,false);theme.window(monitor.window);ShowWindow(monitor.window, SW_SHOWNORMAL); updateMonitors();
    }
    void finishMonitorExchange(bool success)
    {
        const int id = monitorSet.pendingIndex();
        const auto outcome = monitorSet.finish(success, active, [this](int index) -> const Message*
        {
            if (!validSlot(index) || !slots[index] || !slots[index]->running() || slots[index]->detached || !slots[index]->pipe.alive()) return nullptr;
            return &slots[index]->snapshot;
        });
        if (outcome == MonitorSet::Exchange::Exchanged || outcome == MonitorSet::Exchange::Stopped)
        {
            clearMonitor(id); monitors[id].frame = {}; monitors[id].dirty = true;
            if (outcome == MonitorSet::Exchange::Stopped) message(L"Character switched; the previous session is unavailable for this monitor. Other monitors retain their assignments.");
            updateMonitors();
        }
    }
    std::array<HWND,6> monitorChatControls(int id) const
    {
        const auto& m = monitors[id]; return {m.label,m.choices,m.history,m.compose,m.send,m.read};
    }
    Slot* monitorChatSlot(int id) const
    {
        const auto& binding = monitorSet.bindings[id];
        auto* slot = validSlot(binding.slot) ? slots[binding.slot].get() : nullptr;
        return binding.enabled && slot && slot->running() && !slot->detached && slot->pipe.alive() &&
            binding.key.owns(slot->snapshot) && slot->snapshot.state == State::Ready ? slot : nullptr;
    }
    int monitorImageHeight(int id) const
    {
        const auto& m = monitors[id]; RECT r{}; GetClientRect(m.window,&r);
        return (std::max)(0,static_cast<int>(r.bottom)-theme.scale(m.showChat ? 238 : 60,m.dpi));
    }
    void monitorFont(int id)
    {
        auto& m = monitors[id];
        const HFONT next = CreateFontW(-theme.scale(12,m.dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if (!next) return;
        for(auto item:monitorChatControls(id))if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        for(auto item:{m.exchange,m.chatToggle,m.pin,m.options})if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if (m.font) DeleteObject(m.font); m.font = next;
    }
    void createMonitorChat(int id)
    {
        auto& m = monitors[id]; if (m.label) return;
        const auto make = [&m](const wchar_t* type,const wchar_t* text,DWORD style,int command)
        {
            return CreateWindowExW(0,type,text,WS_CHILD|style,0,0,1,1,m.window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)),GetModuleHandleW(nullptr),nullptr);
        };
        m.updatingChat = true;
        m.exchange=make(L"BUTTON",L"Exchange",BS_PUSHBUTTON|WS_TABSTOP,MonitorExchange);
        m.chatToggle=make(L"BUTTON",L"Chat",BS_PUSHBUTTON|WS_TABSTOP,MonitorChatToggle);
        m.pin=make(L"BUTTON",L"Pin",BS_PUSHBUTTON|WS_TABSTOP,PanelPin);
        m.options=make(L"BUTTON",L"Options",BS_PUSHBUTTON|WS_TABSTOP,MonitorOptions);
        m.label = make(L"STATIC",L"Send as: unavailable",SS_LEFT|SS_ENDELLIPSIS|SS_NOPREFIX,0);
        m.choices = make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ChatConversation);
        m.read = make(L"BUTTON",L"Read",BS_PUSHBUTTON|WS_TABSTOP,ChatRead);
        m.history = make(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP,0);
        m.compose = make(L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,ChatCompose);
        m.send = make(L"BUTTON",L"Send",BS_PUSHBUTTON|WS_TABSTOP,ChatSend);
        auto items = std::vector<HWND>{m.label,m.choices,m.history,m.compose,m.send,m.read,m.exchange,m.chatToggle,m.pin,m.options};
        if (std::any_of(items.begin(),items.end(),[](HWND item) { return !item; }))
        {
            for (auto item : items) if (item) DestroyWindow(item);
            m.label = m.choices = m.history = m.compose = m.send = m.read = m.exchange = m.chatToggle = m.pin = m.options = nullptr;
            m.showChat = m.updatingChat = false;
            message(L"Unable to create monitor Chat controls. The read-only image remains available."); return;
        }
        if (m.compose) SendMessageW(m.compose,EM_SETLIMITTEXT,1023,0);
        m.updatingChat = false; monitorFont(id);
        addTooltip(m.exchange,L"Exchange this monitored character with the active world; chat drafts remain bound to their owners");addTooltip(m.pin,L"Keep only this monitor above other windows");
        addTooltip(m.read,L"Mark this character's selected conversation read");
        addTooltip(m.send,L"Send as the character shown above; the active world does not change");
        addTooltip(m.choices,L"Nearby chat and this character's existing conversations");
    }
    void layoutMonitorChat(int id)
    {
        auto& m = monitors[id]; if (!m.label) return;
        const auto scale = [this,&m](int v) { return theme.scale(v,m.dpi); };
        RECT r{}; GetClientRect(m.window,&r);
        const int width = (std::max)(1,static_cast<int>(r.right)-scale(12));
        const int top = monitorImageHeight(id)+scale(60);
        const int header=monitorImageHeight(id)+scale(28);
        for(HWND child:{m.exchange,m.chatToggle,m.pin,m.options})ShowWindow(child,SW_SHOWNOACTIVATE);
        const int cell=(static_cast<int>(r.right)-scale(12))/4;
        MoveWindow(m.exchange,scale(6),header,cell-scale(3),scale(26),TRUE);MoveWindow(m.chatToggle,scale(6)+cell,header,cell-scale(3),scale(26),TRUE);MoveWindow(m.pin,scale(6)+cell*2,header,cell-scale(3),scale(26),TRUE);MoveWindow(m.options,scale(6)+cell*3,header,cell-scale(3),scale(26),TRUE);
        for (auto item : monitorChatControls(id)) if (item) ShowWindow(item,m.showChat ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (m.showChat)
        {
            MoveWindow(m.label,scale(10),top+scale(4),width-scale(4),scale(20),TRUE);
            MoveWindow(m.choices,scale(6),top+scale(26),(std::max)(1,width-scale(62)),scale(180),TRUE);
            MoveWindow(m.read,static_cast<int>(r.right)-scale(62),top+scale(26),scale(56),scale(24),TRUE);
            MoveWindow(m.history,scale(6),top+scale(54),width,scale(82),TRUE);
            MoveWindow(m.compose,scale(6),top+scale(142),(std::max)(1,width-scale(62)),scale(28),TRUE);
            MoveWindow(m.send,static_cast<int>(r.right)-scale(62),top+scale(142),scale(56),scale(28),TRUE);
        }
        InvalidateRect(m.window,nullptr,FALSE);
    }
    void setMonitorChat(int id,bool enabled)
    {
        auto& m = monitors[id]; createMonitorChat(id); if (!m.label) return;
        m.showChat = enabled;
        if (enabled)
        {
            RECT r{}; GetWindowRect(m.window,&r);
            const int width = (std::max)(static_cast<int>(r.right-r.left),theme.scale(320,m.dpi));
            const int height = (std::max)(static_cast<int>(r.bottom-r.top),theme.scale(360,m.dpi));
            SetWindowPos(m.window,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
        else if (IsChild(m.window,GetFocus())) SetFocus(m.window);
        refreshMonitorChat(id); layoutMonitorChat(id);
    }
    void refreshMonitorChat(int id)
    {
        auto& m = monitors[id]; if (!m.label) return;
        auto* slot = monitorChatSlot(id);
        m.updatingChat = true;
        const auto& binding = monitorSet.bindings[id];
        const bool changed = m.chat.bind(binding,slot ? &slot->snapshot : nullptr,slot ? &slot->chat : nullptr);
        const bool restricted = slot && (slot->snapshot.flags & ChatRestricted);
        const bool live = m.showChat && slot && !restricted;
        std::vector<std::string> ids; std::vector<std::wstring> choices;
        if (live) for (const auto& entry : slot->chat.conversations)
        {
            ids.push_back(entry.first);
            auto title = wide(entry.second.title);
            if (entry.second.unread) title += L" · "+std::to_wstring(entry.second.unread)+L" unread";
            choices.push_back(std::move(title));
        }
        auto found = std::find(ids.begin(),ids.end(),m.chat.identity.conversation);
        const int selection = found == ids.end() ? 0 : static_cast<int>(found-ids.begin());
        const auto previous = live ? slot->chat.conversations.find(m.chat.identity.conversation) : std::map<std::string,Conversation>::iterator{};
        const bool privacyChanged = live && previous != slot->chat.conversations.end() && previous->second.restricted;
        if (syncChoices(m.choices,choices,selection,changed || !live || privacyChanged))
        {
            m.conversationIds = std::move(ids);
            if (live && !m.conversationIds.empty()) m.chat.select(binding,slot->snapshot,slot->chat,m.conversationIds[static_cast<std::size_t>(selection)]);
        }
        const auto send = live ? m.chat.request(Kind::SendChat,binding,slot->snapshot,slot->chat) : std::optional<Message>{};
        const auto read = live ? m.chat.request(Kind::MarkRead,binding,slot->snapshot,slot->chat) : std::optional<Message>{};
        const auto current = live ? slot->chat.conversations.find(m.chat.identity.conversation) : std::map<std::string,Conversation>::iterator{};
        const bool blocked = live && current != slot->chat.conversations.end() && current->second.restricted;
        std::wstring label = L"Send as: "+(slot ? characterName(binding.slot) : L"unavailable");
        if (restricted || blocked) label += L" · restricted; use native viewer";
        else if (slot && slot->chat.gap) label += L" · history gap";
        if (label != m.chatStatus) { m.chatStatus = label; SetWindowTextW(m.label,label.c_str()); }
        const auto color = slot ? accountColor(binding.slot) : GetSysColor(COLOR_GRAYTEXT);
        if (color != m.color) { m.color = color; InvalidateRect(m.window,nullptr,FALSE); }
        const bool historyVisible = live && !blocked;
        // The render/status timer must not rebuild four transcripts while no
        // chat changed. Privacy/source changes still clear them immediately.
        if (changed || m.historyVisible != historyVisible || (historyVisible &&
            (m.historyCursor != slot->chat.cursor || m.historyLines != slot->chat.lines.size() || m.historyConversation != m.chat.identity.conversation)))
        {
            std::wstring history;
            if (historyVisible) for (const auto& line : slot->chat.lines) if (line.conversation == m.chat.identity.conversation)
                history += wide(line.sender)+(line.sender.empty() ? L"" : L": ")+wide(line.text)+L"\r\n";
            std::vector<wchar_t> existing(static_cast<std::size_t>(GetWindowTextLengthW(m.history))+1,L'\0');
            GetWindowTextW(m.history,existing.data(),static_cast<int>(existing.size()));
            if (history != existing.data())
            {
                SetWindowTextW(m.history,history.c_str());
                SendMessageW(m.history,EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1)); SendMessageW(m.history,EM_SCROLLCARET,0,0);
            }
            m.historyVisible = historyVisible;
            m.historyCursor = slot ? slot->chat.cursor : 0; m.historyLines = slot ? slot->chat.lines.size() : 0;
            m.historyConversation = m.chat.identity.conversation;
        }
        const auto draft = send ? slot->chat.conversations.at(m.chat.identity.conversation).draft : std::string{};
        if (editText(m.compose) != draft) SetWindowTextW(m.compose,wide(draft).c_str());
        const bool sending = slot && (slot->hasPendingSend || (slot->waiting && slot->request.kind == Kind::SendChat));
        EnableWindow(m.choices,live); EnableWindow(m.compose,send.has_value() && !sending);
        EnableWindow(m.send,send.has_value() && !sending); EnableWindow(m.read,read.has_value());
        m.updatingChat = false;
    }
    void monitorChatCommand(int id,WORD command,WORD notification)
    {
        auto& m = monitors[id]; if (!m.label || !m.showChat || m.updatingChat) return;
        auto* slot = monitorChatSlot(id); if (!slot) { refreshMonitorChat(id); return; }
        const auto& binding = monitorSet.bindings[id];
        if (!m.chat.owns(binding,slot->snapshot,slot->chat)) { refreshMonitorChat(id); return; }
        if (command == ChatConversation && notification == CBN_SELCHANGE)
        {
            const auto selected = SendMessageW(m.choices,CB_GETCURSEL,0,0);
            if (selected >= 0 && static_cast<std::size_t>(selected) < m.conversationIds.size())
                m.chat.select(binding,slot->snapshot,slot->chat,m.conversationIds[static_cast<std::size_t>(selected)]);
            refreshMonitorChat(id);
        }
        else if (command == ChatConversation && notification == CBN_CLOSEUP) refreshMonitorChat(id);
        else if (command == ChatCompose && notification == EN_CHANGE)
        {
            if (m.chat.draft(binding,slot->snapshot,slot->chat,editText(m.compose)))
            {
                if (chatIndex == binding.slot && conversation == m.chat.identity.conversation) refreshChat(true);
                // Other visible editors share the same canonical conversation draft.
                for (int other = 0; other < MaxMonitors; ++other) if (other != id) refreshMonitorChat(other);
            }
        }
        else if (command == ChatSend && notification == BN_CLICKED)
        {
            auto request = m.chat.request(Kind::SendChat,binding,slot->snapshot,slot->chat);
            if (request) { request->text = editText(m.compose); queueChat(binding.slot,std::move(*request)); }
        }
        else if (command == ChatRead && notification == BN_CLICKED)
        {
            const auto request = m.chat.request(Kind::MarkRead,binding,slot->snapshot,slot->chat);
            if (request && !send(binding.slot,Kind::MarkRead,Mode::Warm,0,&*request)) message(L"Viewer is busy; try Mark read again.");
        }
    }
    void updateMonitors()
    {
        for (int id = 0; id < MaxMonitors; ++id)
        {
            auto& monitor = monitors[id]; auto& binding = monitorSet.bindings[id];
            if (!monitor.window) continue;
            refreshMonitorChat(id);
            if (!binding.enabled)
            {
                clearMonitor(id);
                if (monitor.dirty)
                {
                    SetWindowTextW(monitor.window, L"Character monitor · session ended or stopped; choose again");
                    monitor.title.clear();monitor.previewStatus=L"Session ended or stopped"; InvalidateRect(monitor.window, nullptr, FALSE); monitor.dirty = false;
                }
                continue;
            }
            const auto* slot = validSlot(binding.slot) ? slots[binding.slot].get() : nullptr;
            std::wstring status=L"Read-only preview";
            std::wstring title = L"Monitor: ";
            title += slot && !(slot->snapshot.flags & ChatRestricted) && binding.key.owns(slot->snapshot) && !slot->snapshot.name.empty() ? characterName(binding.slot) : L"unavailable";
            title += monitor.showChat ? L" · read-only image + chat" : L" · read only";
            if (!slot || !slot->running() || slot->detached || !slot->pipe.alive() || !binding.key.owns(slot->snapshot))
            { stopMonitor(id); title += L" · session ended; choose again";status=L"Session ended · choose again"; }
            else if (slot->snapshot.state != State::Ready) { stopMonitor(id); title += L" · disconnected";status=L"Disconnected"; }
            else if (slot->snapshot.flags & (PreviewUnavailable | ChatRestricted))
            { clearMonitor(id);status=(slot->snapshot.flags&ChatRestricted)?L"Restricted":L"Preview unavailable";title+=L" · "+status; }
            else if (binding.slot == active || busy()) { clearMonitor(id);status=L"Paused while active or switching";title+=L" · "+status; }
            else if (!wantsMonitor(id)){status=L"Paused while minimized";title+=L" · "+status;}
            else
            {
                Message frame;
                if (slots[binding.slot]->preview.read(slot->snapshot, monitor.frame.event, frame, monitor.pixels))
                { monitor.frame = std::move(frame); monitor.dirty = true; }
                const auto effective = previewBudget(binding.rate, visibleMonitors());
                const auto rate = [](std::uint32_t units) { return units == 1 ? std::wstring(L"0.5") : std::to_wstring(units / 2); };
                if(experience.diagnostics)title += L" · target " + rate(previewTarget(binding.rate)) + L" FPS";
                if (experience.diagnostics && effective != previewTarget(binding.rate)) title += L" · budget " + rate(effective) + L" FPS";
                if (monitor.pixels.empty()){status=L"Waiting for first frame";title+=L" · "+status;}
                else
                {
                    const auto age = GetTickCount64() >= monitor.frame.cursor ? GetTickCount64() - monitor.frame.cursor : 0;
                    if(experience.diagnostics)title += L" · " + std::to_wstring(age / 1000) + L"s old";
                    if (age > 5000){status=L"Stale preview · "+std::to_wstring(age/1000)+L"s old";title+=L" · stale";}
                }
            }
            if(monitor.previewStatus!=status){monitor.previewStatus=status;monitor.dirty=true;}
            if (monitor.title != title) { monitor.title = title; SetWindowTextW(monitor.window, title.c_str()); }
            if (monitor.dirty) { InvalidateRect(monitor.window, nullptr, FALSE); monitor.dirty = false; }
        }
    }
    std::string editText(HWND editor = nullptr) const
    {
        if (!editor) editor = chatCompose;
        const int length = (std::min)(1023, GetWindowTextLengthW(editor));
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(editor, value.data(), length + 1);
        value.resize(static_cast<std::size_t>(length));
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr, nullptr);
        if (count <= 0) return {};
        std::string result(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, result.data(), count, nullptr, nullptr);
        return result;
    }
    Message boundChat(Kind kind) const
    {
        Message request;
        if (!slots[chatIndex]) return request;
        const auto& view = slots[chatIndex]->chat;
        request.worker = view.identity.worker; request.generation = view.identity.generation;
        request.account = view.identity.account; request.grid = view.identity.grid; request.conversation = conversation;
        const auto found = view.conversations.find(conversation);
        if (found != view.conversations.end()) request.topic = found->second.topic;
        request.kind = kind;
        return request;
    }
    void saveDraft(bool stopTyping)
    {
        if (!slots[chatIndex]) return;
        auto& slot = *slots[chatIndex];
        auto found = slot.chat.conversations.find(conversation);
        if (found != slot.chat.conversations.end() && !found->second.restricted && !(slot.snapshot.flags & ChatRestricted)) found->second.draft = editText();
        if (stopTyping && !conversation.empty())
        {
            slot.pendingTyping = boundChat(Kind::Typing); slot.pendingTyping.unread = 0;
            slot.hasPendingTyping = true;
        }
    }
    void refreshChat(bool loadDraft = false, bool forceChoices = false)
    {
        if (!chatAccount) return;
        updatingChat = true;
        std::vector<std::wstring> accounts;
        for (int i = 0; i < MaxCharacters; ++i)
        {
            const std::wstring name = slots[i] && !slots[i]->snapshot.name.empty() ? characterName(i) : L"Character " + std::to_wstring(i + 1);
            accounts.push_back(name);
        }
        syncChoices(chatAccount, accounts, chatIndex);
        auto* slot = slots[chatIndex].get();
        const bool ready = slot && !slot->detached && slot->pipe.alive() && slot->snapshot.state == State::Ready;
        const bool restricted = slot && (slot->snapshot.flags & ChatRestricted);
        const auto selectedChat = slot ? slot->chat.conversations.find(conversation) : std::map<std::string,Conversation>::iterator{};
        bool conversationBlocked = slot && selectedChat != slot->chat.conversations.end() && selectedChat->second.restricted;
        std::wstring label = L"Send as: " + (slot && !slot->snapshot.name.empty() ? characterName(chatIndex) : L"not logged in");
        if (!ready) label += L" · unavailable";
        else if (restricted) label += L" · shared chat restricted; use native viewer";
        else if (conversationBlocked) label += L" · conversation restricted; use native viewer";
        else if (slot->chat.gap) label += L" · history gap; native history has more detail";
        SetWindowTextW(chatLabel, label.c_str());
        const auto senderTip=slot && ready && !restricted?L"Send as: "+wide(slot->snapshot.name)+L" · "+wide(slot->snapshot.grid):std::wstring(L"Sender unavailable or restricted");
        if(chatSenderTip!=senderTip){chatSenderTip=senderTip;addTooltip(chatLabel,chatSenderTip.c_str());}
        const auto color = accountColor(chatIndex);
        if (accentSlot != chatIndex || accentColor != color)
        { accentSlot = chatIndex; accentColor = color; InvalidateRect(chatAccent,nullptr,FALSE); }
        if (loadDraft)
        {
            std::vector<std::string> ids; std::vector<std::wstring> choices;
            if (slot) for (const auto& entry : slot->chat.conversations)
            {
                ids.push_back(entry.first);
                std::wstring name = wide(entry.second.title);
                if (entry.second.unread) name += L" · " + std::to_wstring(entry.second.unread) + L" unread";
                choices.push_back(std::move(name));
            }
            const auto found = std::find(ids.begin(), ids.end(), conversation);
            const int selection = found == ids.end() ? 0 : static_cast<int>(found - ids.begin());
            if (syncChoices(chatConversation, choices, selection, forceChoices || restricted || !ready))
            {
                conversationIds = std::move(ids);
                if (!conversationIds.empty()) conversation = conversationIds[static_cast<std::size_t>(selection)];
            }
            const auto draft = slot ? slot->chat.conversations.find(conversation) : std::map<std::string, Conversation>::iterator{};
            conversationBlocked = slot && draft != slot->chat.conversations.end() && draft->second.restricted;
            const std::string wanted = slot && ready && !restricted && !conversationBlocked && draft != slot->chat.conversations.end() ? draft->second.draft : "";
            if (editText() != wanted) SetWindowTextW(chatCompose, wide(wanted).c_str());
        }
        std::wstring history;
        if (slot && !restricted && !conversationBlocked) for (const auto& line : slot->chat.lines)
            if (line.conversation == conversation)
            { history += wide(line.sender) + (line.sender.empty() ? L"" : L": ") + wide(line.text) + L"\r\n"; }
        std::vector<wchar_t> existing(static_cast<std::size_t>(GetWindowTextLengthW(chatHistory)) + 1, L'\0');
        GetWindowTextW(chatHistory, existing.data(), static_cast<int>(existing.size()));
        if (history != existing.data())
        {
            SetWindowTextW(chatHistory, history.c_str());
            SendMessageW(chatHistory, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            SendMessageW(chatHistory, EM_SCROLLCARET, 0, 0);
        }
        const bool sending = slot && (slot->hasPendingSend || (slot->waiting && slot->request.kind == Kind::SendChat));
        const auto selected = slot ? slot->chat.conversations.find(conversation) : std::map<std::string, Conversation>::iterator{};
        const bool isNotice = slot && selected != slot->chat.conversations.end() && selected->second.topic == Topic::Notice;
        EnableWindow(chatCompose, ready && !restricted && !conversationBlocked && !sending && !isNotice);
        EnableWindow(chatSend, ready && !restricted && !conversationBlocked && !sending && !isNotice);
        EnableWindow(chatReview, ready); EnableWindow(chatRead, ready && !restricted && !conversationBlocked);
        updatingChat = false;
    }
    void composeChanged()
    {
        if (updatingChat || !slots[chatIndex]) return;
        saveDraft(false);
        if (!conversation.empty())
        {
            auto& slot = *slots[chatIndex];
            // Do not replace a pending stop with typing in another conversation.
            if (!slot.hasPendingTyping || slot.pendingTyping.unread != 0 || slot.pendingTyping.conversation == conversation)
            {
                slot.pendingTyping = boundChat(Kind::Typing); slot.pendingTyping.unread = editText().empty() ? 0u : 1u;
                slot.hasPendingTyping = true;
            }
        }
        for (int id = 0; id < MaxMonitors; ++id) refreshMonitorChat(id);
    }
    void queueChat(int index, Message request)
    {
        if (!validSlot(index) || !slots[index]) return;
        auto& slot = *slots[index];
        if (request.kind != Kind::SendChat || !slot.running() || slot.detached || !slot.pipe.alive() || !chatRequestAllowed(slot.chat,slot.snapshot,request))
        { message(L"The chat account/session is unavailable or restricted. The draft was not sent."); return; }
        if (slot.hasPendingSend || (slot.waiting && slot.request.kind == Kind::SendChat)) return;
        if (request.text.empty() || request.text.size() > 1023 || !validUtf8(request.text, true))
        { message(L"Use a message of 1 to 1023 UTF-8 bytes."); return; }
        slot.pendingSend = std::move(request); slot.hasPendingSend = true;
        message(L"Sending as " + characterName(index) + L" through this character's native chat…");
        refreshChat(); updateMonitors();
    }
    void sendChat()
    {
        Message request = boundChat(Kind::SendChat); request.text = editText();
        queueChat(chatIndex,std::move(request));
    }
    void restartCharacter(int index)
    {
        if (!validSlot(index)) return;
        if (busy()) { message(L"Finish the current switch or close action before restarting."); return; }
        if (!slots[index] || !slots[index]->running()) { launch(index); return; }
        auto& slot = *slots[index];
        if (slot.detached || !slot.pipe.alive())
        { message(L"Close this character's separate viewer normally, then use + to open a fresh login. Other characters remain connected."); return; }
        if (!slot.restart.begin(slot.snapshot, GetTickCount64()))
        { message(L"Character is changing login or already restarting; finish its native window first."); return; }
        lifecycle.beginManagement();
        slot.hasPendingSend = slot.hasPendingTyping = slot.hasPendingAction = false;
        if (const int id = monitorSet.find(index); id >= 0) stopMonitor(id);
        message(L"Restarting " + wide(slot.snapshot.name) + L": confirm native close. Waiting for process/profile release before a fresh login; queued messages are not replayed.");
    }
    std::filesystem::path mainSettingsFolder() const
    {
        wchar_t* roaming = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roaming)) || !roaming) return {};
#if defined(_WIN64)
        const auto path = std::filesystem::path(roaming)/L"Folderstorm_x64"/L"user_settings";
#else
        const auto path = std::filesystem::path(roaming)/L"Folderstorm"/L"user_settings";
#endif
        CoTaskMemFree(roaming); return path;
    }
    void copyProfileSettings(int index)
    {
        if (!validSlot(index) || busy() || (slots[index] && slots[index]->running()))
        { message(L"Close the selected character's viewer before copying preferences into its profile."); return; }
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
        { message(L"Unable to open the settings-folder picker."); return; }
        DWORD options = 0;
        if (FAILED(dialog->GetOptions(&options)) ||
            FAILED(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR)))
        { dialog->Release(); message(L"Unable to configure the settings-folder picker."); return; }
        dialog->SetTitle(L"Choose the main viewer's user_settings folder (or a settings backup)");
        const auto initial = mainSettingsFolder(); IShellItem* start = nullptr;
        if (!initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&start))))
        { dialog->SetFolder(start); start->Release(); }
        std::filesystem::path source;
        if (SUCCEEDED(dialog->Show(window)))
        {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)))
            {
                wchar_t* path = nullptr;
                if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH,&path))) { source = path; CoTaskMemFree(path); }
                result->Release();
            }
        }
        dialog->Release(); if (source.empty()) return;
        std::error_code error;
        const auto file = source/L"settings.xml";
        if (!std::filesystem::is_regular_file(file,error) || error || std::filesystem::file_size(file,error) > ProfileSettingsLimit || error)
        { message(L"Choose a folder containing a saved settings.xml file (up to 4 MiB). Save/close the main viewer first for its latest preferences."); return; }
        const auto destination = profiles/(L"Character"+std::to_wstring(index+1));
        const auto text = L"Copy graphics, interface, colors and key bindings from:\n"+source.wstring()+
            L"\n\nInto:\n"+destination.wstring()+
            L"\n\nThis opens native login without logging in automatically. Existing preferences receive a backup before replacement. Credentials, account data, chats, caches and local assistant permissions stay separate. Use the main viewer's last saved preferences; close it first for recent changes.";
        if (MessageBoxW(window,text.c_str(),L"Copy preferences into this character profile?",MB_OKCANCEL|MB_ICONQUESTION) != IDOK) return;
        launch(index,source);
    }
    void launch(int index, const std::filesystem::path& settingsSource = {})
    {
        if (!validSlot(index) || busy()) { message(L"Finish the current switch or close action before launching."); return; }
        if (slots[index] && slots[index]->running()) { message(L"This slot still has a viewer open. Close that viewer before relaunching."); return; }
        std::error_code error;
        if (!std::filesystem::is_regular_file(viewer, error) || error) { message(L"Viewer executable missing. Keep this controller beside the matching viewer."); return; }
        auto slot = std::make_unique<Slot>();
        if (BCryptGenRandom(nullptr, slot->id.data(), static_cast<ULONG>(slot->id.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        { message(L"Unable to create the private session identity."); return; }
        const auto profile = profiles / (L"Character" + std::to_wstring(index + 1));
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
        if (!slot->preview.create(slot->id, GetCurrentProcessId())) { message(L"Unable to create the bounded preview frame lane."); return; }
        auto childEnvironment = environment(profile, pair.client.value, lease.value, slot->preview.mapping(), slot->preview.mutex(), slot->id, settingsSource);
        if (childEnvironment.empty()) { message(L"Unable to prepare the worker environment."); return; }
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        std::vector<std::uint8_t> attributes(attributeBytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) { message(L"Unable to restrict inherited handles."); return; }
        HANDLE inherited[] = {pair.client.value, lease.value, slot->preview.mapping(), slot->preview.mutex()};
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
        lifecycle.beginManagement();
        message(L"Log in a different character in each viewer. The first ready character becomes active.");
    }
    bool busy() const
    {
        if (handoff.step() != Handoff::Step::Idle || detaching) return true;
        for (const auto& slot : slots) if (slot && slot->restart.phase() != Restart::Phase::Idle && !slot->detached && slot->running()) return true;
        return false;
    }
    bool waitingForLostOwner(int target) const
    {
        for (int index = 0; index < MaxCharacters; ++index) if (index != target && slots[index] && !slots[index]->pipe.alive())
            if (auto surface = slots[index]->surface())
                if (GetPropW(surface, InputLeaseProperty) == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(GetCurrentProcessId()))) return true;
        return false;
    }
    void switchTo(int index)
    {
        if (!validSlot(index) || busy() || !slots[index] || slots[index]->detached || !slots[index]->pipe.alive()) return;
        if (slots[index]->snapshot.state != State::Ready) { message(L"Finish login before switching this character."); return; }
        if (index == active)
        {
            if (embedding) { SetForegroundWindow(window); focusRequested = false; }
            else if (auto surface = slots[index]->surface()) SetForegroundWindow(surface);
            return;
        }
        const auto oldGeneration = active >= 0 && slots[active] ? slots[active]->snapshot.generation : 0;
        if (handoff.begin(active, index, oldGeneration, slots[index]->snapshot.generation, active < 0 ? Mode::Warm : standby[active]))
        {
            monitorSet.beginExchange(active, index, validSlot(active) && slots[active] ? &slots[active]->snapshot : nullptr, slots[index]->snapshot);
            BOOL animation = TRUE;
            const bool reduced = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) && !animation;
            handoffStyle = presentation.transition; handoffDuration = presentation.duration; handoffHeight = presentation.height;
            handoffAnimated = handoffStyle != 0 && !reduced; escapeHeld = false;
            message(L"Switching to " + wide(slots[index]->snapshot.name) + L"; waiting for its ready frame…");
        }
    }
    void skipTransition()
    {
        handoffAnimated = false;
        if (handoff.step() == Handoff::Step::Idle) return;
        const int index = handoff.worker();
        if (!validSlot(index) || !slots[index]) return;
        auto& slot = *slots[index];
        if (!slot.waiting || slot.request.kind != Kind::SetMode) return;
        slot.pendingSkip = slot.request; slot.pendingSkip.kind = Kind::CancelTransition;
        slot.pendingSkip.cursor = slot.request.sequence; slot.hasPendingSkip = true;
    }
    void failHandoff(int index, const std::wstring& error)
    {
        // Closing an uncertain promotion's capability prevents a delayed grant
        // from remaining a managed owner when the old worker is restored.
        if (handoff.step() == Handoff::Step::Promote && slots[index])
        { slots[index]->pipe.close(); slots[index]->detached = true; }
        for (auto& slot : slots) if (slot) slot->hasPendingReview = false;
        handoff.fail();
        finishMonitorExchange(false);
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
        if (!slot.importReported && response.detail.rfind("Settings import:",0) == 0)
        { slot.importReported = true; message(wide(response.detail)); }
        const bool changedSession = !slot.chat.identity.owns(response);
        if (changedSession) attention.forget(slot.chat.identity);
        slot.chat.bind(response);
        if (changedSession)
        {
            if (slot.restart.phase() != Restart::Phase::Idle && !slot.restart.owns(response))
            { slot.restart.cancel(); message(L"Login changed; restart cancelled without replaying a close on the new session."); }
            slot.hasPendingSend = slot.hasPendingTyping = false; slot.catalogIndex = 0; slot.catalogAt = slot.eventsAt = 0;
            slot.hasPendingAction = false; slot.hasPendingReview = false; slot.workspace.clear(); slot.workspaceModified = slot.preferencesOpen = false; slot.workspaceAt = 0;
            slot.previewWidth = slot.previewHeight = slot.previewRate = 0; slot.shortcutPolicy = -1;
            if (chatIndex == index) { conversation.clear(); refreshChat(true, true); }
        }
        slot.snapshot = response;
        const auto profileKey = AccountKey::from(response);
        if (response.state == State::Ready && profileKey.valid() && !presentation.accounts.count(profileKey))
        {
            auto value = presentation.appearance(profileKey);
            const std::uint32_t palette[] = {0x247ac0,0xa543a6,0x17845c,0xb36016,0x635dc0};
            for (const auto color : palette)
            {
                bool used = false;
                for (int peer = 0; peer < MaxCharacters; ++peer) if (peer != index && slots[peer] && AccountKey::from(slots[peer]->snapshot).valid() && appearance(peer).color == color) used = true;
                if (!used) { value.color = color; break; }
            }
            presentation.set(profileKey,value);
        }
        if (request.kind == Kind::WorkspaceInfo && !(response.flags & Error))
        {
            slot.workspace = response.title; slot.workspaceModified = (response.unread & 1) != 0;
            slot.preferencesOpen = (response.unread & 2) != 0; slot.workspaceAt = GetTickCount64();
        }
        if (response.flags & ChatRestricted)
        {
            attention.forget(slot.chat.identity); slot.hasPendingReview = false;
            slot.chat.lines.clear(); slot.chat.conversations.clear(); slot.chat.cursor = 0;
            slot.chat.conversations.emplace("", Conversation{"", "Nearby chat", "", Topic::Nearby, 0});
            if (chatIndex == index) { conversation.clear(); refreshChat(true, true); }
        }
        if (!(response.flags & Error) && (request.kind == Kind::Events || request.kind == Kind::Conversations))
        {
            const bool accepted = slot.chat.accept(response);
            if (accepted)
            {
                attention.accept(response); alert(response,index);
                if (validSwitchIntent(response,slot.snapshot,index,active,GetTickCount64(),presentation.shortcuts,busy())) dispatchShortcut(response.unread);
                else if(validShellIntent(response,slot.snapshot,index,active,GetTickCount64(),experience.viewerShellKeys)){if(response.unread==7)recover();else navigateAttention(response.unread==8?1:-1);}
            }
            if (request.kind == Kind::Events) slot.eventsAt = GetTickCount64();
            else if (response.eventType == EventType::None) { slot.catalogIndex = 0; slot.catalogAt = GetTickCount64(); }
            else ++slot.catalogIndex;
            if (index == chatIndex)
            {
                const bool catalogChanged = response.eventType == EventType::Conversation || response.eventType == EventType::Chat || response.eventType == EventType::Notice || response.eventType == EventType::Gap;
                refreshChat(catalogChanged);
            }
        }
        if (request.kind == Kind::ShortcutPolicy && !(response.flags & Error)) slot.shortcutPolicy = static_cast<int>(request.unread);
        if (request.kind == Kind::AudioPolicy && !(response.flags & Error)) slot.audioPolicy = request.unread;
        if (request.kind == Kind::MonitorPolicy && !(response.flags & Error))
        { slot.previewWidth = request.width; slot.previewHeight = request.height; slot.previewRate = request.unread; }
        if (request.kind == Kind::MarkRead && !(response.flags & Error))
        {
            if (slot.chat.markRead(request) && index == chatIndex)
            { refreshChat(true); }
        }
        if (request.kind == Kind::SendChat)
        {
            if (response.flags & Error) message(L"Send as " + wide(request.name.empty() ? slot.snapshot.name : request.name) + L": " + wide(response.detail));
            else
            {
                if (acknowledgeChatDraft(slot.chat,request) && index == chatIndex && request.conversation == conversation)
                { updatingChat = true; SetWindowTextW(chatCompose,L""); updatingChat = false; }
                message(L"Message handed to the selected character's native transport; remote delivery is not confirmed.");
            }
        }
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
        if ((request.kind == Kind::Detach || request.kind == Kind::Quit) && !(response.flags & (Error | Embedded)))
        {
            slot.detached = true; slot.pipe.close();
            if (active == index) active = -1;
        }
        if (response.flags & Error)
        {
            if (request.kind == Kind::Detach || request.kind == Kind::Quit)
            { detaching = closing = logoutOnClose = false; slot.restart.cancel(); }
            if (request.kind == Kind::Embed || request.kind == Kind::Unembed)
            {
                embedding = (response.flags & Embedded) != 0;
                SendMessageW(embedControl, BM_SETCHECK, embedding ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (request.kind == Kind::MonitorPolicy) { const int id = monitorSet.find(index); if (id >= 0) stopMonitor(id); }
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
                lifecycle.completedHandoff(active);
                SendMessageW(characterControl, CB_SETCURSEL, lifecycle.selection(), 0);
                finishMonitorExchange(step != Handoff::Step::Rollback);
                message(step == Handoff::Step::Rollback ? L"Returned to the previous character." : L"Character ready. Other characters use their chosen standby mode.");
                if (!response.detail.empty()) message(L"Character ready. " + wide(response.detail));
                if (embedding)
                {
                    // Keep shell controls focused; world clicks use native focus.
                    focusRequested = false;
                }
                else if (auto surface = slots[active]->surface()) SetForegroundWindow(surface);
            }
            else active = -1; // Old input owner has explicitly acknowledged revoke.
        }
    }
    void describe(int index)
    {
        if (!slots[index]) { descriptions[index] = L"Character " + std::to_wstring(index + 1) + L": not launched"; SetWindowTextW(status[index], descriptions[index].c_str());SetWindowTextW(cardButtons[index],descriptions[index].c_str()); return; }
        const auto& slot = *slots[index];
        std::wostringstream text;
        text << (std::to_wstring(index + 1) + L": ") << (slot.snapshot.name.empty() ? L"Login" : characterName(index));
        text << (!slot.running() ? L" · closed" : slot.detached ? L" · separate viewer" : !slot.pipe.alive() ? L" · control lost" : slot.snapshot.state == State::Disconnected ? L" · disconnected" :
            slot.snapshot.state != State::Ready ? L" · connecting" : slot.snapshot.mode == Mode::Warm ? L" · warm" :
                slot.snapshot.mode == Mode::Economy ? L" · economy (experimental)" : L" · active");
        if (experience.diagnostics && (slot.snapshot.flags & EconomyTrimmed)) text << L" · render targets released";
        if (experience.diagnostics && (slot.snapshot.flags & VoiceOwner)) text << L" · voice owner";
        if (!slot.workspace.empty()) text << L" · workspace " << wide(slot.workspace) << (slot.workspaceModified ? L" *" : L"");
        if (slot.preferencesOpen) text << L" · Preferences open";
        std::uint64_t unread = 0; for (const auto& entry : slot.chat.conversations) if(!entry.second.restricted)unread += entry.second.unread;
        if (unread) text << L" · " << unread << L" unread";
        if (experience.diagnostics && slot.pipe.alive() && !slot.detached && slot.snapshot.state == State::Ready && slot.snapshot.mode == Mode::Active)
            text << ((slot.snapshot.flags & ClientFocused) ? L" · keyboard focused" : L" · keyboard unfocused");
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
        if (experience.diagnostics && slot.running() && GetProcessMemoryInfo(slot.process.value, &memory, sizeof(memory)))
            text << L" · " << memory.WorkingSetSize / (1024 * 1024) << L" MiB";
        if(experience.diagnostics)text << std::fixed << std::setprecision(1) << L" · " << slot.fps << L" draws/s · " << slot.loops << L" loops/s";
        const auto description = text.str();
        if (descriptions[index] != description) { descriptions[index] = description; SetWindowTextW(status[index],descriptions[index].c_str());SetWindowTextW(cardButtons[index],descriptions[index].c_str()); }
        if (index == active)
        {
            std::wstring summary = characterName(index)+L" · active · "+std::to_wstring(unread)+L" unread";
            SetWindowTextW(compactInfo, summary.c_str());
        }
    }
    void tick()
    {
        DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        bool ownForeground = foregroundPid == GetCurrentProcessId();
        for (const auto& slot : slots) if (slot && slot->pid == foregroundPid) ownForeground = true;
        const bool escape = ownForeground && (GetAsyncKeyState(VK_ESCAPE) & 0x8000);
        if (escape && !escapeHeld && busy()) skipTransition();
        escapeHeld = escape;
        for (int index = 0; index < MaxCharacters; ++index)
        {
            if (!slots[index]) continue;
            auto& slot = *slots[index];
            const auto tag = static_cast<std::uintptr_t>(slot.restart.sequence());
            const bool cancelHint = slot.restart.phase() == Restart::Phase::Waiting && slot.surface() &&
                GetPropW(slot.surface(), L"FolderstormRestartCancelled") == reinterpret_cast<HANDLE>(tag ? tag : 1);
            const auto restartResult = lifecycle.pollRestart(slot.restart, slot.running(), cancelHint, GetTickCount64(), !busy());
            if (restartResult == Restart::Result::Launch) { launch(index); continue; }
            if (restartResult == Restart::Result::Cancelled) message(L"Native close cancelled; restart stopped. The old viewer remains available.");
            if (restartResult == Restart::Result::TimedOut) message(L"Restart stopped waiting. Finish closing the old viewer, then choose +; no forced termination or automatic retry.");
            Message response;
            if (slot.pipe.receive(response)) reply(index, response);
            if (slot.hasPendingSkip)
            {
                if (!slot.waiting || slot.request.kind != Kind::SetMode || slot.request.sequence != slot.pendingSkip.cursor ||
                    !slot.chat.identity.owns(slot.pendingSkip)) slot.hasPendingSkip = false;
                else if (!slot.pipe.writing())
                {
                    slot.pendingSkip.sequence = ++slot.nextSequence;
                    slot.pipe.send(slot.pendingSkip); // No reply/retry; pending SetMode still owns its response.
                    slot.hasPendingSkip = false;
                }
            }
            if (!slot.running() || !slot.pipe.alive())
            {
                attention.forget(slot.chat.identity);
                if (slot.hasPendingSend || (slot.waiting && slot.request.kind == Kind::SendChat))
                    message(L"Chat connection lost: delivery is uncertain. Text was not retried; check the owning character's native chat before sending again.");
                slot.hasPendingSend = slot.hasPendingTyping = false;
                slot.hasPendingAction = false; slot.hasPendingReview = false;
                slot.waiting = false;
                if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index) failHandoff(index, L"Character connection lost. Other connected characters can still be selected.");
                if (active == index) active = -1;
            }
            if (slot.waiting && GetTickCount64() - slot.sentAt > (slot.snapshot.state == State::Ready ? 12000ULL : 60000ULL))
            {
                slot.pipe.close(); slot.waiting = false; slot.detached = true;
                if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index) failHandoff(index, L"The viewer did not respond; returning it to an ordinary window.");
                else if (slot.request.kind == Kind::SendChat)
                    message(L"Chat send timed out: delivery is uncertain and will not be retried. Check the native conversation; the watchdog restores an ordinary window.");
                else message(L"The viewer did not respond; its local watchdog will return it to an ordinary window.");
            }
            if (!slot.waiting && slot.pipe.alive() && !slot.detached)
            {
                if (detaching)
                {
                    if (logoutOnClose)
                    {
                        if (!slot.chat.identity.owns(slot.closingAction))
                        { detaching = closing = logoutOnClose = false; message(L"A login changed before logout. No action was retried on the new session."); }
                        else send(index, Kind::Quit, Mode::Warm, 0, &slot.closingAction);
                    }
                    else send(index, Kind::Detach);
                }
                else if (slot.restart.phase() == Restart::Phase::Queued)
                {
                    if (!slot.restart.owns(slot.snapshot))
                    { slot.restart.cancel(); message(L"Character changed; restart cancelled."); }
                    else
                    {
                        auto action = slot.restart.source(); action.kind = Kind::Quit; action.unread = 1;
                        if (send(index, Kind::Quit, Mode::Warm, 0, &action)) slot.restart.sent(slot.request.sequence);
                    }
                }
                else if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index)
                {
                    if (handoff.generation() != slot.snapshot.generation) failHandoff(index, L"Login changed while switching.");
                    else if (handoff.mode() == Mode::Active && waitingForLostOwner(index))
                    {
                        message(L"Waiting for the disconnected controller link to release its old input owner…");
                        if (GetTickCount64() - slot.pollAt >= 1000) send(index, Kind::Poll);
                    }
                    else send(index, Kind::SetMode, handoff.mode(), embedding && handoff.mode() == Mode::Active ?
                        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(viewport)) : 0);
                }
                else if (slot.snapshot.flags & LoginPending)
                {
                    const bool collision = loginCollisionAny(slot.snapshot, index, [this](int other)
                    {
                        const auto* peer = slots[other].get();
                        return LoginPeer{peer ? &peer->snapshot : nullptr, peer ? &peer->reservation : nullptr, peer && peer->running()};
                    });
                    if (send(index, collision ? Kind::DenyLogin : Kind::PermitLogin))
                    {
                        if (!collision) slot.reservation = slot.snapshot.name;
                        else message(L"That character is already reserved. Use a different character in this viewer.");
                    }
                }
                else if (index == active && slot.snapshot.state == State::Ready &&
                    (embedding != ((slot.snapshot.flags & Embedded) != 0) ||
                        (!embedding && (slot.snapshot.flags & HostedStyle))))
                {
                    send(index, embedding ? Kind::Embed : Kind::Unembed, Mode::Active,
                        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(viewport)));
                }
                else if (index == active && embedding && focusRequested &&
                    slot.snapshot.state == State::Ready && (slot.snapshot.flags & Embedded) &&
                    GetForegroundWindow() == window && !IsIconic(window) &&
                    GetPropW(window, L"FolderstormViewportFocusIntent"))
                {
                    if (send(index, Kind::Focus)) focusRequested = false;
                }
                else if (!busy() && index != active && slot.snapshot.state == State::Ready && slot.snapshot.mode != standby[index])
                    send(index, Kind::SetMode, standby[index]);
                else if (slot.snapshot.state == State::Ready && slot.audioPolicy != ((voice ? 1u : 0u) | (muteBackground ? 2u : 0u)))
                    send(index, Kind::AudioPolicy);
                else if (slot.snapshot.state == State::Ready && slot.shortcutPolicy != static_cast<int>(shortcutPolicy()))
                    send(index,Kind::ShortcutPolicy);
                else if (slot.hasPendingReview && index == active && !busy())
                {
                    const auto action = slot.pendingReview; slot.hasPendingReview = false;
                    send(index,action.kind,Mode::Warm,0,&action);
                }
                else if (slot.hasPendingAction)
                {
                    const auto action = slot.pendingAction; slot.hasPendingAction = false;
                    if(action.kind==Kind::WorkspaceMenu && index!=active)message(L"Workspace opening was cancelled because its character did not become active.");
                    else send(index, action.kind, Mode::Warm, 0, &action);
                }
                else if (slot.hasPendingSend)
                {
                    const auto pending = slot.pendingSend; slot.hasPendingSend = false;
                    if (!chatRequestAllowed(slot.chat,slot.snapshot,pending))
                        message(L"Chat eligibility changed before sending. The original draft was not sent or retried.");
                    else if (!send(index, Kind::SendChat, Mode::Warm, 0, &pending))
                        message(L"Send was not acknowledged. Text remains in its original draft and was not retried.");
                }
                else if (slot.hasPendingTyping && (slot.pendingTyping.unread == 0 || GetTickCount64() - slot.typingAt >= 1000))
                {
                    const auto pending = slot.pendingTyping; slot.hasPendingTyping = false;
                    if (send(index, Kind::Typing, Mode::Warm, 0, &pending)) slot.typingAt = GetTickCount64();
                }
                else
                {
                    const auto policy = monitorPolicy(index);
                    if (slot.snapshot.state == State::Ready && (slot.previewWidth != policy.width || slot.previewHeight != policy.height || slot.previewRate != policy.unread) && admitMonitorPolicy(index, policy))
                        send(index, Kind::MonitorPolicy);
                    else
                    {
                        const auto kind = slot.periodic.next(slot.snapshot.state == State::Ready, (slot.snapshot.flags & ChatRestricted) != 0,
                            GetTickCount64(), slot.eventsAt, slot.catalogAt, slot.workspaceAt, slot.pollAt);
                        if (kind != Kind::Status) send(index, kind);
                    }
                }
            }
            describe(index);

        }
        bool canLaunch = false;
        for (int i = 0; i < MaxCharacters; ++i)
        {
            if (!slots[i]) describe(i);
            if (!slots[i] || !slots[i]->running()) canLaunch = true;
        }
        EnableWindow(launchButton, canLaunch && !busy());
        pollSessionSet();
        refreshChat();
        refreshPins(); refreshAttention();
        refreshCharacters();
        std::uint64_t signature=static_cast<std::uint64_t>(lifecycle.selection()+1);for(int i=0;i<MaxCharacters;++i)if(slots[i])signature=signature*131+static_cast<unsigned int>(slots[i]->snapshot.state)*7+static_cast<unsigned int>(slots[i]->restart.phase())+(slots[i]->pipe.alive()?0u:100u);
        if(signature!=layoutState){layoutState=signature;layout();}
        if (active < 0) SetWindowTextW(compactInfo, L"Choose a character · details in Session");
        updateMonitors();
        if (!busy() && active < 0 && selectFirst)
            for (int i = 0; i < MaxCharacters; ++i) if (slots[i] && slots[i]->pipe.alive() && !slots[i]->detached && slots[i]->snapshot.state == State::Ready) { switchTo(i); selectFirst = false; break; }
        if (detaching)
        {
            bool complete = true;
            for (const auto& slot : slots) if (slot)
            {
                if (slot->pipe.alive() && !slot->detached) complete = false;
                if (logoutOnClose && slot->running()) complete = false;
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
    void detach(bool close, bool logout = false)
    {
        setOpen.cancel();
        for (auto& slot : slots) if (slot) slot->hasPendingReview = false;
        lifecycle.stopManaging(slots);
        handoff.cancel(); detaching = true; closing = close; logoutOnClose = logout; focusRequested = false;
        stopMonitors();
        if (logout)
            for (auto& slot : slots) if (slot)
            {
                slot->closingAction = {};
                slot->closingAction.worker = slot->chat.identity.worker; slot->closingAction.generation = slot->chat.identity.generation;
                slot->closingAction.account = slot->chat.identity.account; slot->closingAction.grid = slot->chat.identity.grid;
            }
        message(logout ? L"Confirm logout in each native viewer. Close this host again to keep any remaining separate viewers logged in." :
            L"Returning characters to separate windows before closing their controller connection…");
    }
    std::vector<HWND> chatControls() const
    {
        std::vector<HWND> result{chatAccent,chatAccount,chatConversation,chatReview,chatRead,chatLabel,chatHistory,chatCompose,chatSend,voiceControl,muteControl,pinMenu};
        for (auto button : pinButtons) result.push_back(button); return result;
    }
    std::vector<HWND> characterControls() const
    {
        std::vector<HWND> result{characterControl, launchButton, embedControl, detachControl, showChatControl, optionsControl, compactInfo,viewMenu,settingsMenu,attentionButton,nextButton,restartButton};
        for(int i=0;i<MaxCharacters;++i){result.push_back(cardButtons[i]);result.push_back(cardActions[i]);}for(auto button:quickButtons)result.push_back(button);
        for (int i = 0; i < MaxCharacters; ++i) { result.push_back(status[i]); result.push_back(standbyControl[i]); }
        return result;
    }
    void panelFont(HWND panel, UINT value)
    {
        const bool chat = panel == chatWindow;
        UINT& panelDpi = panel == attentionWindow ? attentionDpi : chat ? chatDpi : controlsDpi;
        HFONT& previous = panel == attentionWindow ? attentionFont : chat ? chatFont : controlsFont;
        panelDpi = (std::max)(96u, value);
        HFONT next = CreateFontW(-theme.scale(12,panelDpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!next) return;
        EnumChildWindows(panel, [](HWND child, LPARAM data) -> BOOL
        { fs_host_ui::setNativeFont(child,reinterpret_cast<HFONT>(data));return TRUE; }, reinterpret_cast<LPARAM>(next));
        if (previous) DeleteObject(previous);
        previous = next;
    }
    void setPanelDetached(bool chat, bool detached, bool show = true)
    {
        DWORD selectionStart = 0, selectionEnd = 0;
        HWND priorFocus = GetFocus();
        if (chat)
        {
            saveDraft(true);
            SendMessageW(chatCompose, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
        }
        HWND& panel = chat ? chatWindow : controlsWindow;
        const bool newPanel = detached && !panel;
        bool& preference = chat ? chatDetached : controlsDetached;
        if (detached && !panel)
            panel = CreateWindowExW(WS_EX_TOOLWINDOW, L"FolderstormSessionPanel", chat ? L"Shared Chat · choose Send as" : L"Character controls",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, scaled(chat ? 800 : 840), scaled(chat ? 360 : 100 + MaxCharacters * 24), window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (detached && !panel) { message(L"Could not create the panel; its controls remain docked."); return; }
        const HWND target = detached ? panel : window;
        DWORD process = 0;
        if (!GetWindowThreadProcessId(target, &process) || process != GetCurrentProcessId()) return;
        auto children = chat ? chatControls() : characterControls();
        std::vector<std::pair<HWND, HWND>> moved;
        for (HWND child : children)
        {
            const HWND old = GetParent(child);
            if (old != target)
            {
                if (!SetParent(child, target))
                {
                    for (const auto& entry : moved) SetParent(entry.first, entry.second);
                    message(L"Could not move the panel controls; docking was retained."); return;
                }
                moved.emplace_back(child, old);
            }
        }
        for (const auto& entry : moved)
        {
            TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.hwnd = entry.second;
            tool.uId = reinterpret_cast<UINT_PTR>(entry.first);
            if (tooltips && SendMessageW(tooltips, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool)))
            {
                SendMessageW(tooltips, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
                tool.hwnd = target; SendMessageW(tooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            }
        }
        preference = detached;
        if (chat && show) { showChat = true; SendMessageW(showChatControl, BM_SETCHECK, BST_CHECKED, 0); }
        if(!detached && panel)SetWindowPos(panel,HWND_NOTOPMOST,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
        if (detached)
        {
            pinWindow(panel,false);
            using WindowDpi = UINT(WINAPI*)(HWND);
            const auto windowDpi = reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
            panelFont(panel, windowDpi ? windowDpi(panel) : dpi);
        }
        else for (HWND child : children) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (newPanel) { installPanelMenu(panel);CreateWindowExW(0,L"BUTTON",L"Pin",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,50,24,panel,reinterpret_cast<HMENU>(static_cast<INT_PTR>(PanelPin)),GetModuleHandleW(nullptr),nullptr); restorePlacement(panel,chat ? 1u : 2u,chat ? chatDpi : controlsDpi); }
        theme.window(target);
        layout();
        if(detached)pinWindow(panel,false);
        if (detached && show) ShowWindow(panel, SW_SHOWNORMAL);
        if (chat) SendMessageW(chatCompose, EM_SETSEL, selectionStart, selectionEnd);
        DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        if (foregroundPid == GetCurrentProcessId() && std::find(children.begin(), children.end(), priorFocus) != children.end()) SetFocus(priorFocus);
    }
    int characterHeight(HWND parent,bool condensed) const
    {
        RECT r{};GetClientRect(parent,&r);const UINT local=parent==window?dpi:controlsDpi;
        const int width=MulDiv(static_cast<int>(r.right),96,theme.uiDpi(local));
        const int row=experience.density?38:30;
        const int quickRows=experience.quick.empty()?0:((static_cast<int>(experience.quick.size())+(std::max)(1,width/110)-1)/(std::max)(1,width/110));
        const int cardsHeight=!condensed && experience.cards?MaxCharacters*row:0;
        const int detailHeight=!condensed && experience.diagnostics?MaxCharacters*24:0;
        const int selected=lifecycle.selection();const bool needsRestart=slots[selected] && (slots[selected]->snapshot.state==State::Disconnected || !slots[selected]->pipe.alive() || slots[selected]->restart.phase()!=Restart::Phase::Idle);
        return theme.scale(row+quickRows*row+cardsHeight+detailHeight+(needsRestart?row:0)+(parent!=window?row:0),local);
    }
    void layoutCharacters(HWND parent,bool condensed,bool visible)
    {
        RECT r{};GetClientRect(parent,&r);const UINT local=parent==window?dpi:controlsDpi;
        const auto scale=[this,local](int n){return theme.scale(n,local);};const int width=MulDiv(static_cast<int>(r.right),96,theme.uiDpi(local));
        const int row=experience.density?38:30;const int h=row-6;
        for(auto child:characterControls())ShowWindow(child,SW_HIDE);
        if(!visible)return;
        auto move=[&](HWND child,int x,int y,int w){MoveWindow(child,scale(x),scale(y),(std::max)(1,scale(w)),scale(h),TRUE);ShowWindow(child,SW_SHOWNOACTIVATE);};
        const bool narrow=width<800,small=width<440;commandOverflow=narrow;
        int x=4;const int selector=small?(std::max)(80,width-174):(std::max)(100,(std::min)(180,width-345));
        MoveWindow(characterControl,scale(x),scale(3),scale(selector),scale(220),TRUE);ShowWindow(characterControl,SW_SHOWNOACTIVATE);x+=selector+4;
        move(launchButton,x,3,narrow?28:64);x+=narrow?32:68;
        move(showChatControl,x,3,48);x+=52;
        if(!small){move(attentionButton,x,3,narrow?82:104);x+=narrow?86:108;move(nextButton,x,3,44);x+=48;}
        move(optionsControl,x,3,narrow?58:88);x+=narrow?62:92;
        SetWindowTextW(launchButton,narrow?L"+":L"+ Login");SetWindowTextW(optionsControl,narrow?L"More":L"Characters");
        if(!narrow){move(viewMenu,x,3,52);x+=56;move(settingsMenu,x,3,76);}
        int y=row;const int columns=(std::max)(1,width/110);
        for(std::size_t i=0;i<experience.quick.size();++i)
        {SetWindowTextW(quickButtons[i],quickLabel(experience.quick[i]).c_str());move(quickButtons[i],4+static_cast<int>(i%static_cast<std::size_t>(columns))*((width-8)/columns),y+static_cast<int>(i/static_cast<std::size_t>(columns))*row,(width-8)/columns-4);const auto* c=shellCommand(experience.quick[i]);if(c)addTooltip(quickButtons[i],c->tip);}
        y+=experience.quick.empty()?0:((static_cast<int>(experience.quick.size())+columns-1)/columns)*row;
        const int selected=lifecycle.selection();const bool needsRestart=slots[selected] && (slots[selected]->snapshot.state==State::Disconnected || !slots[selected]->pipe.alive() || slots[selected]->restart.phase()!=Restart::Phase::Idle);
        if(needsRestart){move(restartButton,4,y,160);SetWindowTextW(restartButton,slots[selected]->restart.phase()==Restart::Phase::Idle?L"Restart character":L"Cancel restart");y+=row;}
        if(!condensed && experience.cards)
        {
            for(unsigned int position=0;position<MaxCharacters;++position){const int i=static_cast<int>(experience.order[position]);move(cardButtons[i],4,y,width-48);move(cardActions[i],width-40,y,34);y+=row;}
        }
        if(!condensed && experience.diagnostics)for(int i=0;i<MaxCharacters;++i){MoveWindow(status[i],scale(4),scale(y),scale(width-8),scale(22),TRUE);ShowWindow(status[i],SW_SHOWNOACTIVATE);y+=24;}
        if(parent!=window){move(GetDlgItem(parent,PanelPin),width-58,y+3,50);}
    }
    void layoutChat(HWND parent,int top,bool visible,int bottomInset=0)
    {
        RECT r{};GetClientRect(parent,&r);const int width=static_cast<int>(r.right),height=static_cast<int>(r.bottom)-bottomInset;const UINT local=parent==window?dpi:chatDpi;
        const auto scale=[this,local](int n){return theme.scale(n,local);};const bool narrow=width<scale(650);
        for(auto child:chatControls())ShowWindow(child,visible?SW_SHOWNOACTIVATE:SW_HIDE);
        const auto pinsVisible=(std::min)(pins.entries.size(),static_cast<std::size_t>(6));
        for(std::size_t i=0;i<6;++i){MoveWindow(pinButtons[i],scale(6)+static_cast<int>(i)*((width-scale(12))/6),top,(std::max)(1,(width-scale(12))/6-scale(3)),scale(25),TRUE);ShowWindow(pinButtons[i],visible && i<pinsVisible?SW_SHOWNOACTIVATE:SW_HIDE);}if(pinsVisible)top+=scale(28);
        const auto move=[&](HWND child,int x,int y,int w,int h){MoveWindow(child,x,y,(std::max)(1,w),h,TRUE);};
        const int inset=scale(6);const int usable=width-inset*2;
        move(chatAccount,inset,top,narrow?usable:scale(180),scale(150));
        move(chatConversation,narrow?inset:scale(196),top+(narrow?scale(28):0),narrow?usable:(std::max)(scale(80),usable-scale(196)),scale(200));
        top+=scale(narrow?56:28);
        move(chatReview,inset,top,scale(120),scale(24));move(chatRead,scale(132),top,scale(60),scale(24));move(pinMenu,scale(198),top,scale(64),scale(24));
        if(parent!=window)move(GetDlgItem(parent,PanelPin),width-scale(58),top,scale(50),scale(24));
        top+=scale(28);move(chatAccent,inset,top,scale(5),scale(20));move(chatLabel,scale(18),top,width-scale(24),scale(20));top+=scale(24);
        const int input=(std::max)(top+scale(50),height-scale(42));
        move(chatHistory,inset,top,usable,(std::max)(1,input-top-scale(6)));
        move(chatCompose,inset,input,usable-scale(70),scale(30));move(chatSend,width-scale(70),input,scale(64),scale(30));
        // Audio policies live in Settings, eliminating a permanently occupied footer.
        ShowWindow(voiceControl,SW_HIDE);ShowWindow(muteControl,SW_HIDE);
    }
    void layout()
    {
        RECT rect{}; GetClientRect(window, &rect);
        const int width = static_cast<int>(rect.right), height = static_cast<int>(rect.bottom);
        const bool collapsed = chrome == 2;
        // On a small work area keep every command reachable through More, rather
        // than letting optional card/diagnostic rows cover the world or composer.
        const bool condensed=chrome==1 || (!controlsDetached && characterHeight(window,false)+scaled(showChat && !chatDetached?402:124)>height);
        RECT controlsRect{};if(controlsWindow)GetClientRect(controlsWindow,&controlsRect);
        const bool controlsCondensed=controlsDetached && characterHeight(controlsWindow,false)>controlsRect.bottom;
        layoutCharacters(controlsDetached ? controlsWindow : window, controlsDetached ? controlsCondensed : condensed, controlsDetached || !collapsed);
        const int top = collapsed ? scaled(revealHeight(chrome,experience.density!=0)) : controlsDetached ? 0 : characterHeight(window,condensed);
        ShowWindow(revealControl,collapsed ? SW_SHOWNOACTIVATE : SW_HIDE);
        MoveWindow(revealControl,0,0,width,scaled(revealHeight(chrome,experience.density!=0)),TRUE);
        const bool dockChat = showChat && !chatDetached && !collapsed;
        const int footerHeight = chrome != 2 ? scaled(24) : 0;
        const int chatTop = dockChat ? (std::max)(top + scaled(100), height - footerHeight - scaled(pins.entries.empty() ? 250 : 278)) : height - footerHeight;
        MoveWindow(viewport, 0, top, width, (std::max)(1, chatTop - top), TRUE);
        layoutChat(chatDetached ? chatWindow : window, chatDetached ? theme.scale(8,chatDpi) : chatTop, showChat && (chatDetached || !collapsed),chatDetached?0:footerHeight);
        auto showPanel = [](HWND panel, bool visible)
        {
            if (!panel) return;
            // Do not undo a user's independent panel minimization during layout.
            if (!visible) ShowWindow(panel, SW_HIDE);
            else if (!IsWindowVisible(panel)) ShowWindow(panel, IsIconic(panel) ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
        };
        showPanel(controlsWindow, controlsDetached && !IsIconic(window));
        showPanel(chatWindow, chatDetached && showChat && !IsIconic(window));
        ShowWindow(notice,chrome!=2?SW_SHOWNOACTIVATE:SW_HIDE);
        MoveWindow(notice, scaled(10), height - scaled(23), (std::max)(1, width - scaled(20)), scaled(20), TRUE);
    }
    void addTooltip(HWND child, const wchar_t* text)
    {
        if (!tooltips) return;
        TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = GetParent(child); tool.uId = reinterpret_cast<UINT_PTR>(child); tool.lpszText = const_cast<wchar_t*>(text);
        const auto* label=tool.lpszText;
        const bool exists=SendMessageW(tooltips,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&tool))!=0;
        tool.lpszText=const_cast<wchar_t*>(label);
        SendMessageW(tooltips,exists?TTM_UPDATETIPTEXTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    }
};
Host* host = nullptr;
LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (!host) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_SYSCOMMAND && ((wparam & 0xfff0u) == 0xA140 || (wparam & 0xfff0u) == 0xA150))
    { host->recover((wparam & 0xfff0u) == 0xA150); return 0; }
    if(message==WM_SYSCOMMAND && (wparam&0xfff0u)==0xA160){host->pinWindow(window);return 0;}
    const bool chat = window == host->chatWindow;
    if (window == host->attentionWindow)
    {
        if (message == WM_SIZE)
        {
            host->layoutAttention(); return 0;
        }
        if (message == WM_CLOSE) { ShowWindow(window,SW_HIDE); return 0; }
        if (message == WM_DPICHANGED)
        {
            host->panelFont(window,LOWORD(wparam)); const auto* r = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); host->layoutAttention(); return 0;
        }
        if (message == WM_GETMINMAXINFO) { reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {host->theme.scale(450,host->attentionDpi),host->theme.scale(220,host->attentionDpi)}; return 0; }
    }
    switch (message)
    {
    case WM_ERASEBKGND: {RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,host->theme.backBrush);return 1;}
    case WM_MEASUREITEM: return SendMessageW(host->window,message,wparam,lparam);
    case WM_DRAWITEM: return SendMessageW(host->window,message,wparam,lparam);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: return SendMessageW(host->window,message,wparam,lparam);
    case WM_COMMAND: if(LOWORD(wparam)==PanelPin){host->pinWindow(window);return 0;}return SendMessageW(host->window, message, wparam, lparam);
    case WM_CONTEXTMENU:
    {
        const auto child=reinterpret_cast<HWND>(wparam);const int id=GetDlgCtrlID(child);
        if(child==host->characterControl || (id>=CardBase && id<CardBase+MaxCharacters))return SendMessageW(host->window,message,wparam,lparam);
        return DefWindowProcW(window,message,wparam,lparam);
    }
    case WM_SIZE:
        if (window == host->chatWindow || window == host->controlsWindow) host->layout();
        return 0;
    case WM_DPICHANGED:
    {
        host->panelFont(window, LOWORD(wparam));
        const auto* rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        host->layout(); return 0;
    }
    case WM_GETMINMAXINFO:
    {
        const UINT dpi = chat ? host->chatDpi : host->controlsDpi;
        int minimumHeight=host->theme.scale(chat?300:350,dpi);
        if(!chat)minimumHeight=(std::max)(minimumHeight,host->characterHeight(window,false)+host->theme.scale(40,dpi));
        MONITORINFO work{};work.cbSize=sizeof(work);
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&work))minimumHeight=(std::min)(minimumHeight,static_cast<int>(work.rcWork.bottom-work.rcWork.top));
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {
            host->theme.scale(chat?360:560,dpi),
            minimumHeight}; return 0;
    }
    case WM_CLOSE:
        if (chat)
        {
            host->saveDraft(true); host->showChat = false;
            SendMessageW(host->showChatControl, BM_SETCHECK, BST_UNCHECKED, 0); host->layout();
        }
        else { host->setPanelDetached(false, false); host->setChrome(recoveredChrome(host->chrome,true)); }
        ShowWindow(window, SW_HIDE); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
LRESULT CALLBACK monitorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* monitor = reinterpret_cast<MonitorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!host || !monitor) return DefWindowProcW(window, message, wparam, lparam);
    int id = -1;
    for (int i = 0; i < MaxMonitors; ++i) if (&host->monitors[i] == monitor) { id = i; break; }
    if (id < 0) return DefWindowProcW(window, message, wparam, lparam);
    if(message==WM_SYSCOMMAND){const auto command=wparam&0xfff0u;if(command==0xA140 || command==0xA150){host->recover(command==0xA150);return 0;}if(command==0xA160){host->pinWindow(window);return 0;}}
    const auto& binding = host->monitorSet.bindings[id];
    auto scale = [monitor](int value) { return host->theme.scale(value,monitor->dpi); };
    switch (message)
    {
    case WM_COMMAND:
        if(LOWORD(wparam)==PanelPin){host->pinWindow(window);return 0;}
        if(LOWORD(wparam)==MonitorChatToggle){host->setMonitorChat(id,!monitor->showChat);return 0;}
        if(LOWORD(wparam)==MonitorOptions){host->monitorMenu(id);return 0;}
        if(LOWORD(wparam)==MonitorExchange)
        {const auto* slot=host->monitorChatSlot(id);if(slot && !(slot->snapshot.flags&ChatRestricted) && binding.enabled){SetForegroundWindow(host->window);host->switchTo(binding.slot);}return 0;}
        host->monitorChatCommand(id,LOWORD(wparam),HIWORD(wparam));return 0;
    case WM_DRAWITEM: if(host->theme.button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))return TRUE;break;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN: return host->theme.color(message,wparam,lparam);
    case WM_CONTEXTMENU: host->monitorMenu(id); return 0;
    case WM_CLOSE:
        host->stopMonitor(id, true); return 0;
    case WM_LBUTTONDOWN:
    {
        // Only the labelled footer promotes a character. Image/chat clicks do not.
        const int y = static_cast<short>(HIWORD(lparam)), footer = host->monitorImageHeight(id);
        if (y >= footer && y < footer+scale(28))
        {
            const auto* slot = validSlot(binding.slot) ? host->slots[binding.slot].get() : nullptr;
            if (!binding.enabled || !slot || !slot->running() || slot->detached ||
                !slot->pipe.alive() || slot->snapshot.state != State::Ready || !binding.key.owns(slot->snapshot))
                host->message(L"The monitored session changed. Choose its monitor again before switching.");
            else { SetForegroundWindow(host->window); host->switchTo(binding.slot); }
        }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{}; const HDC destination = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        PaintBuffer buffer(destination, static_cast<int>(client.right), static_cast<int>(client.bottom));
        const HDC dc = buffer.dc();
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const int width = client.right, height = host->monitorImageHeight(id);
        const auto* source = validSlot(binding.slot) ? host->slots[binding.slot].get() : nullptr;
        if (!binding.enabled || !source || !source->running() || source->detached || !source->pipe.alive() ||
            !binding.key.owns(source->snapshot) || source->snapshot.state != State::Ready ||
            (source->snapshot.flags & (ChatRestricted | PreviewUnavailable)) || binding.slot == host->active || host->busy()) host->clearMonitor(id);
        if (!monitor->pixels.empty())
        {
            const auto& frame = monitor->frame;
            const double fit = (std::min)(static_cast<double>(width) / frame.width, static_cast<double>(height) / frame.height);
            const int w = static_cast<int>(frame.width * fit), h = static_cast<int>(frame.height * fit);
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(frame.width); info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, (width - w) / 2, (height - h) / 2, w, h, 0, 0, static_cast<int>(frame.width), static_cast<int>(frame.height),
                monitor->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }
        RECT footer{0,height,client.right,height+scale(28)};
        FillRect(dc, &footer, host->theme.panelBrush);
        if (binding.enabled)
        {
            RECT accent = footer; accent.right = accent.left + scale(6);
            const HBRUSH brush = CreateSolidBrush(host->accountColor(binding.slot)); FillRect(dc,&accent,brush); DeleteObject(brush);
        }
        SetTextColor(dc,binding.enabled?host->theme.text:host->theme.muted);
        SetBkMode(dc, TRANSPARENT); DrawTextW(dc,monitor->previewStatus.c_str(), -1, &footer, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (monitor->showChat)
        {
            RECT chat{0,height+scale(28),client.right,client.bottom};
            FillRect(dc,&chat,host->theme.backBrush);
            RECT accent = chat; accent.right = scale(5); accent.bottom = accent.top+scale(25);
            const HBRUSH brush = CreateSolidBrush(monitor->color); FillRect(dc,&accent,brush); DeleteObject(brush);
        }
        buffer.present(); EndPaint(window, &paint); return 0;
    }
    case WM_DPICHANGED:
    {
        monitor->dpi = (std::max)(96u, static_cast<UINT>(LOWORD(wparam))); host->monitorFont(id);
        const auto* rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        host->layoutMonitorChat(id); return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {scale(monitor->showChat ? 320 : 280),scale(monitor->showChat ? 360 : 120)}; return 0;
    case WM_SIZE: host->layoutMonitorChat(id); InvalidateRect(window,nullptr,FALSE); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
LRESULT CALLBACK switchChoiceProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR)
{
    if(message==WM_KEYDOWN && (wp==VK_UP || wp==VK_DOWN) && !SendMessageW(window,CB_GETDROPPEDSTATE,0,0))SendMessageW(window,CB_SHOWDROPDOWN,TRUE,0);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,switchChoiceProc,1);
    return DefSubclassProc(window,message,wp,lp);
}
HWND control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0)
{
    HWND result = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return result;
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (host && host->taskbarCreated && message == host->taskbarCreated) { host->tray(true); return 0; }
    if (!host) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_SYSCOMMAND && ((wparam & 0xfff0u) == 0xA140 || (wparam & 0xfff0u) == 0xA150))
    { host->recover((wparam & 0xfff0u) == 0xA150); return 0; }
    switch (message)
    {
    case WM_MEASUREITEM:
    {
        auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        const UINT local=item->CtlID==ChatAccount && host->chatDetached?host->chatDpi:item->CtlID==ActiveCharacter && host->controlsDetached?host->controlsDpi:item->CtlID==AttentionList?host->attentionDpi:host->dpi;
        item->itemHeight=static_cast<UINT>(host->theme.scale(22,local));return TRUE;
    }
    case WM_ERASEBKGND: {RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,host->theme.backBrush);return 1;}
    case WM_DRAWITEM: if (host->drawIdentity(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE; break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: return host->theme.color(message,wparam,lparam);
    case WM_CREATE:
    {
        host->window = window;
        host->launchButton = control(window, L"BUTTON", L"+ Character", BS_PUSHBUTTON | WS_TABSTOP, 190, 10, 95, 28, LaunchNext);
        host->characterControl = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 10, 10, 170, 200, ActiveCharacter);
        SetWindowSubclass(host->characterControl,switchChoiceProc,1,0);
        host->showChatControl = control(window, L"BUTTON", L"Chat panel", BS_AUTOCHECKBOX | WS_TABSTOP, 695, 10, 90, 28, ShowChat);
        SendMessageW(host->showChatControl, BM_SETCHECK, host->showChat ? BST_CHECKED : BST_UNCHECKED, 0);
        host->embedControl = control(window, L"BUTTON", L"Host active viewer", BS_AUTOCHECKBOX | WS_TABSTOP, 383, 10, 235, 28, HostSurface);
        host->detachControl = control(window, L"BUTTON", L"Separate windows", BS_PUSHBUTTON | WS_TABSTOP, 630, 10, 125, 28, DetachAll);
        host->viewMenu=control(window,L"BUTTON",L"View",BS_PUSHBUTTON|WS_TABSTOP,0,0,60,28,ViewActions);
        host->settingsMenu=control(window,L"BUTTON",L"Settings",BS_PUSHBUTTON|WS_TABSTOP,0,0,70,28,SettingsActions);
        host->attentionButton=control(window,L"BUTTON",L"Attention",BS_PUSHBUTTON|WS_TABSTOP,0,0,80,28,274);
        host->nextButton=control(window,L"BUTTON",L"Next",BS_PUSHBUTTON|WS_TABSTOP,0,0,45,28,NextAttention);
        host->restartButton=control(window,L"BUTTON",L"Restart",BS_PUSHBUTTON|WS_TABSTOP,0,0,70,28,240);
        for(int i=0;i<MaxCharacters;++i){host->cardButtons[i]=control(window,L"BUTTON",L"Character",BS_PUSHBUTTON|WS_TABSTOP,0,0,200,30,CardBase+i);host->cardActions[i]=control(window,L"BUTTON",L"…",BS_PUSHBUTTON|WS_TABSTOP,0,0,30,30,CardActionBase+i);}
        for(int i=0;i<12;++i)host->quickButtons[i]=control(window,L"BUTTON",L"",BS_PUSHBUTTON|WS_TABSTOP,0,0,50,28,QuickBase+i);
        host->revealControl = control(window,L"BUTTON",L"Show controls",BS_PUSHBUTTON|WS_TABSTOP,0,0,200,24,RestoreControls);
        host->optionsControl = control(window, L"BUTTON", L"Characters", BS_PUSHBUTTON | WS_TABSTOP, 705, 10, 80, 28, SessionOptions);
        SendMessageW(host->embedControl, BM_SETCHECK, host->embedding ? BST_CHECKED : BST_UNCHECKED, 0);
        for (int i = 0; i < MaxCharacters; ++i)
        {
            const auto label = L"Character " + std::to_wstring(i + 1) + L": not launched";
            host->status[i] = control(window, L"STATIC", label.c_str(), SS_OWNERDRAW, 10, 48 + i * 24, 850, 20);
            host->standbyControl[i] = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 760, 46 + i * 24, 190, 130, StandbyBase + i);
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Background: Warm"));
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Economy (experimental)"));
            SendMessageW(host->standbyControl[i], CB_SETCURSEL, host->standby[i] == Mode::Economy ? 1 : 0, 0);
        }
        host->compactInfo = control(window, L"STATIC", L"Character status in Session menu", SS_LEFT, 536, 5, 250, 20);
        host->viewport = control(window, L"STATIC", L"", SS_BLACKRECT | SS_NOTIFY | WS_CLIPCHILDREN, 0, 99, 880, 490, ViewportFocus);
        host->notice = control(window, L"STATIC", L"Choose a character or open a login to begin.", SS_LEFT|SS_ENDELLIPSIS|SS_NOPREFIX, 10, 600, 850, 20);
        host->pinMenu = control(window,L"BUTTON",L"Pins…",BS_PUSHBUTTON|WS_TABSTOP,810,400,80,24,PinActions);
        for (int i = 0; i < 6; ++i) host->pinButtons[i] = control(window,L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,10+i*130,400,125,25,PinBase+i);
        host->chatAccount = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 10, 400, 180, 150, ChatAccount);
        host->chatConversation = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 200, 400, 400, 200, ChatConversation);
        host->chatHistory = control(window, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 10, 452, 850, 88);
        host->chatCompose = control(window, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER | WS_TABSTOP, 10, 545, 800, 35, ChatCompose);
        SendMessageW(host->chatCompose, EM_SETLIMITTEXT, 1023, 0);
        host->chatSend = control(window, L"BUTTON", L"Send", BS_PUSHBUTTON | WS_TABSTOP, 820, 545, 75, 35, ChatSend);
        host->chatReview = control(window, L"BUTTON", L"Review in viewer", BS_PUSHBUTTON | WS_TABSTOP, 620, 400, 140, 24, ChatReview);
        host->chatRead = control(window, L"BUTTON", L"Mark read", BS_PUSHBUTTON | WS_TABSTOP, 770, 400, 90, 24, ChatRead);
        host->chatAccent = control(window,L"STATIC",L"",SS_OWNERDRAW,10,429,5,20,ChatAccent);
        host->chatLabel = control(window, L"STATIC", L"Send as: not logged in", SS_LEFT, 10, 429, 850, 20);
        host->voiceControl = control(window, L"BUTTON", L"Voice follows active character", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 585, 255, 24, VoicePolicy);
        host->muteControl = control(window, L"BUTTON", L"Mute background world/UI/media sound", BS_AUTOCHECKBOX | WS_TABSTOP, 280, 585, 280, 24, BackgroundMute);
        SendMessageW(host->voiceControl, BM_SETCHECK, host->voice ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(host->muteControl, BM_SETCHECK, host->muteBackground ? BST_CHECKED : BST_UNCHECKED, 0);
        host->refreshChat(true);
        host->refreshCharacters(); host->updateDpi(96);
        HMENU system = GetSystemMenu(window, FALSE);
        AppendMenuW(system, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(system, MF_STRING, 0xA100, L"Normal character controls");
        AppendMenuW(system, MF_STRING, 0xA110, L"Condensed character controls");
        AppendMenuW(system, MF_STRING, 0xA120, L"Minimal controls with reveal strip");
        AppendMenuW(system, MF_STRING, 0xA130, L"Character sessions…");
        host->recoveryMenu(window); host->tray(true);
        const bool chatDetached = host->chatDetached, controlsDetached = host->controlsDetached;
        host->chatDetached = host->controlsDetached = false;
        if(!host->revealControl && host->chrome==2)host->chrome=1;
        host->setChrome(host->chrome);
        host->tooltips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        host->addTooltip(host->launchButton, L"Open the next available character login; Session offers individual slots");
        host->addTooltip(host->embedControl, L"Fit the active viewer to this host; uncheck to return to a separate managed window");
        host->addTooltip(host->detachControl, L"Use separate viewer windows while retaining character management");
        host->addTooltip(host->optionsControl, L"Character actions, monitors, host presentation and docking");
        if (chatDetached) host->setPanelDetached(true, true, false);
        if (controlsDetached) host->setPanelDetached(false, true, false);
        if (!host->lastNotice.empty()) host->message(host->lastNotice);
        host->applyTheme();
        SetTimer(window, Timer, 100, nullptr); return 0;
    }
    case WM_TIMER: host->tick(); return 0;
    case WM_CONTEXTMENU:
    {
        const HWND child=reinterpret_cast<HWND>(wparam);const int id=GetDlgCtrlID(child);
        if((id>=CardBase && id<CardBase+MaxCharacters) || child==host->characterControl)
        {const int selected=child==host->characterControl?host->selectedSlot(child):id-CardBase;host->lifecycle.observeSelection(selected);SendMessageW(host->characterControl,CB_SETCURSEL,selected,0);host->sessionMenu(1,0,selected);return 0;}
        break;
    }
    case WM_DISPLAYCHANGE: host->refitWindows(); return 0;
    case WM_SETTINGCHANGE:
        host->applyTheme();host->updateDpi(host->dpi);
        for(HWND panel:{host->chatWindow,host->controlsWindow,host->attentionWindow})if(panel)host->panelFont(panel,panel==host->chatWindow?host->chatDpi:panel==host->controlsWindow?host->controlsDpi:host->attentionDpi);
        for(int i=0;i<MaxMonitors;++i)if(host->monitors[i].window){host->monitorFont(i);host->layoutMonitorChat(i);}
        host->refitWindows(); return 0;
    case TrayMessage:
        if (lparam == WM_LBUTTONDBLCLK) host->recover();
        else if (lparam == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu(); AppendMenuW(menu,MF_STRING,RestoreControls,L"Show controls"); AppendMenuW(menu,MF_STRING,RecoverWindows,L"Recover windows"); AppendMenuW(menu,MF_STRING,142,L"Exit host…");
            POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
            const auto choice = TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr); DestroyMenu(menu);
            if (choice == RestoreControls) host->recover(); else if (choice == RecoverWindows) host->recover(true); else if (choice == 142) SendMessageW(window,WM_CLOSE,0,0);
            PostMessageW(window,WM_NULL,0,0);
        } return 0;
    case WM_EXITMENULOOP: host->menuOpen = false; return 0;
    case WM_ENTERMENULOOP: host->menuOpen = true; host->focusRequested = false; RemovePropW(window,L"FolderstormViewportFocusIntent"); return 0;
    case WM_LBUTTONDOWN:
    case WM_PARENTNOTIFY:
        host->focusRequested = false; RemovePropW(window, L"FolderstormViewportFocusIntent");
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
        { host->focusRequested = false; RemovePropW(window, L"FolderstormViewportFocusIntent"); }
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_SIZE: if (host->viewport) host->layout(); return 0;
    case WM_DPICHANGED:
    {
        host->updateDpi(LOWORD(wparam));
        const auto* next = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, next->left, next->top, next->right - next->left, next->bottom - next->top, SWP_NOZORDER | SWP_NOACTIVATE);
        host->layout(); return 0;
    }
    case WM_GETMINMAXINFO:
    {
        const bool dockChat=host->showChat && !host->chatDetached && host->chrome!=2;
        const int header=host->chrome==2?host->scaled(revealHeight(host->chrome,host->experience.density!=0)):host->controlsDetached?0:host->characterHeight(window,host->chrome==1);
        int minimumHeight=(std::max)(host->scaled(300),header+host->scaled(dockChat?(host->pins.entries.empty()?414:442):164));
        MONITORINFO work{};work.cbSize=sizeof(work);
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&work))minimumHeight=(std::min)(minimumHeight,static_cast<int>(work.rcWork.bottom-work.rcWork.top));
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {
            host->scaled(host->chrome == 2 ? 400 : 560),
            minimumHeight}; return 0;
    }
    case WM_SYSCOMMAND:
    {
        const auto command = static_cast<unsigned int>(wparam & 0xfff0u);
        if (command >= 0xA100 && command <= 0xA120) { host->setChrome((command - 0xA100) / 0x10); return 0; }
        if (command == 0xA130) { host->sessionMenu(); return 0; }
        if (command == 0xA140 || command == 0xA150) { host->recover(command == 0xA150); return 0; }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    case WM_COMMAND:
        if(LOWORD(wparam)!=ViewportFocus){host->focusRequested=false;RemovePropW(window,L"FolderstormViewportFocusIntent");}
        if(LOWORD(wparam)>=CardBase && LOWORD(wparam)<CardBase+MaxCharacters)
        {const int index=LOWORD(wparam)-CardBase;host->lifecycle.observeSelection(index);SendMessageW(host->characterControl,CB_SETCURSEL,index,0);if(host->slots[index] && host->slots[index]->snapshot.state==State::Ready)host->switchTo(index);else host->sessionMenu(1,0,index);return 0;}
        if(LOWORD(wparam)>=CardActionBase && LOWORD(wparam)<CardActionBase+MaxCharacters)
        {const int index=LOWORD(wparam)-CardActionBase;host->lifecycle.observeSelection(index);SendMessageW(host->characterControl,CB_SETCURSEL,index,0);host->sessionMenu(1,0,index);return 0;}
        if(LOWORD(wparam)>=QuickBase && LOWORD(wparam)<QuickBase+12)
        {const auto i=static_cast<std::size_t>(LOWORD(wparam)-QuickBase);if(i<host->experience.quick.size())host->sessionMenu(0,host->experience.quick[i]);return 0;}
        if(LOWORD(wparam)==ViewActions){host->sessionMenu(2);return 0;}if(LOWORD(wparam)==SettingsActions){host->sessionMenu(3);return 0;}
        if(LOWORD(wparam)==274 && reinterpret_cast<HWND>(lparam)==host->attentionButton){host->sessionMenu(5);return 0;}
        if(LOWORD(wparam)==240 && host->slots[host->lifecycle.selection()] && host->slots[host->lifecycle.selection()]->restart.phase()!=Restart::Phase::Idle){host->sessionMenu(0,241);return 0;}
        if(shellCommand(LOWORD(wparam)) || LOWORD(wparam)==AttentionSettings){host->sessionMenu(0,LOWORD(wparam));return 0;}
        if (LOWORD(wparam) >= PinBase && LOWORD(wparam) < PinBase+6) { host->selectPin(LOWORD(wparam)-PinBase); return 0; }
        if (LOWORD(wparam) != ViewportFocus)
        { host->focusRequested = false; RemovePropW(window, L"FolderstormViewportFocusIntent"); }
        if (LOWORD(wparam) >= StandbyBase && LOWORD(wparam) < StandbyBase + MaxCharacters)
        {
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                const int index = LOWORD(wparam) - StandbyBase;
                host->standby[index] = SendMessageW(host->standbyControl[index], CB_GETCURSEL, 0, 0) == 1 ? Mode::Economy : Mode::Warm;
                host->message(L"Background choice applies to this character when inactive. Economy savings need native measurements.");
            }
            return 0;
        }
        switch (LOWORD(wparam))
        {
        case ViewportFocus:
            if (host->embedding && HIWORD(wparam) == STN_CLICKED)
            {
                if (++host->focusIntent == 0) ++host->focusIntent;
                host->focusRequested = SetPropW(window, L"FolderstormViewportFocusIntent", reinterpret_cast<HANDLE>(host->focusIntent)) != FALSE;
            }
            break;
        case LaunchNext: host->sessionMenu(4); break;
        case RestoreControls: host->recover(); break;
        case RecoverWindows: host->recover(true); break;
        case SessionOptions: host->sessionMenu(host->commandOverflow?0:1); break;
        case ActiveCharacter:
            if(HIWORD(wparam)==CBN_SELENDOK)
            {const int index=host->selectedSlot(host->characterControl);host->lifecycle.observeSelection(index);if(host->slots[index] && host->slots[index]->snapshot.state==State::Ready)host->switchTo(index);else host->sessionMenu(1,0,index);}
            if (HIWORD(wparam) == CBN_CLOSEUP) host->refreshCharacters();
            break;
        case ShowChat:
            if(host->chrome==2 && !host->chatDetached)host->setChrome(1);
            host->saveDraft(true); host->showChat = SendMessageW(host->showChatControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            host->layout(); break;
        case ChatAccount:
            if (!host->updatingChat && HIWORD(wparam) == CBN_SELCHANGE)
            {
                host->saveDraft(true); host->chatIndex = host->selectedSlot(host->chatAccount);
                host->conversation.clear(); host->refreshChat(true, true);
            }
            if (HIWORD(wparam) == CBN_CLOSEUP) host->refreshChat(true);
            break;
        case ChatConversation:
            if (!host->updatingChat && HIWORD(wparam) == CBN_SELCHANGE)
            {
                const auto selected = SendMessageW(host->chatConversation, CB_GETCURSEL, 0, 0);
                host->saveDraft(true);
                if (selected >= 0 && static_cast<std::size_t>(selected) < host->conversationIds.size()) host->conversation = host->conversationIds[static_cast<std::size_t>(selected)];
                host->refreshChat(true);
            }
            if (HIWORD(wparam) == CBN_CLOSEUP) host->refreshChat(true);
            break;
        case ChatCompose: if (HIWORD(wparam) == EN_CHANGE) host->composeChanged(); break;
        case PinActions: host->pinActions(); break;
        case AttentionReview: host->attentionAction(false); break;
        case AttentionDismiss: host->attentionAction(true); break;
        case ChatSend: host->saveDraft(true); host->sendChat(); break;
        case ChatReview: host->reviewChat(); break;
        case ChatRead:
            if (host->slots[host->chatIndex])
            {
                const auto request = host->boundChat(Kind::MarkRead);
                if (!host->send(host->chatIndex, Kind::MarkRead, Mode::Warm, 0, &request)) host->message(L"Viewer is busy; try Mark read again.");
            }
            break;
        case VoicePolicy: host->voice = SendMessageW(host->voiceControl, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
        case BackgroundMute: host->muteBackground = SendMessageW(host->muteControl, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
        case HostSurface:
            host->embedding = SendMessageW(host->embedControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            host->focusRequested = false;
            host->message(host->embedding ? L"Active viewer fitted to this host. Use Separate windows for fullscreen or recovery." : L"Using separate viewer windows; chosen standby modes remain enabled.");
            break;
        case DetachAll:
            host->embedding = false; SendMessageW(host->embedControl, BM_SETCHECK, BST_UNCHECKED, 0);
            host->message(L"Returning the active viewer to a separate window; character management remains enabled."); break;
        }
        return 0;
    case WM_CLOSE:
        if (host->closing)
        {
            if (host->logoutOnClose && host->closeAction(true)==1 && IsWindow(window)) host->logoutOnClose = false;
            return 0;
        }
        switch (host->closeAction(false))
        {
        case 2: if(IsWindow(window))host->detach(true, true); break;
        case 1: if(IsWindow(window))host->detach(true); break;
        }
        return 0;
    case WM_DESTROY: host->tray(false); KillTimer(window, Timer); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    ComApartment apartment;
    // Resolve at runtime for older Windows SDK/runtime compatibility.
    using DpiContext = BOOL(WINAPI*)(HANDLE);
    const auto dpiContext = reinterpret_cast<DpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpiContext) dpiContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); // PER_MONITOR_AWARE_V2
    else SetProcessDPIAware();
    INITCOMMONCONTROLSEX common{sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES}; InitCommonControlsEx(&common);
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
    std::error_code profileError; std::filesystem::create_directories(state.profiles, profileError);
    if (profileError) return 1;
    state.optionsLease.value = CreateFileW((state.profiles / L"host.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (state.optionsLease.value == INVALID_HANDLE_VALUE)
    { MessageBoxW(nullptr, L"A character controller is already using these profiles. Use that controller or close it first.", L"Folderstorm", MB_OK); return 1; }
    bool haveHostOptions=false;
    std::ifstream savedOptions(state.profiles / L"host-options.txt", std::ios::binary);
    if (savedOptions)
    {
        char data[513]{}; savedOptions.read(data, sizeof(data)); HostOptions choices;
        if (HostOptions::decode(std::string(data, static_cast<std::size_t>(savedOptions.gcount())), choices))
        {
            haveHostOptions=true;state.standby = choices.standby;
            state.voice = choices.voice; state.muteBackground = choices.muteBackground; state.embedding = choices.hosted; state.showChat = choices.chat;
            state.monitorSize = choices.previewSize; state.monitorRate = choices.previewRate;
            state.cinematic = choices.cinematic; state.chrome = choices.chrome;
            state.chatDetached = choices.chatDetached; state.controlsDetached = choices.controlsDetached;
        }
    }
    state.presentation.transition = state.cinematic ? 1u : 0u;
    std::ifstream appearanceFile(state.profiles / L"host-presentation.dat",std::ios::binary);
    if (appearanceFile)
    {
        std::string data(PresentationStore::MaxBytes + 1,'\0'); appearanceFile.read(data.data(),static_cast<std::streamsize>(data.size()));
        data.resize(static_cast<std::size_t>(appearanceFile.gcount()));
        if (!PresentationStore::decode(data,state.presentation)) state.lastNotice = L"Saved host appearance data is invalid; using defaults.";
    }
    std::ifstream experienceFile(state.profiles/L"host-experience.dat",std::ios::binary);
    if(experienceFile){std::string data(HostExperience::MaxBytes+1,'\0');experienceFile.read(data.data(),static_cast<std::streamsize>(data.size()));data.resize(static_cast<std::size_t>(experienceFile.gcount()));if(!HostExperience::decode(data,state.experience) || bindingsConflict(state.experience,state.presentation)){state.experience=HostExperience{};state.lastNotice=L"Saved host experience settings are invalid; using defaults.";}}
    else if(!haveHostOptions)state.chrome=1;
    std::ifstream setsFile(state.profiles/L"session-sets.dat",std::ios::binary);
    if(setsFile){std::string data(SessionSetStore::MaxBytes+1,'\0');setsFile.read(data.data(),static_cast<std::streamsize>(data.size()));data.resize(static_cast<std::size_t>(setsFile.gcount()));if(!SessionSetStore::decode(data,state.setStore))state.lastNotice=L"Saved session sets are invalid; no set was opened.";}
    state.pins.restore(state.presentation.pins);
    host = &state;
    const auto icon = static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_SESSION_HOST),IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    const auto smallIcon = static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_SESSION_HOST),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    WNDCLASSEXW klass{}; klass.cbSize = sizeof(klass); klass.hIcon = icon; klass.hIconSm = smallIcon; klass.lpfnWndProc = windowProc; klass.hInstance = instance;
    klass.lpszClassName = L"FolderstormSessionPrototype"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&klass)) return 1;
    WNDCLASSEXW panelClass{}; panelClass.cbSize = sizeof(panelClass); panelClass.hIcon = icon; panelClass.hIconSm = smallIcon; panelClass.lpfnWndProc = panelProc; panelClass.hInstance = instance;
    panelClass.lpszClassName = L"FolderstormSessionPanel"; panelClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    panelClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&panelClass)) return 1;
    WNDCLASSEXW monitorClass{}; monitorClass.cbSize = sizeof(monitorClass); monitorClass.hIcon = icon; monitorClass.hIconSm = smallIcon; monitorClass.lpfnWndProc = monitorProc; monitorClass.hInstance = instance;
    monitorClass.lpszClassName = L"FolderstormSessionMonitor"; monitorClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&monitorClass)) return 1;
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"Folderstorm character sessions",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 980, 700, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    using WindowDpi = UINT(WINAPI*)(HWND);
    const auto windowDpi = reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (windowDpi) { state.updateDpi(windowDpi(window)); state.layout(); }
    state.restorePlacement(window,0,state.dpi);
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (state.hostShortcut(message)) continue;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE && state.busy())
        { state.skipTransition(); continue; }
        std::array<HWND,4+MaxMonitors> panels{window,state.chatWindow,state.controlsWindow,state.attentionWindow};
        for (int i = 0; i < MaxMonitors; ++i) panels[static_cast<std::size_t>(i)+4] = state.monitors[i].window;
        const bool handled = routePanelDialog(message.hwnd, panels,
            [](HWND panel, HWND target)
            { return IsWindow(panel) && IsWindowVisible(panel) && (panel == target || IsChild(panel, target)); },
            [&message](HWND panel) { return IsDialogMessageW(panel, &message) != FALSE; });
        if (!handled) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    host = nullptr;
    return static_cast<int>(message.wParam);
}
