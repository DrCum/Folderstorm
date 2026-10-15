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
#include "fssessionaudio.h"
#include "fssessionchoicestate.h"
#include "fssessionshortcuts.h"
#include <imm.h>
#include "dialog.h"
#include "chatui.h"
#include "friendsui.h"
#include "fssessionmonitortools.h"
#include "fssessionchatprefs.h"
#include "fssessionreadpolicy.h"
#include "fssessionsound.h"
#include "resource.h"
#include "fssessionpresentation.h"
#include "fssessionusability.h"
#include "fssessionpipe.h"
#include "fssessionchatmodel.h"
#include "fssessionhostoptions.h"
#include "fssessionmonitormodel.h"
#include "fssessionmonitorchat.h"
#include "fssessionprofileseed.h"
#include "fssessionpreferences.h"
#include "fssessionframepipe.h"
#include "fssessionrestart.h"
#include "fssessionrecovery.h"
#include "fssessionexperience.h"
#include "fssessiontoolbarlayout.h"
#include "fssessionattentionnav.h"
#include "fssessionassistant.h"
#include "fssessionsets.h"
#include "theme.h"
#include "tooltips.h"
#include "fssessionlifecycle.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iterator>
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
constexpr int VoicePolicy = 114, BackgroundMute = 115, ChatRead = 116, ChatLatest = 169, ChatSettings = 455, CharacterSoundSettings = 456, ChatFilters = 457, ChatClose = 458, VisibilityBase = 470;
constexpr int ActiveCharacter = 117, ShowChat = 118;
constexpr int SessionOptions = 119, ViewportFocus = 120;
constexpr int ChatAccent = 122, PinActions = 123, PinBase = 600, AttentionList = 130, AttentionReview = 131, AttentionDismiss = 132;
constexpr int RestoreControls = 140, RecoverWindows = 141;
constexpr int CardBase=700, CardActionBase=710, QuickBase=730, PanelPin=165, MonitorExchange=166, MonitorChatToggle=167, MonitorOptions=168;
constexpr int DensityToggle=175,ControlsDock=176,ChatDock=177,QuickOverflow=178,MonitorsMenu=179,MainOverflow=181;
constexpr int ChatNewIM=460,ChatFriends=461,ContactFilter=810,ContactSearch=811,ContactFriends=812,ContactList=813,ContactOpen=814,ContactPopout=815,ContactOnlineOnly=816;
constexpr int CyclePlay=820,CyclePrevious=821,CycleNext=822,CycleInterval=823;
constexpr UINT TrayMessage = WM_APP + 20;
constexpr UINT_PTR Timer = 1;
LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK monitorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK friendsProc(HWND window,UINT message,WPARAM wp,LPARAM lp);
LRESULT CALLBACK friendsListProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data);
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
    auto* pending = reinterpret_cast<ChoiceTransaction*>(GetPropW(control,L"FolderstormChoicePending"));
    if (!force && (pending || SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0))) return false;
    if (force && pending) { pending->reset(); RemovePropW(control,L"FolderstormChoicePending"); }
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
std::optional<std::string> choiceNotification(HWND control, ChoiceTransaction& transaction, WORD notification,
                                               const std::vector<std::string>& ids, const std::string& current)
{
    if (notification == CBN_DROPDOWN)
    { transaction.begin(ids,current); SetPropW(control,L"FolderstormChoicePending",&transaction); return {}; }
    if (notification == CBN_SELENDCANCEL)
    { RemovePropW(control,L"FolderstormChoicePending"); return transaction.cancel(); }
    const auto selected = static_cast<int>(SendMessageW(control,CB_GETCURSEL,0,0));
    if (notification == CBN_SELENDOK)
    { RemovePropW(control,L"FolderstormChoicePending"); return transaction.accept(selected); }
    if (notification == CBN_SELCHANGE && !transaction.open && !SendMessageW(control,CB_GETDROPPEDSTATE,0,0) &&
        selected >= 0 && static_cast<std::size_t>(selected) < ids.size()) return ids[static_cast<std::size_t>(selected)];
    return {}; // CLOSEUP never refreshes an uncommitted selection.
}
struct EnvLess { bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; } };
struct ProfileCopyRequest
{
    std::filesystem::path settings, toolbars;
    std::uint32_t groups = 0;
};
struct ProfileSetupScope
{
    bool& open;
    explicit ProfileSetupScope(bool& flag) : open(flag) { open = true; }
    ~ProfileSetupScope() { open = false; }
    ProfileSetupScope(const ProfileSetupScope&) = delete;
    ProfileSetupScope& operator=(const ProfileSetupScope&) = delete;
};
std::vector<wchar_t> environment(const std::filesystem::path& profile, HANDLE pipe, HANDLE lease, HANDLE frames, HANDLE frameMutex, const WorkerId& id, const ProfileCopyRequest& copy, bool pauseAssistant)
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
    entries[L"FOLDERSTORM_SESSION_ASSISTANT_PAUSE"] = pauseAssistant?L"1":L"0";
    entries[L"FOLDERSTORM_SESSION_FRAMES"] = hexHandle(frames);
    entries[L"FOLDERSTORM_SESSION_FRAME_MUTEX"] = hexHandle(frameMutex);
    entries.erase(L"FOLDERSTORM_SESSION_SETTINGS_SOURCE");
    entries.erase(L"FOLDERSTORM_SESSION_PROFILE_SOURCE");
    entries.erase(L"FOLDERSTORM_SESSION_TOOLBARS_SOURCE");
    entries.erase(L"FOLDERSTORM_SESSION_COPY_GROUPS");
    if (copy.groups)
    {
        // Older workers ignore this selective request instead of interpreting
        // a toolbar-only selection as their legacy all-preferences import.
        entries[L"FOLDERSTORM_SESSION_PROFILE_SOURCE"] = copy.settings.wstring();
        entries[L"FOLDERSTORM_SESSION_COPY_GROUPS"] = std::to_wstring(copy.groups);
        if (copy.groups & (CopyToolbars|CopyAccountUI|CopyMovement|CopyAvatar)) entries[L"FOLDERSTORM_SESSION_TOOLBARS_SOURCE"] = copy.toolbars.wstring();
    }
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
    ContactDirectory contacts;
    Message pendingContact;bool hasPendingContact=false;
    ULONGLONG contactsAt=0,friendRefreshAt=0;
    int openIMOrigin=-1;
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
    ULONGLONG eventsAt = 0, catalogAt = 0, typingAt = 0, autoReadAt = 0;
    Pipe pipe;
    Handle process;
    WorkerId id{};
    DWORD pid = 0;
    std::uint64_t nextSequence = 0;
    ULONGLONG sentAt = 0, pollAt = 0, sampleAt = 0;
    Message snapshot, request, sample;
    Message assistant;std::wstring assistantDiagnostic;bool assistantPauseFailed=false;std::uint64_t assistantEditRevision=0;
    ULONGLONG assistantAt=0,assistantSince=0;
    std::string reservation;
    bool waiting = false, detached = false, soundPrepared = false;
    std::string lastImportReport;
    std::string soundAsset;
    std::array<std::wstring,4> alertReasons;
    std::array<ULONGLONG,4> alertAt{};
    std::uint64_t latestChatEvent=0;
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
    fs_host_ui::FriendsPane friends;bool showFriends=false;
    HWND newIM=nullptr,friendsToggle=nullptr,cyclePlay=nullptr,cyclePrevious=nullptr,cycleNext=nullptr,cycleInterval=nullptr;
    HWND label = nullptr, choices = nullptr, history = nullptr, compose = nullptr, send = nullptr, read = nullptr, close = nullptr;
    HWND exchange=nullptr,chatToggle=nullptr,pin=nullptr,options=nullptr,latest=nullptr;
    fs_host_ui::Transcript transcript; fs_host_ui::Composer editor;
    HFONT font = nullptr;
    std::vector<std::string> conversationIds;
    ChoiceTransaction conversationChoice;
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
    HWND densityToggle=nullptr,controlsDock=nullptr,chatDock=nullptr,quickDivider=nullptr,rightDivider=nullptr,quickOverflow=nullptr,mainOverflow=nullptr;
    std::vector<unsigned int> overflowMain;
    std::vector<unsigned int> overflowActions;
    std::size_t attentionCount=0;
    int controlsExpandedHeight=0;
    std::uint64_t layoutState=0;
    AttentionNavigator attentionNavigation;
    SessionSetStore setStore;SessionSetOpen setOpen;
    std::map<std::tuple<std::string,std::string,std::string>,std::uint64_t> unreadSince;
    unsigned int chrome = 0;
    HWND chatWindow = nullptr, controlsWindow = nullptr;
    fs_host_ui::Tooltips tooltips;
    bool chatDetached = false, controlsDetached = false;
    UINT chatDpi = 96, controlsDpi = 96, attentionDpi = 96;
    HFONT chatFont = nullptr, controlsFont = nullptr, attentionFont = nullptr;
    std::wstring descriptions[MaxCharacters], lastNotice,chatSenderTip;
    Handle optionsLease;
    bool logoutOnClose = false;
    std::uintptr_t focusIntent = 0;
    MonitorSet monitorSet;
    std::array<MonitorWindow, MonitorViews> monitors{};
    CyclingMonitor cycler;bool cycleOpen=false,tilingMonitors=false;
    fs_host_ui::FriendsPane sharedFriends;bool showFriends=false;
    std::array<fs_host_ui::FriendsPane,MaxCharacters> friendsWindows;
    HWND chatNewIM=nullptr,chatFriends=nullptr;
    unsigned int monitorSize = 0, monitorRate = 1; // Saved defaults, not assignments.
    bool showChat = true;
    PresentationStore presentation;
    ChatPreferences chatPreferences; ActiveReadClock activeReadClock;
    PinBook pins;
    AttentionBook attention;
    AlertGate alertGate;
    std::vector<std::uint8_t> alertTone;
    std::map<std::string,WavSound> soundCache;
    bool playbackSucceeded=true;
    std::uint64_t playbackSerial = 0;
    HWND pinMenu = nullptr, pinButtons[6]{}, attentionWindow = nullptr, attentionList = nullptr;
    std::vector<Message> attentionRows;
    std::vector<std::wstring> attentionLabels;
    std::wstring pinLabels[6], pinTips[6];
    bool menuOpen = false;
    bool profileSetupOpen = false;
    std::uint64_t profileSetupEpoch = 0;

    HWND chatAccent = nullptr;
    int accentSlot = -1;
    COLORREF accentColor = 0;
    unsigned int handoffStyle = 0, handoffDuration = 1100, handoffHeight = 64;
    bool cinematic = false, handoffAnimated = false, escapeHeld = false;
    UINT dpi = 96;
    HFONT font = nullptr;
    HWND chatAccount = nullptr, chatConversation = nullptr, chatHistory = nullptr, chatCompose = nullptr;
    HWND chatSend = nullptr, chatReview = nullptr, chatRead = nullptr, voiceControl = nullptr, muteControl = nullptr, chatLabel = nullptr;
    HWND chatLatest = nullptr;
    fs_host_ui::Transcript transcript; fs_host_ui::Composer editor;
    int chatIndex = 0;
    std::string conversation;
    std::vector<std::string> conversationIds;
    ChoiceTransaction conversationChoice;
    ChoiceTransaction accountChoice;
    bool updatingChat = false, voice = false, muteBackground = true;
    std::array<Mode, MaxCharacters> standby = warmModes();
    std::unique_ptr<Slot> slots[MaxCharacters];
    Handoff handoff;
    HostLifecycle lifecycle;
    int active = -1;
    bool embedding = false, detaching = false, closing = false, selectFirst = true, focusRequested = false;
    std::filesystem::path viewer, profiles;
    ChoiceAutosave autosave;
    std::wstring saveError;
    bool preferencesOpen=false,pauseAllAssistant=false;
    std::function<void()> refreshAssistantEditor;
    unsigned int preferencesPage=0;
    std::uint64_t autosaveAt=0;
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
        if(sharedFriends.window){friendsFont(sharedFriends);layoutFriends(sharedFriends);}
        for(auto& m:monitors)if(m.window){if(m.friends.window){friendsFont(m.friends);layoutFriends(m.friends);}theme.window(m.window);m.dirty=true;}
        for(auto& p:friendsWindows)if(p.window){friendsFont(p);theme.window(p.window);layoutFriends(p);}
        tooltips.style(theme);
    }
    std::wstring quickLabel(unsigned int id) const
    {
        const auto* c=shellCommand(id);if(!c)return L"";
        return c->label;
    }
    unsigned int windowKey(HWND panel) const
    {
        if(panel==chatWindow)return 1;if(panel==controlsWindow)return 2;if(panel==attentionWindow)return 7;
        for(unsigned int i=0;i<MonitorViews;++i)if(panel==monitors[i].window)return i==CyclingView?8:i+3;
        for(unsigned int i=0;i<MaxCharacters;++i)if(panel==friendsWindows[i].window)return 9+i;
        return 0;
    }
    int pinSlot(const ChatPin& pin) const
    {
        for (int i = 0; i < MaxCharacters; ++i) if (slots[i] && slots[i]->running() && !slots[i]->detached && slots[i]->pipe.alive() && slots[i]->snapshot.state == State::Ready &&
            !(slots[i]->snapshot.flags & ChatRestricted) && pin.live(slots[i]->chat)) return i;
        return -1;
    }
    int sourceSlot(const Message& item) const
    {
        for (int i = 0; i < MaxCharacters; ++i) if (slots[i] && slots[i]->chat.identity.owns(item)) return i;
        return -1;
    }
    enum PreferencePage : unsigned int { PrefLayout,PrefAppearance,PrefCharacter,PrefSounds,PrefViewer,PrefSession,
        PrefConversations,PrefAttention,PrefReading,PrefAudio,PrefSwitching,PrefMonitors,PrefQuick,PrefKeys,PrefDiagnostics,PrefRecovery,PrefAssistant };
    void editPreferences(unsigned int page=~0u,int target=-1)
    {
        if(preferencesOpen)return;
        if(page==~0u)page=preferencesPage;
        using namespace fs_host_ui;
        struct ProfileDraft
        {
            Message source;bool ready=false,imported=false,attention=true;
            std::wstring label,alias;unsigned int color=0,categories=0;Mode mode=Mode::Warm;
            std::string asset,soundName;WavSound wav;
            CharacterAppearance originalAppearance;CharacterSound originalSound;AssistantPreferenceDraft assistant;bool assistantPending=false;std::uint64_t assistantRevision=0;
        };
        std::array<ProfileDraft,MaxCharacters> people;
        auto originalLabels=chatPreferences.profileNames;auto originalModes=standby;auto draftOrder=experience.order;
        std::vector<std::wstring> names;
        for(int i=0;i<MaxCharacters;++i)
        {
            auto& p=people[i];p.attention=(experience.accountMask&(1u<<i))!=0;p.label=wide(chatPreferences.profileNames[i]);p.mode=standby[i];names.push_back(characterName(i));
            p.ready=slots[i] && slots[i]->snapshot.state==State::Ready;
            if(p.ready){p.assistant.capture(slots[i]->assistant,slots[i]->snapshot);p.assistantRevision=slots[i]->assistantEditRevision;}
            if(p.ready){p.source=slots[i]->snapshot;p.originalAppearance=appearance(i);p.originalSound=characterSound(i);p.alias=wide(p.originalAppearance.alias);p.color=p.originalAppearance.color;p.categories=p.originalSound.categories;p.asset=p.originalSound.asset;p.soundName=p.originalSound.name;}
        }
        int selected=validSlot(target)?target:lifecycle.selection();if(!validSlot(selected))selected=0;
        std::vector<Field> fields;
        const auto add=[&](unsigned int category,Field field){field.page=category;fields.push_back(std::move(field));return fields.size()-1;};
        const std::vector<EditorPage> navigation{
            {0,L"Layout & theme",L"Layout",L""},{0,L"Layout & theme",L"Appearance",L""},
            {1,L"Character settings",L"Profile",L"Each character profile keeps separate viewer settings."},
            {1,L"Character settings",L"Sounds",L"Alerts for this account while another character is active."},
            {1,L"Character settings",L"Viewer settings",L"Copy settings or open this character's workspace controls."},
            {1,L"Character settings",L"Session",L""},
            {2,L"Chats & notifications",L"Conversations",L"Choose what appears in shared and monitor Chat. Applies to all characters."},
            {2,L"Chats & notifications",L"Attention",L"Choose which chats and offers appear in Attention. Applies to all characters."},
            {2,L"Chats & notifications",L"Reading",L""},
            {3,L"Audio & voice",L"Audio",L""},
            {4,L"Switching & monitors",L"Switching",L"Choose the visual effect when switching characters."},
            {4,L"Switching & monitors",L"Monitors",L"Read-only views of inactive characters. These choices set defaults for new monitors."},
            {5,L"Controls",L"Quick actions",L"Quick actions add buttons to the host toolbar. Choose actions below, then click their buttons during play."},
            {5,L"Controls",L"Keyboard shortcuts",L""},
            {6,L"Advanced",L"Diagnostics",L""},{6,L"Advanced",L"Files & recovery",L""},
            {1,L"Character settings",L"Local Assistant",L"Connect an MCP assistant to this character. Access and permissions stay separate for each profile."}};
        const auto chromeField=add(PrefLayout,{L"Character controls",std::to_wstring(chrome==2?1:chrome),FieldType::Choice,{L"Expanded",L"Condensed"}});
        const auto areaField=add(PrefLayout,{L"Docked controls area",std::to_wstring(experience.dockedArea),FieldType::Choice,{L"Automatic",L"Full window",L"Left half",L"Right half",L"Left third",L"Middle third",L"Right third"}});
        fields[areaField].help=L"Automatic uses the right half for a broad two-monitor span and the middle third for three. Explicit choices override detection; this moves host controls only.";
        const auto hostedField=add(PrefLayout,{L"Show viewer inside host",embedding?L"1":L"0",FieldType::Check});
        fields[hostedField].help=L"Fit the active viewer inside the host. Turn off to use separate viewer windows while retaining character switching.";
        const auto chatField=add(PrefLayout,{L"Shared Chat",std::to_wstring(!showChat?0:chatDetached?2:1),FieldType::Choice,{L"Hidden",L"Docked",L"Separate window"}});
        const auto controlsDetachedField=add(PrefLayout,{L"Separate character controls",controlsDetached?L"1":L"0",FieldType::Check});
        const auto themeField=add(PrefAppearance,{L"Theme",std::to_wstring(experience.theme),FieldType::Choice,{L"Folderstorm Midnight",L"Folderstorm Daylight",L"Follow Windows"}});
        const auto densityField=add(PrefAppearance,{L"Button spacing",std::to_wstring(experience.density),FieldType::Choice,{L"Compact",L"Comfortable"}});
        const auto cardsField=add(PrefAppearance,{L"Character cards in Expanded mode",experience.cards?L"1":L"0",FieldType::Check});
        const auto labelsField=add(PrefQuick,{L"Button display",std::to_wstring(experience.labels),FieldType::Choice,{L"Icon and text",L"Text only",L"Icons only"}});
        fields[labelsField].help=L"Docked quick actions share the toolbar row. Labels collapse as space runs out; Attention becomes its count.";
        const auto profileField=add(PrefCharacter,{L"Character",std::to_wstring(selected),FieldType::Choice,names});
        fields[profileField].fixed=true;fields[profileField].transient=true;
        const auto labelField=add(PrefCharacter,{L"Character name",L""});
        fields[labelField].help=L"Name this saved character slot, including when offline. This does not change the account you log in as.";
        const auto modeField=add(PrefCharacter,{L"Background mode",L"0",FieldType::Choice,{L"Warm",L"Economy (experimental)"}});
        fields[modeField].help=L"Warm retains rendering buffers. Economy releases disposable render targets and rebuilds them when needed. Both keep the account connected; savings vary.";
        const auto aliasField=add(PrefCharacter,{L"Account alias (optional)",L""});
        fields[aliasField].help=L"Optional host label for this logged-in account. It follows the account; the character name follows its saved slot. Actual sending accounts remain identified in Chat.";
        const auto colorField=add(PrefCharacter,{L"Character color",L"0",FieldType::Color});
        fields[colorField].help=L"Tint this account's cards, Chat and monitors. Log in to edit.";
        const auto accountNote=add(PrefCharacter,{L"",L"",FieldType::Note});fields[accountNote].rowHeight=44;
        const auto positionField=add(PrefCharacter,{L"Card position",L"0",FieldType::Choice,{L"1",L"2",L"3",L"4",L"5"}});
        UINT pendingCommand=0;int pendingTarget=-1;Slot* pendingSlot=nullptr;Message pendingIdentity;
        const auto queueAction=[&](HWND owner,UINT command)
        {
            const int index=selected;auto* slot=slots[index].get();const Message identity=slot?slot->snapshot:Message{};
            if(finishEditorAction(owner)){pendingCommand=command;pendingTarget=index;pendingSlot=slot;pendingIdentity=identity;}
        };
        const auto copyField=add(PrefViewer,{L"Copy viewer settings",L"Copy settings…",FieldType::Action});
        fields[copyField].help=L"Close this character before copying. Choose a source installation or backup; copying opens a normal login.";
        fields[copyField].action=[&](HWND owner){queueAction(owner,277);};
        const auto workspaceField=add(PrefViewer,{L"Workspaces",L"Open workspace controls…",FieldType::Action});
        fields[workspaceField].action=[&](HWND owner){queueAction(owner,201);};
        const auto profileFolderField=add(PrefViewer,{L"Profile files",L"Open character folder",FieldType::Action});
        fields[profileFolderField].action=[&](HWND owner){const auto path=profiles/(L"Character"+std::to_wstring(selected+1));ShellExecuteW(owner,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);};
        const auto reportField=add(PrefViewer,{L"Last settings copy",L"Open copy report",FieldType::Action});
        fields[reportField].action=[&](HWND owner)
        {
            const auto root=profiles/(L"Character"+std::to_wstring(selected+1))/L"Roaming";std::error_code error;
            std::filesystem::directory_iterator item(root,error),end;unsigned int count=0;
            while(!error && item!=end && count++<32)
            {
                const auto file=item->path()/L"user_settings"/L"settings-import-report.txt";
                if(std::filesystem::is_regular_file(file,error)&&!error){ShellExecuteW(owner,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}
                error.clear();item.increment(error);
            }
            MessageBoxW(owner,L"No settings-copy report is available yet.",L"Folderstorm",MB_OK|MB_ICONINFORMATION);
        };
        const auto sessionNote=add(PrefSession,{L"",characterName(selected),FieldType::Note});
        const auto loginField=add(PrefSession,{L"Login",L"Open login",FieldType::Action});fields[loginField].action=[&](HWND owner){queueAction(owner,LaunchBase+static_cast<UINT>(selected));};
        const auto restartField=add(PrefSession,{L"Restart",L"Restart character…",FieldType::Action});fields[restartField].action=[&](HWND owner){queueAction(owner,240);};
        const auto cancelRestartField=add(PrefSession,{L"Pending restart",L"Cancel restart",FieldType::Action});fields[cancelRestartField].action=[&](HWND owner){queueAction(owner,241);};
        const auto logoutField=add(PrefSession,{L"Logout",L"Log out character…",FieldType::Action});fields[logoutField].action=[&](HWND owner){queueAction(owner,202);};
        const auto assistantEnableField=add(PrefAssistant,{L"Enable Local Assistant",L"0",FieldType::Check});
        fields[assistantEnableField].help=L"Uses this profile's native Local Assistant setting. Apply sends only edited accounts; native session-only drafts are preserved.";
        const auto assistantStatusField=add(PrefAssistant,{L"",L"Log in to configure Local Assistant.",FieldType::Note});fields[assistantStatusField].rowHeight=90;
        const auto assistantPermissionsField=add(PrefAssistant,{L"Permissions",L"Open native permissions…",FieldType::Action});
        fields[assistantPermissionsField].help=L"Activates this character and opens its native Local Assistant permissions. This never grants access or approves a request.";
        fields[assistantPermissionsField].action=[&](HWND owner){queueAction(owner,3901);};
        const auto assistantReviewField=add(PrefAssistant,{L"Pending requests",L"Review in viewer…",FieldType::Action});
        fields[assistantReviewField].action=[&](HWND owner){queueAction(owner,3902);};
        const auto assistantClientField=add(PrefAssistant,{L"MCP client",L"0",FieldType::Choice,{L"Codex",L"Claude Desktop",L"Cursor"}});fields[assistantClientField].transient=true;
        const auto assistantCopyField=add(PrefAssistant,{L"Configuration",L"Copy MCP configuration",FieldType::Action});
        fields[assistantCopyField].help=L"Copies a token-free entry for the matching installed sidecar and stable hosted registry. Each managed tool call must use its exact viewer_session.";
        const auto assistantCheckField=add(PrefAssistant,{L"Connection",L"Check connection",FieldType::Action});
        fields[assistantCheckField].help=L"Runs a bounded authenticated sidecar health check for this exact login. No inventory data or tool actions are requested.";
        const auto assistantPauseField=add(PrefAssistant,{L"Pause all MCP access",pauseAllAssistant?L"1":L"0",FieldType::Check});
        fields[assistantPauseField].help=L"Runtime only. Apply revokes access and pending work in managed viewers; saved enable choices and permissions stay intact. Resume creates fresh connections. Newly launched characters inherit the pause.";
        const auto showStart=fields.size();
        for(const auto* name:{L"Nearby Chat",L"Direct IMs",L"Group Chats",L"Conferences",L"Viewer Notifications"})
            add(PrefConversations,{name,(chatPreferences.showMask&(1u<<static_cast<unsigned int>(fields.size()-showStart)))?L"1":L"0",FieldType::Check});
        for(unsigned int i=0;i<5;++i)fields[showStart+i].help=L"Hides this category in host Chat without marking it read or leaving native conversations. Sound choices are separate.";
        const auto attentionStart=fields.size();
        for(const auto* name:{L"Nearby Chat",L"Direct IMs",L"Group Chats",L"Conferences",L"Viewer Notifications",L"Teleport offers",L"Permission requests",L"Inventory offers"})
            add(PrefAttention,{name,(chatPreferences.attentionMask&(1u<<static_cast<unsigned int>(fields.size()-attentionStart)))?L"1":L"0",FieldType::Check});
        const auto inboxField=add(PrefAttention,{L"Attention inbox",L"Open inbox",FieldType::Action});fields[inboxField].action=[&](HWND owner){queueAction(owner,274);};
        const auto oldestField=add(PrefAttention,{L"Oldest first",experience.oldestFirst?L"1":L"0",FieldType::Check});
        const auto accountField=add(PrefCharacter,{L"Include in Attention",L"1",FieldType::Check});fields[accountField].help=L"Include this character in the Attention inbox and its counts. Category filters are configured under Chats & notifications.";
        const auto visibleReadField=add(PrefReading,{L"Mark displayed chats read",chatPreferences.visibleRead?L"1":L"0",FieldType::Check});
        fields[visibleReadField].help=L"Read visible conversations while their host Chat panel is open and readable. Hidden or minimized panels pause this behavior.";
        const auto activeReadField=add(PrefReading,{L"Auto-read active character",chatPreferences.activeRead?L"1":L"0",FieldType::Check});
        const auto delayField=add(PrefReading,{L"Read delay (seconds)",std::to_wstring(chatPreferences.delay),FieldType::Number,{},5,3600});
        fields[activeReadField].help=L"Mark eligible chats read after playing the active character for this interval. This never accepts offers or permissions.";
        const auto voiceField=add(PrefAudio,{L"Voice follows active character",voice?L"1":L"0",FieldType::Check});
        fields[voiceField].help=L"Only the active character owns the microphone. Native voice preferences and restrictions still apply.";
        const auto muteField=add(PrefAudio,{L"Mute background world audio",muteBackground?L"1":L"0",FieldType::Check});
        fields[muteField].help=L"Mute inactive characters' world, UI and media audio. Host notification alerts use their own volume and character choices.";
        const auto volumeField=add(PrefAudio,{L"Alert volume (%)",std::to_wstring(presentation.volume),FieldType::Number,{},0,100});
        const auto soundStart=fields.size();
        for(const auto* name:{L"Direct IM alerts",L"Group chat alerts",L"Conference alerts",L"Notification alerts"})add(PrefSounds,{name,L"0",FieldType::Check});
        fields[soundStart+3].help=L"Supported viewer notifications and offers. Muting, Do Not Disturb, restrictions and burst limits still apply.";
        const auto wavField=add(PrefSounds,{L"Notification sound",L"Choose WAV…",FieldType::Action});
        const auto wavNote=add(PrefSounds,{L"",L"Built-in tone",FieldType::Note});fields[wavNote].rowHeight=44;
        const auto previewField=add(PrefSounds,{L"Preview",L"Play at host volume",FieldType::Action});
        const auto resetSoundField=add(PrefSounds,{L"Reset sound",L"Use built-in tone",FieldType::Action});
        const auto transitionField=add(PrefSwitching,{L"Switch effect",std::to_wstring(presentation.transition),FieldType::Choice,{L"Instant",L"Bird's-eye",L"Fade"}});
        const auto durationField=add(PrefSwitching,{L"Animation time (ms)",std::to_wstring(presentation.duration),FieldType::Number,{},250,2000});fields[durationField].help=L"Time for each zoom or fade stage.";
        const auto heightField=add(PrefSwitching,{L"Height above avatar (m)",std::to_wstring(presentation.height),FieldType::Number,{},16,96});
        const auto sizeField=add(PrefMonitors,{L"Image size",std::to_wstring(monitorSize),FieldType::Choice,{L"320 × 180",L"480 × 270",L"640 × 360"}});
        const auto cycleSecondsField=add(PrefMonitors,{L"Cycle interval (seconds)",std::to_wstring(experience.cycleSeconds),FieldType::Number,{},2,3600});fields[cycleSecondsField].help=L"The separate cycling monitor visits inactive characters in card order. It never changes the active world.";
        const auto rateField=add(PrefMonitors,{L"Refresh rate",std::to_wstring(monitorRate),FieldType::Choice,{L"0.5 FPS",L"1 FPS",L"2 FPS",L"5 FPS"}});fields[rateField].help=L"Higher rates increase preview rendering and readback work. Open or close monitors from the toolbar camera button.";
        const unsigned int modifiers[]={5,6,3,7};const std::vector<unsigned int> keys{0,49,50,51,52,53,39,37,112,113,114,115,116,117,118,119,120,121,122,123};
        const std::vector<std::wstring> keyNames{L"Disabled",L"1",L"2",L"3",L"4",L"5",L"Right",L"Left",L"F1",L"F2",L"F3",L"F4",L"F5",L"F6",L"F7",L"F8",L"F9",L"F10",L"F11",L"F12"};
        const auto shortcutField=add(PrefKeys,{L"Enable character shortcuts",presentation.shortcuts?L"1":L"0",FieldType::Check});
        unsigned int modifier=0;for(unsigned int i=0;i<4;++i)if(presentation.bindings[0].modifiers==modifiers[i])modifier=i;
        const auto modifierField=add(PrefKeys,{L"Modifier keys",std::to_wstring(modifier),FieldType::Choice,{L"Alt + Shift",L"Ctrl + Shift",L"Ctrl + Alt",L"Ctrl + Alt + Shift"}});
        const auto keysStart=fields.size();
        for(unsigned int i=0;i<10;++i)
        {
            const auto key=i<7?presentation.bindings[i].key:experience.keys[i-7].key;const auto found=std::find(keys.begin(),keys.end(),key);
            const auto label=i<5?profileName(static_cast<int>(i)):i==5?L"Next character":i==6?L"Previous character":i==7?L"Restore controls":i==8?L"Next attention":L"Previous attention";
            add(PrefKeys,{label,std::to_wstring(found==keys.end()?0:static_cast<unsigned int>(found-keys.begin())),FieldType::Choice,keyNames});
        }
        const auto viewerKeysField=add(PrefKeys,{L"Use recovery / attention keys in viewer",experience.viewerShellKeys?L"1":L"0",FieldType::Check});
        fields[viewerKeysField].help=L"Allow the configured recovery and attention shortcuts while playing. Viewer movement and typing bindings are configured in that viewer's Preferences.";
        const auto quickPreview=add(PrefQuick,{L"",L"",FieldType::Note});fields[quickPreview].rowHeight=84;
        std::vector<const ShellCommandInfo*> quickChoices;std::vector<std::wstring> commands{L"Unused"};
        for(const auto& command:shellCommands())
        {
            if(command.id==206 && std::find(experience.quick.begin(),experience.quick.end(),206)==experience.quick.end())continue;
            quickChoices.push_back(&command);commands.push_back(command.label);
        }
        const auto quickStart=fields.size();
        for(std::size_t i=0;i<12;++i)
        {
            unsigned int choice=0;if(i<experience.quick.size())for(std::size_t j=0;j<quickChoices.size();++j)if(quickChoices[j]->id==experience.quick[i])choice=static_cast<unsigned int>(j)+1;
            const auto field=add(PrefQuick,{L"Position "+std::to_wstring(i+1),std::to_wstring(choice),FieldType::Choice,commands});fields[field].rowHeight=28;
        }
        const auto cycleLockField=add(PrefDiagnostics,{L"Require pause to chat",experience.requirePauseToChat?L"1":L"0",FieldType::Check});fields[cycleLockField].help=L"Cycling monitors disable Chat until paused. When unchecked, focusing the composer pauses before typing; Play resumes explicitly.";
        const auto diagnosticsField=add(PrefDiagnostics,{L"Show diagnostics",experience.diagnostics?L"1":L"0",FieldType::Check});
        const auto chatReportField=add(PrefDiagnostics,{L"Chat and alerts",L"Show report…",FieldType::Action});
        fields[chatReportField].help=L"Applied choices and the latest alert decision for each character. No message text is included.";
        fields[chatReportField].action=[&](HWND owner){chatAlertReport(owner);};
        const auto minimalField=add(PrefRecovery,{L"Minimal host controls",chrome==2?L"1":L"0",FieldType::Check});fields[minimalField].help=L"Advanced: hide the regular toolbar. Show controls, the tray and recovery shortcuts remain available.";
        const auto folderField=add(PrefRecovery,{L"Host settings",L"Open settings folder",FieldType::Action});fields[folderField].action=[&](HWND owner){ShellExecuteW(owner,L"open",profiles.c_str(),nullptr,nullptr,SW_SHOWNORMAL);};
        const auto licenseField=add(PrefRecovery,{L"Icon credits",L"Lucide license",FieldType::Action});fields[licenseField].action=[&](HWND owner){MessageBoxW(owner,wide(fs_host_ui::lucideLicense).c_str(),L"Lucide icon license",MB_OK|MB_ICONINFORMATION);};
        const auto saveField=add(PrefRecovery,{L"Saving",L"Retry saving",FieldType::Action});fields[saveField].enabled=!saveError.empty();fields[saveField].help=L"Retry saving already applied choices. Use Apply for the changes currently being edited.";fields[saveField].action=[&](HWND owner)
        {
            const bool saved=saveOptions();fields[saveField].enabled=!saved;EnableWindow(fields[saveField].control,!saved);EnableWindow(fields[saveField].caption,!saved);
            if(auto* editor=currentEditor(owner))SetWindowTextW(editor->savedLabel,saved?(editor->dirty?L"Applied choices saved":L"Saved"):L"Not saved");
        };
        const auto recoverField=add(PrefRecovery,{L"Lost windows",L"Recover window positions",FieldType::Action});fields[recoverField].action=[&](HWND owner){queueAction(owner,RecoverShell);};
        const auto setField=add(PrefRecovery,{L"Session sets",L"Manage session sets…",FieldType::Action});fields[setField].action=[&](HWND owner){queueAction(owner,SetActions);};
        const auto stopField=add(PrefRecovery,{L"Character management",L"Stop managing…",FieldType::Action});fields[stopField].action=[&](HWND owner){queueAction(owner,209);};
        const auto readField=[&](std::size_t at)
        {
            auto& field=fields[at];if(!field.control)return field.value;
            if(field.type==FieldType::Check)return std::wstring(SendMessageW(field.control,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0");
            if(field.type==FieldType::Choice)return std::to_wstring(SendMessageW(field.control,CB_GETCURSEL,0,0));
            if(field.type==FieldType::Color)return field.value;
            std::vector<wchar_t> text(static_cast<std::size_t>(GetWindowTextLengthW(field.control))+1);GetWindowTextW(field.control,text.data(),static_cast<int>(text.size()));return std::wstring(text.data());
        };
        const auto assistantAction=[&](unsigned int operation)
        {
            if(!people[selected].ready || !slots[selected] || !ChatKey{people[selected].source.worker,people[selected].source.generation,people[selected].source.account,people[selected].source.grid,{}}.owns(slots[selected]->snapshot) || slots[selected]->hasPendingAction)
            {message(L"Local Assistant source changed or another action is pending. Try again.");return;}
            auto action=people[selected].source;action.kind=Kind::AssistantInfo;action.unread=operation;
            const auto client=static_cast<unsigned int>(std::wcstoul(readField(assistantClientField).c_str(),nullptr,10));
            action.text=client==1?"claude":client==2?"cursor":"codex";
            slots[selected]->pendingAction=action;slots[selected]->hasPendingAction=true;
            message(operation==1?L"Preparing token-free MCP configuration…":L"Checking Local Assistant connection…");
        };
        fields[assistantCopyField].action=[&](HWND){assistantAction(1);};
        fields[assistantCheckField].action=[&](HWND){assistantAction(2);};
        const auto refreshAssistant=[&]()
        {
            auto& p=people[selected];auto* slot=slots[selected].get();
            const bool owned=p.ready && slot && slot->running() && slot->pipe.alive() && !slot->detached && assistantInfoOwned(slot->assistant,p.source) && assistantInfoOwned(slot->assistant,slot->snapshot);
            if(owned && p.assistantRevision!=slot->assistantEditRevision){p.assistant={};p.assistantPending=false;p.assistantRevision=slot->assistantEditRevision;}
            if(owned && p.assistant.capture(slot->assistant,p.source))
            {fields[assistantEnableField].value=p.assistant.enabled?L"1":L"0";if(fields[assistantEnableField].control)SendMessageW(fields[assistantEnableField].control,BM_SETCHECK,p.assistant.enabled?BST_CHECKED:BST_UNCHECKED,0);}
            const bool editable=owned && !p.assistantPending && p.assistant.known && !(p.assistant.original.width&AssistantSessionOnly);
            fields[assistantEnableField].enabled=editable;if(fields[assistantEnableField].control)EnableWindow(fields[assistantEnableField].control,editable);
            for(const auto at:{assistantPermissionsField,assistantCopyField,assistantCheckField,assistantReviewField})
            {fields[at].enabled=owned && (at!=assistantReviewField || slot->assistant.unread>0);if(fields[at].control)EnableWindow(fields[at].control,fields[at].enabled);}
            std::wstring text=L"Log in and wait for Local Assistant status.";
            if(owned)
            {
                const auto& info=slot->assistant;text=(info.width&AssistantPaused)?L"Paused":(info.width&AssistantReady)?L"Ready":(info.width&AssistantEnabled)?L"Enabled · unavailable":L"Off";
                if(((info.width&AssistantPaused)!=0)!=pauseAllAssistant)text+=L" · pause change pending";
                text+=L" · "+wide(p.source.name)+L" · "+wide(p.source.grid)+L"\n"+wide(info.sender);
                if(info.width&AssistantSessionOnly)text+=L" · session-only settings; edit in viewer";
                if(info.unread)text+=L"\n"+std::to_wstring(info.unread)+L" pending request(s)";
                if(!info.detail.empty())text+=L"\n"+wide(info.detail);
                if(!slot->assistantDiagnostic.empty())text+=L"\n"+slot->assistantDiagnostic;
            }
            fields[assistantStatusField].value=text;if(fields[assistantStatusField].control)SetWindowTextW(fields[assistantStatusField].control,text.c_str());
        };
        const auto remember=[&](bool controls)
        {
            auto& p=people[selected];const auto value=[&](std::size_t i){return controls?readField(i):fields[i].value;};
            if(p.assistant.known)p.assistant.enabled=value(assistantEnableField)==L"1";
            p.attention=value(accountField)==L"1";p.label=value(labelField);p.alias=value(aliasField);p.color=static_cast<unsigned int>(std::wcstoul(value(colorField).c_str(),nullptr,10));p.mode=value(modeField)==L"1"?Mode::Economy:Mode::Warm;
            p.categories=0;for(unsigned int i=0;i<4;++i)if(value(soundStart+i)==L"1")p.categories|=1u<<i;
        };
        const auto showPerson=[&](HWND owner=nullptr)
        {
            EditorUpdate update(owner);
            const auto& p=people[selected];const auto set=[&](std::size_t at,const std::wstring& value){auto& field=fields[at];field.value=value;if(!field.control)return;if(field.type==FieldType::Check)SendMessageW(field.control,BM_SETCHECK,number(field)?BST_CHECKED:BST_UNCHECKED,0);else if(field.type==FieldType::Choice)SendMessageW(field.control,CB_SETCURSEL,number(field),0);else if(field.type==FieldType::Color)InvalidateRect(field.control,nullptr,TRUE);else SetWindowTextW(field.control,value.c_str());};
            set(assistantEnableField,p.assistant.enabled?L"1":L"0");
            set(profileField,std::to_wstring(selected));set(positionField,std::to_wstring(std::find(draftOrder.begin(),draftOrder.end(),static_cast<unsigned int>(selected))-draftOrder.begin()));set(accountField,p.attention?L"1":L"0");set(labelField,p.label);set(aliasField,p.alias);set(colorField,std::to_wstring(p.color));set(modeField,p.mode==Mode::Economy?L"1":L"0");
            for(unsigned int i=0;i<4;++i){set(soundStart+i,p.categories&(1u<<i)?L"1":L"0");fields[soundStart+i].enabled=p.ready;if(fields[soundStart+i].control)EnableWindow(fields[soundStart+i].control,p.ready);}
            for(const auto at:{aliasField,colorField,wavField,previewField,resetSoundField}){fields[at].enabled=p.ready;if(fields[at].control)EnableWindow(fields[at].control,p.ready);}
            fields[copyField].enabled=!slots[selected] || !slots[selected]->running();if(fields[copyField].control)EnableWindow(fields[copyField].control,fields[copyField].enabled);
            fields[wavNote].value=!p.ready?L"Log in to choose account alerts and sound.":p.asset.empty()&&!p.imported?L"Built-in tone":p.soundName.empty()?L"Custom WAV":wide(p.soundName);
            if(fields[wavNote].control)SetWindowTextW(fields[wavNote].control,fields[wavNote].value.c_str());
            fields[accountNote].value=p.ready?L"Logged in as "+wide(p.source.name):L"Log in to edit account color and sound.";
            if(fields[accountNote].control)SetWindowTextW(fields[accountNote].control,fields[accountNote].value.c_str());
            if(fields[sessionNote].control)SetWindowTextW(fields[sessionNote].control,characterName(selected).c_str());
            const bool running=slots[selected] && slots[selected]->running();
            for(const auto at:{loginField,restartField,cancelRestartField,logoutField,workspaceField})
            {
                fields[at].enabled=at==loginField?!running:at==workspaceField?p.ready:at==cancelRestartField?slots[selected]&&slots[selected]->restart.phase()!=Restart::Phase::Idle:running;
                if(fields[at].control)EnableWindow(fields[at].control,fields[at].enabled);
            }
        };
        fields[profileField].changed=[&](HWND owner){const int next=static_cast<int>(SendMessageW(fields[profileField].control,CB_GETCURSEL,0,0));remember(true);if(validSlot(next))selected=next;showPerson(owner);refreshAssistant();};
        fields[positionField].changed=[&](HWND owner)
        {
            remember(true);const auto position=static_cast<std::size_t>(SendMessageW(fields[positionField].control,CB_GETCURSEL,0,0));
            const auto found=std::find(draftOrder.begin(),draftOrder.end(),static_cast<unsigned int>(selected));const auto old=static_cast<std::size_t>(found-draftOrder.begin());
            if(position<draftOrder.size()){if(position<old)std::rotate(draftOrder.begin()+position,found,found+1);else if(position>old)std::rotate(found,found+1,draftOrder.begin()+position+1);}showPerson(owner);
        };
        fields[wavField].action=[&](HWND owner)
        {
            if(!people[selected].ready)return;remember(true);std::vector<wchar_t> file(32768);OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=owner;picker.lpstrFilter=L"PCM WAV (*.wav)\0*.wav\0\0";picker.lpstrFile=file.data();picker.nMaxFile=static_cast<DWORD>(file.size());picker.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
            if(!GetOpenFileNameW(&picker))return;WavSound sound;if(!readSound(std::filesystem::path(file.data()),sound)){MessageBoxW(owner,L"Choose a PCM WAV of at most 10 seconds / 4 MiB.",L"Sound not imported",MB_OK);return;}
            people[selected].wav=std::move(sound);people[selected].imported=true;people[selected].soundName=boundedText(utf8(std::filesystem::path(file.data()).filename().wstring()),128);dirtyEditor(owner);showPerson(owner);
        };
        std::uint64_t preview=0;
        fields[previewField].action=[&](HWND)
        {
            const auto volume=static_cast<unsigned int>(std::wcstoul(readField(volumeField).c_str(),nullptr,10));if(volume>100)return;
            auto& p=people[selected];if(p.wav.bytes.empty()&&!p.asset.empty()&&!readSound(profiles/L"sounds"/wide(p.asset),p.wav))
            {message(L"Saved alert sound is unavailable. Choose another WAV or reset the sound.");return;}
            preview=playAlert(p.wav.bytes.empty()?builtinAlertTone(volume):p.wav.volume(volume));
        };
        fields[resetSoundField].action=[&](HWND owner){remember(true);people[selected].wav={};people[selected].asset.clear();people[selected].soundName.clear();people[selected].imported=false;dirtyEditor(owner);showPerson(owner);};
        const auto dependencies=[&](HWND)
        {
            const bool read=readField(activeReadField)==L"1";fields[delayField].enabled=read;if(fields[delayField].control)EnableWindow(fields[delayField].control,read);
            const auto effect=readField(transitionField);fields[durationField].enabled=effect!=L"0";fields[heightField].enabled=effect==L"1";
            for(const auto at:{durationField,heightField})if(fields[at].control)EnableWindow(fields[at].control,fields[at].enabled);
        };
        fields[activeReadField].changed=dependencies;fields[transitionField].changed=dependencies;
        const auto updateQuick=[&](HWND owner)
        {
            EditorUpdate update(owner);std::wstring full;
            for(unsigned int i=0;i<12;++i)
            {
                const auto n=static_cast<unsigned int>(std::wcstoul(readField(quickStart+i).c_str(),nullptr,10));if(!n || n>quickChoices.size())continue;const auto& command=*quickChoices[n-1];
                if(!full.empty())full+=L" | ";full+=command.label;
            }
            fields[quickPreview].value=full.empty()?L"Toolbar preview: no quick actions":L"Toolbar: "+full;
            if(fields[quickPreview].control)SetWindowTextW(fields[quickPreview].control,fields[quickPreview].value.c_str());
        };
        for(unsigned int i=0;i<12;++i)fields[quickStart+i].changed=updateQuick;
        showPerson();refreshAssistant();dependencies(nullptr);updateQuick(nullptr);HostPreferences accepted;

        const auto validate=[&](HWND owner)
        {
            remember(false);auto next=preferences();const auto n=[&](std::size_t at){return number(fields[at]);};
            next.options.chrome=n(minimalField)?2:n(chromeField);next.options.hosted=n(hostedField)!=0;next.options.chat=n(chatField)!=0;next.options.chatDetached=n(chatField)==0?chatDetached:n(chatField)==2;next.options.controlsDetached=n(controlsDetachedField)!=0;
            next.experience.order=draftOrder;next.experience.theme=n(themeField);next.experience.density=n(densityField);next.experience.cards=n(cardsField)!=0;next.experience.labels=n(labelsField);next.experience.diagnostics=n(diagnosticsField)!=0;next.experience.dockedArea=n(areaField);next.experience.cycleSeconds=n(cycleSecondsField);next.experience.requirePauseToChat=n(cycleLockField)!=0;
            next.chat.haveCategories=true;next.chat.showMask=next.chat.attentionMask=0;for(unsigned int i=0;i<5;++i)if(n(showStart+i))next.chat.showMask|=1u<<i;for(unsigned int i=0;i<8;++i)if(n(attentionStart+i))next.chat.attentionMask|=1u<<i;
            next.chat.visibleRead=n(visibleReadField)!=0;next.chat.activeRead=n(activeReadField)!=0;next.chat.delay=n(activeReadField)?n(delayField):chatPreferences.delay;next.experience.oldestFirst=n(oldestField)!=0;next.experience.accountMask=0;for(unsigned int i=0;i<MaxCharacters;++i)if(people[i].attention)next.experience.accountMask|=1u<<i;
            next.options.voice=n(voiceField)!=0;next.options.muteBackground=n(muteField)!=0;next.presentation.volume=n(volumeField);next.presentation.transition=n(transitionField);next.presentation.duration=n(transitionField)?n(durationField):presentation.duration;next.presentation.height=n(transitionField)==1?n(heightField):presentation.height;next.options.previewSize=n(sizeField);next.options.previewRate=n(rateField);next.options.cinematic=next.presentation.transition==1;
            next.presentation.shortcuts=n(shortcutField)!=0;next.experience.viewerShellKeys=n(viewerKeysField)!=0;for(unsigned int i=0;i<7;++i){const auto key=keys[n(keysStart+i)];next.presentation.bindings[i]={key,key!=presentation.bindings[i].key || n(modifierField)!=modifier?modifiers[n(modifierField)]:presentation.bindings[i].modifiers};}for(unsigned int i=0;i<3;++i){const auto key=keys[n(keysStart+7+i)];next.experience.keys[i]={key,key!=experience.keys[i].key || n(modifierField)!=modifier?modifiers[n(modifierField)]:experience.keys[i].modifiers};}
            next.experience.quick.clear();for(unsigned int i=0;i<12;++i)if(n(quickStart+i))next.experience.quick.push_back(quickChoices[n(quickStart+i)-1]->id);
            for(int i=0;i<MaxCharacters;++i)
            {
                const auto& p=people[i];const auto label=utf8(p.label);if(label.size()>64 || (!p.label.empty()&&label.empty())){MessageBoxW(owner,L"Profile names must fit in 64 UTF-8 bytes.",L"Invalid name",MB_OK);return false;}
                if(p.label!=wide(originalLabels[i]))next.chat.profileNames[i]=label;
                if(p.mode!=originalModes[i])next.options.standby[i]=p.mode;
                if(p.assistant.edited() && (!slots[i] || !p.assistant.applicable(slots[i]->snapshot) || slots[i]->hasPendingAction))
                {MessageBoxW(owner,L"An edited Local Assistant account changed or has another action pending. Reopen Preferences before editing it.",L"Local Assistant not applied",MB_OK);return false;}
                if(!p.ready)continue;
                const AccountPreferenceDraft account{p.originalAppearance,{utf8(p.alias),p.color,p.originalAppearance.alerts},
                    p.originalSound,{p.categories,p.asset,p.soundName},p.imported};
                if(!account.edited())continue;
                if(!slots[i] || !slots[i]->chat.identity.owns(p.source) || slots[i]->snapshot.state!=State::Ready){MessageBoxW(owner,L"A character login changed while Preferences was open. Cancel and reopen Preferences to edit that account.",L"Character changed",MB_OK);return false;}
                if((!p.alias.empty()&&account.appearance.alias.empty()) || !account.apply(next,AccountKey::from(p.source),true))
                {MessageBoxW(owner,L"Account choices could not be applied. Check the name and sound; at most 32 customized accounts can be saved.",L"Preferences not applied",MB_OK|MB_ICONINFORMATION);return false;}
                next.chat.profileNames[i]=account.profileLabel(originalLabels[i],label,next.chat.profileNames[i],p.source.name);
            }
            const auto error=[&](std::size_t at,const std::wstring& message){MessageBoxW(owner,message.c_str(),L"Preferences not applied",MB_OK|MB_ICONINFORMATION);revealEditorField(owner,fields[at]);return false;};
            for(unsigned int i=0;i<10;++i)for(unsigned int j=0;j<i;++j)
            {
                const auto x=i<7?next.presentation.bindings[i]:next.experience.keys[i-7],y=j<7?next.presentation.bindings[j]:next.experience.keys[j-7];
                if(x.key && x==y)return error(keysStart+i,L"This shortcut is also assigned to "+fields[keysStart+j].label+L".");
            }
            std::set<unsigned int> used;for(unsigned int i=0;i<12;++i)if(n(quickStart+i) && !used.insert(n(quickStart+i)).second)return error(quickStart+i,L"This action already has a toolbar position.");
            if(next.encode().empty())return error(labelField,L"Check character names and account choices. Names must fit in 64 UTF-8 bytes.");
            accepted=std::move(next);return true;
        };
        const auto apply=[&](HWND owner)->bool
        {
            EditorUpdate update(owner);
        // Immutable WAV assets are written only after the complete draft passed
        // validation. Cancel never changes an asset or a character's settings.
        for(int i=0;i<MaxCharacters;++i)if(people[i].ready && people[i].imported)
        {
            WorkerId id{};if(BCryptGenRandom(nullptr,id.data(),static_cast<ULONG>(id.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0){message(L"Sound asset creation failed; preferences were not applied.");return false;}
            const auto asset=hexId(id)+L".wav";std::error_code error;std::filesystem::create_directories(profiles/L"sounds",error);
            const auto file=profiles/L"sounds"/asset,temp=std::filesystem::path(file.wstring()+L".tmp");std::ofstream output(temp,std::ios::binary|std::ios::trunc);const auto& wav=people[i].wav;output.write(reinterpret_cast<const char*>(wav.bytes.data()),static_cast<std::streamsize>(wav.bytes.size()));output.close();
            if(error || !output || !MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temp,error);message(L"The sound could not be saved; preferences were not applied.");return false;}
            accepted.chat.sounds[AccountKey::from(people[i].source)].asset=utf8(asset);people[i].asset=utf8(asset);people[i].imported=false;
        }
        pauseAllAssistant=number(fields[assistantPauseField])!=0;
        for(auto& slot:slots)if(slot)slot->assistantPauseFailed=false;
        for(int i=0;i<MaxCharacters;++i)if(people[i].assistant.edited())
        {
            auto& draft=people[i].assistant;auto request=draft.request();
            slots[i]->pendingAction=request;slots[i]->hasPendingAction=true;people[i].assistantPending=true;
            // Queued per-worker edits are acknowledged independently; never replay them.
            draft.original.height=draft.enabled?1u:0u;
        }
        const bool dockChat=accepted.options.chatDetached,dockControls=accepted.options.controlsDetached;
        accepted.options.chatDetached=chatDetached;accepted.options.controlsDetached=controlsDetached;
        const auto previousChrome=chrome;saveDraft(true);applyPreferences(accepted);const auto nextChrome=chrome;chrome=previousChrome;setPanelDetached(true,dockChat,showChat);setPanelDetached(false,dockControls);setChrome(nextChrome);
        SendMessageW(embedControl,BM_SETCHECK,embedding?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(showChatControl,BM_SETCHECK,showChat?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(voiceControl,BM_SETCHECK,voice?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(muteControl,BM_SETCHECK,muteBackground?BST_CHECKED:BST_UNCHECKED,0);
        for(int i=0;i<MaxCharacters;++i){SendMessageW(standbyControl[i],CB_SETCURSEL,standby[i]==Mode::Economy?1:0,0);if(slots[i])slots[i]->soundPrepared=false;}
        activeReadClock={};applyTheme();refreshChat(true,true);refreshCharacters();updateMonitors();refreshAttention();refreshPins();layout();
        originalLabels=chatPreferences.profileNames;originalModes=standby;modifier=number(fields[modifierField]);
        for(int i=0;i<MaxCharacters;++i)
        {
            auto& p=people[i];p.label=wide(originalLabels[i]);if(!p.ready)continue;
            const auto key=AccountKey::from(p.source);p.originalAppearance=presentation.appearance(key);p.originalSound=chatPreferences.sound(key,p.originalAppearance.alerts);
        }
        fields[profileField].choices.clear();SendMessageW(fields[profileField].control,CB_RESETCONTENT,0,0);
        for(int i=0;i<MaxCharacters;++i){fields[profileField].choices.push_back(characterName(i));SendMessageW(fields[profileField].control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(fields[profileField].choices.back().c_str()));}
        showPerson(owner);refreshAssistant();dependencies(owner);
        for(const auto pair:{std::make_pair(delayField,chatPreferences.delay),std::make_pair(durationField,presentation.duration),std::make_pair(heightField,presentation.height)})
            if(!fields[pair.first].enabled){fields[pair.first].value=std::to_wstring(pair.second);if(fields[pair.first].control)SetWindowTextW(fields[pair.first].control,fields[pair.first].value.c_str());}
        const bool saved=saveOptions();fields[saveField].enabled=!saved;if(fields[saveField].control)EnableWindow(fields[saveField].control,!saved);return saved;
        };
        preferencesOpen=true;refreshAssistantEditor=refreshAssistant;
        edit(window,L"Folderstorm Host Preferences",fields,{},page,true,validate,navigation,apply,&preferencesPage,profileDialogOwner());
        refreshAssistantEditor={};preferencesOpen=false;if(preview && preview==playbackSerial)playAlert({});
        if(pendingCommand)
        {
            if(slots[pendingTarget].get()!=pendingSlot || (pendingSlot && !ChatKey{pendingIdentity.worker,pendingIdentity.generation,pendingIdentity.account,pendingIdentity.grid,{}}.owns(pendingSlot->snapshot)))
            {message(L"Character changed while Preferences was open; the action was discarded.");return;}
            if(pendingCommand==3901 || pendingCommand==3902)
            {
                if(!pendingSlot || pendingSlot->snapshot.state!=State::Ready || pendingSlot->hasPendingReview){message(L"Local Assistant Review is unavailable.");return;}
                auto request=pendingIdentity;request.kind=Kind::AssistantReview;request.unread=pendingCommand==3901?1u:0u;
                pendingSlot->pendingReview=request;pendingSlot->hasPendingReview=true;switchTo(pendingTarget);
            }
            else sessionMenu(0,pendingCommand,pendingTarget);
        }
    }
    void editHostSettings(unsigned int page=~0u) { editPreferences(page); }
    void customizeQuick() { editPreferences(PrefQuick); }
    void pinWindow(HWND panel,bool toggle=true)
    {
        const auto id=windowKey(panel);if(!id)return;
        if(toggle)experience.pinned[id]=!experience.pinned[id];
        SetWindowPos(panel,experience.pinned[id]?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
        CheckMenuItem(GetSystemMenu(panel,FALSE),0xA160,MF_BYCOMMAND|(experience.pinned[id]?MF_CHECKED:MF_UNCHECKED));
        HWND pin=GetDlgItem(panel,PanelPin);if(pin){SetWindowTextW(pin,L"Always on top");fs_host_ui::setButtonIcon(pin,fs_host_ui::Icon::Pin);setToggle(pin,experience.pinned[id]);addTooltip(pin,L"Always on top");}
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
    CharacterSound characterSound(int index) const
    { return chatPreferences.sound(AccountKey::from(slots[index]->snapshot),appearance(index).alerts); }
    bool readSound(const std::filesystem::path& path,WavSound& sound)
    {
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if (!input) return false;
        const std::streamoff size = input.tellg();
        if (size <= 0 || size > static_cast<std::streamoff>(WavSound::MaxBytes)) return false;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size)); input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        return input && WavSound::decode(std::move(bytes),sound);
    }
    void storeCachedSound(const std::string& asset,WavSound sound)
    {
        if (asset.empty() || soundCache.count(asset)) return;
        std::size_t bytes = sound.bytes.size(); for (const auto& entry : soundCache) bytes += entry.second.bytes.size();
        constexpr std::size_t budget = 5*WavSound::MaxBytes;
        for (auto i = soundCache.begin(); bytes > budget && i != soundCache.end();)
        {
            bool used = false;
            for (int slot = 0; slot < MaxCharacters; ++slot) if (slots[slot] && slots[slot]->snapshot.state == State::Ready &&
                !slots[slot]->detached && characterSound(slot).asset == i->first) used = true;
            if (used) ++i;
            else { bytes -= i->second.bytes.size(); i = soundCache.erase(i); }
        }
        if (bytes <= budget) soundCache.emplace(asset,std::move(sound));
    }
    void cacheSound(const std::string& asset)
    {
        if (asset.empty() || soundCache.count(asset)) return;
        WavSound sound;
        if (!CharacterSound{0,asset}.valid() || !readSound(profiles/L"sounds"/wide(asset),sound))
        { message(L"A character sound is unavailable; eligible alerts use the built-in tone. Choose or reset its sound in Settings."); return; }
        storeCachedSound(asset,std::move(sound));
    }
    std::uint64_t playAlert(std::vector<std::uint8_t> bytes)
    {
        // SND_MEMORY playback owns its buffer until stopped, including Preview.
        PlaySoundW(nullptr,nullptr,0); ++playbackSerial; alertTone = std::move(bytes);
        playbackSucceeded=alertTone.empty() || PlaySoundW(reinterpret_cast<LPCWSTR>(alertTone.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT)!=FALSE;
        return playbackSerial;
    }
    void alert(const Message& item,int index)
    {
        if(!slots[index] || (item.eventType!=EventType::Chat && item.eventType!=EventType::Attention))return;
        const auto category=soundCategory(item);if(!category)return;
        if(!presentation.volume){recordAlert(item,index,L"Host alert volume zero");return;}
        const bool eligible = slots[index]->snapshot.state == State::Ready && !slots[index]->detached && slots[index]->snapshot.mode != Mode::Active &&
            (slots[index]->audioPolicy & AudioHostUI) && !(slots[index]->snapshot.flags & (ChatRestricted|Promoting|Error));
        const auto sound = characterSound(index);
        const auto decision=alertGate.decide(item,sound.categories,GetTickCount64(),eligible);
        if(decision!=AlertDecision::Allowed)
        {
            recordAlert(item,index,decision==AlertDecision::WorkerIneligible && item.detail.rfind("Alert:",0)==0?
                wide(item.detail):wide(alertDecisionText(decision)));return;
        }
        const auto saved = soundCache.find(sound.asset);
        playAlert(saved == soundCache.end() ? builtinAlertTone(presentation.volume) : saved->second.volume(presentation.volume));
        recordAlert(item,index,playbackSucceeded?(saved==soundCache.end()?L"Built-in tone playback requested":L"Custom WAV playback requested"):L"Windows refused sound playback");
    }
    void recordAlert(const Message& item,int index,const std::wstring& reason)
    {
        if(!slots[index] || (item.topic==Topic::Nearby && item.eventType!=EventType::Attention))return;
        if(item.eventType==EventType::Chat && item.recipient==item.account)return; // Keep the last incoming result when replying.
        const std::size_t at=item.eventType==EventType::Attention || item.topic==Topic::Notice?3:item.topic==Topic::Private?0:item.topic==Topic::Group?1:2;
        slots[index]->alertReasons[at]=reason;slots[index]->alertAt[at]=GetTickCount64();
    }
    void chatAlertReport(HWND owner) const
    {
        std::wstring report=L"Applied choices; latest live results. No message text.\n";
        const wchar_t* categories[]={L"Direct IM",L"Group",L"Conference",L"Notification"};
        for(int i=0;i<MaxCharacters;++i)if(slots[i])
        {
            const auto& slot=*slots[i];report+=L"\n"+characterName(i)+L" · "+(!slot.pipe.alive() || slot.detached?L"unavailable":slot.snapshot.mode==Mode::Active?L"active":slot.snapshot.mode==Mode::Warm?L"Warm":L"Economy")+L"\n";
            const auto chats=slot.chat.conversationCount(false);
            report+=L"Chats: "+std::to_wstring(chats?chats-1:0)+L"; viewer notices: "+std::to_wstring(slot.chat.conversationCount(true))+L"\n";
            const auto queued=slot.latestChatEvent>slot.chat.cursor?slot.latestChatEvent-slot.chat.cursor:0;
            report+=L"Queued events: "+std::to_wstring(queued)+L"; host alert volume: "+std::to_wstring(presentation.volume)+L"%; viewer alert policy: "+((slot.audioPolicy&AudioHostUI)?L"applied":L"off/pending")+L"\n";
            for(std::size_t at=0;at<4;++at)
            {
                report+=std::wstring(categories[at])+L": "+(slot.alertReasons[at].empty()?L"No event observed":slot.alertReasons[at]);
                if(slot.alertAt[at])report+=L" ("+std::to_wstring((GetTickCount64()-slot.alertAt[at])/1000)+L"s ago)";
                report+=L"\n";
            }
        }
        MessageBoxW(owner,report.c_str(),L"Chat and alert diagnostics",MB_OK|MB_ICONINFORMATION);
    }
    void editCharacterSound(int index) { editPreferences(PrefSounds,index); }
    unsigned int audioPolicyFor(int index) const
    { return (voice ? AudioVoice : 0u) | (muteBackground ? AudioMuteWorld : 0u) |
        (presentation.volume && characterSound(index).categories ? AudioHostUI : 0u); }
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
        if (menuOpen || profileSetupOpen || (event.message != WM_KEYDOWN && event.message != WM_SYSKEYDOWN) || (event.lParam & (1LL<<30))) return false;
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
    void editShortcuts() { editPreferences(PrefKeys); }
    void editTransition() { editPreferences(PrefSwitching); }
    void editAlertVolume() { editPreferences(PrefAudio); }
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
        const auto current=slots[chatIndex]?slots[chatIndex]->chat.conversations.find(conversation):std::map<std::string,Conversation>::iterator{};
        const bool closable=slots[chatIndex] && current!=slots[chatIndex]->chat.conversations.end() && current->second.topic!=Topic::Nearby && !current->second.closed;
        AppendMenuW(menu,MF_STRING|(closable?0:MF_GRAYED),ChatClose,L"Close conversation in host");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(conversationFilters()),L"Show conversations");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
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
        if(filterCommand(choice))return;
        if(choice==ChatClose){if(slots[chatIndex] && slots[chatIndex]->chat.identity.owns(source) && source.conversation==conversation)closeConversation(chatIndex,conversation);return;}
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
    bool shownPin(const ChatPin& pin) const
    {
        if(!(chatPreferences.showMask&conversationCategory(pin.topic)))return false;
        for(const auto& slot:slots)if(slot && AccountKey::from(slot->snapshot)==pin.owner)
        {const auto found=slot->chat.conversations.find(pin.conversation);if(found!=slot->chat.conversations.end() && found->second.closed)return false;}
        return true;
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
                addTooltip(pinButtons[i],pinTips[i].c_str());
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
            for(const auto& entry:view.conversations)if(entry.second.unread && !entry.second.restricted && !entry.second.closed)
            {
                Message item=slots[i]->snapshot;item.topic=entry.second.topic;item.conversation=entry.first;item.eventType=EventType::Conversation;item.cursor=entry.second.lastIncomingEvent;item.unread=entry.second.unread;item.title=entry.second.title;
                if(attention.hiddenUnread(item) || !(chatPreferences.attentionMask&attentionCategoryMask(item)))continue;
                const auto key=std::make_tuple(item.account,item.grid,item.conversation);liveUnread.insert(key);
                if(!unreadSince.count(key))unreadSince[key]=GetTickCount64();item.eventAt=unreadSince[key];rows.push_back(item);
            }
        }
        for(auto it=unreadSince.begin();it!=unreadSince.end();)if(!liveUnread.count(it->first))it=unreadSince.erase(it);else ++it;
        for(const auto& item:attention.entries){const int i=sourceSlot(item);if(i>=0 && slots[i]->running() && !slots[i]->detached && slots[i]->pipe.alive() && slots[i]->snapshot.state==State::Ready && !(slots[i]->snapshot.flags&ChatRestricted) && (experience.accountMask&(1u<<i)) && (chatPreferences.attentionMask&attentionCategoryMask(item)))rows.push_back(item);}
        for(int i=0;i<MaxCharacters;++i)if(slots[i])
        {
            const auto& slot=*slots[i];
            if(slot.running() && slot.pipe.alive() && !slot.detached && (experience.accountMask&(1u<<i)) &&
                slot.snapshot.state==State::Ready && assistantInfoOwned(slot.assistant,slot.snapshot) && slot.assistant.unread)
            {auto item=assistantAttention(slot.assistant);item.eventAt=slot.assistantSince;item.cursor=slot.assistant.event;if(!attention.hiddenUnread(item))rows.push_back(item);}
        }
        attentionNavigation.update(std::move(rows),experience.oldestFirst);return attentionNavigation.rows;
    }
    void editAttentionFilters() { editChatSettings(1); }
    bool shownConversation(const Conversation& item) const
    { return !item.closed && (chatPreferences.showMask & conversationCategory(item.topic)) != 0; }
    HMENU conversationFilters() const
    {
        HMENU menu=CreatePopupMenu();const wchar_t* labels[]={L"Nearby Chat",L"Direct IMs",L"Group Chats",L"Conferences",L"Viewer Notifications"};
        for(unsigned int i=0;i<5;++i)AppendMenuW(menu,MF_STRING|((chatPreferences.showMask&(1u<<i))?MF_CHECKED:0),VisibilityBase+i,labels[i]);
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,ChatFilters,L"Chats and notifications settings…");return menu;
    }
    bool filterCommand(UINT command)
    {
        if(command>=VisibilityBase && command<VisibilityBase+5)
        {saveDraft(true);chatPreferences.showMask^=1u<<(command-VisibilityBase);chatPreferences.haveCategories=true;refreshChat(true,true);updateMonitors();refreshAttention();refreshPins();layout();return true;}
        if(command==ChatFilters){editChatSettings();return true;}return false;
    }
    void closeConversation(int index,const std::string& id)
    {
        if(!validSlot(index) || !slots[index])return;auto& slot=*slots[index];
        const auto found=slot.chat.conversations.find(id);
        if(found==slot.chat.conversations.end() || found->second.topic==Topic::Nearby || found->second.closed)return;
        if(slot.hasPendingAction || slot.hasPendingSend || (slot.waiting && slot.request.kind==Kind::SendChat))
        {message(L"Finish the current chat action before closing this conversation.");return;}
        if(slot.snapshot.state!=State::Ready || slot.detached || !slot.pipe.alive())return;
        if(index==chatIndex && id==conversation)saveDraft(true);
        Message action;action.worker=slot.chat.identity.worker;action.generation=slot.chat.identity.generation;
        action.account=slot.chat.identity.account;action.grid=slot.chat.identity.grid;
        action.kind=Kind::CloseChat;action.topic=found->second.topic;action.conversation=id;
        slot.chat.close(id,(std::numeric_limits<std::uint64_t>::max)());
        slot.pendingAction=action;slot.hasPendingAction=true;
        refreshChat(true,true);updateMonitors();refreshAttention();refreshPins();layout();
    }
    void refreshAttention()
    {
        auto rows=collectAttention();
        if(attentionCount!=rows.size()){attentionCount=rows.size();layout();}
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
        if(index<0 || !slots[index] || (item.conversation!=AssistantAttentionId && (slots[index]->snapshot.flags&ChatRestricted)))return;
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
        const bool assistantRequest=item.conversation==AssistantAttentionId;
        if (slots[index]->detached || !slots[index]->pipe.alive() || slots[index]->snapshot.state != State::Ready || (!assistantRequest && (slots[index]->snapshot.flags & ChatRestricted)))
        { message(L"This character's attention is unavailable. Review its native viewer."); return; }
        if (item.eventType == EventType::Attention)
        {
            auto action = item; action.kind = assistantRequest?Kind::AssistantReview:Kind::ReviewAttention;
            if(assistantRequest)action.unread=0;
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
    std::wstring profileName(int index) const
    {
        if(!validSlot(index))return L"Unavailable";
        const auto& name=chatPreferences.profileNames[static_cast<std::size_t>(index)];
        return name.empty()?L"Character "+std::to_wstring(index+1):wide(name);
    }
    std::wstring characterName(int index) const
    {
        const auto label=profileName(index);
        if(!validSlot(index) || !slots[index] || slots[index]->snapshot.state!=State::Ready || slots[index]->snapshot.name.empty())return label;
        const auto actual=wide(slots[index]->snapshot.name);
        const auto alias=appearance(index).alias;
        const auto friendly=chatPreferences.profileNames[static_cast<std::size_t>(index)].empty() && !alias.empty()?wide(alias):label;
        return friendly==actual?actual:friendly+L" ("+actual+L")";
    }
    COLORREF accountColor(int index) const
    {
        HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) return GetSysColor(COLOR_HIGHLIGHT);
        const auto rgb=[](COLORREF c){return (static_cast<unsigned int>(GetRValue(c))<<16)|(static_cast<unsigned int>(GetGValue(c))<<8)|GetBValue(c);};
        const auto c=visibleAccent(appearance(index).color,rgb(theme.background),rgb(theme.text));
        return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
    }
    void editAppearance(int index) { editPreferences(PrefCharacter,index); }
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
    void capturePreferencePlacement()
    {
        capturePlacement(window,0,dpi); capturePlacement(chatWindow,1,chatDpi); capturePlacement(controlsWindow,2,controlsDpi);
        for(unsigned int i=0;i<MonitorViews;++i)capturePlacement(monitors[i].window,i==CyclingView?8:i+3,monitors[i].dpi);
        for(unsigned int i=0;i<MaxCharacters;++i)capturePlacement(friendsWindows[i].window,9+i,friendsWindows[i].dpi);
        presentation.pins=pins.saved();
        if(attentionWindow)experience.attentionRect=liveRect(attentionWindow,attentionDpi);
    }
    HostPreferences preferences() const {return {options(),presentation,experience,chatPreferences};}
    void applyPreferences(const HostPreferences& value)
    {
        const auto& o=value.options;standby=o.standby;voice=o.voice;muteBackground=o.muteBackground;
        embedding=o.hosted;showChat=o.chat;chrome=o.chrome;chatDetached=o.chatDetached;controlsDetached=o.controlsDetached;
        monitorSize=o.previewSize;monitorRate=o.previewRate;cinematic=o.cinematic;
        presentation=value.presentation;experience=value.experience;chatPreferences=value.chat;
    }
    static bool readPreferences(const std::filesystem::path& path,HostPreferences& value)
    {
        std::ifstream input(path,std::ios::binary);if(!input)return false;
        std::string bytes(HostPreferences::MaxBytes+1,'\0');input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));return HostPreferences::decode(bytes,value);
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
        if (draw.CtlType != ODT_COMBOBOX || (draw.CtlID != ActiveCharacter && draw.CtlID != ChatAccount)) return theme.drawCombo(draw);
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
            text += characterName(i);if(!slots[i] || slots[i]->snapshot.state!=State::Ready)text+=L" · not logged in";
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
        lastNotice = text; const auto display=saveError.empty()?text:saveError+L" "+text; SetWindowTextW(notice, display.c_str());
        if (chrome == 2) SetWindowTextW(revealControl,(L"Show controls · "+display).c_str());
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
        if(positions)for(auto& p:friendsWindows)if(p.window){experience.pinned[windowKey(p.window)]=false;pinWindow(p.window,false);fitOpenWindow(p.window,p.dpi,true);}
        ShowWindow(window,SW_SHOW); SetForegroundWindow(window); layout();
        message(positions ? L"Controls and window positions recovered. Window pins were cleared." : L"Host controls restored.");
    }
    void refitWindows()
    {
        fitOpenWindow(window,dpi,false);
        for (HWND panel : {chatWindow,controlsWindow,attentionWindow}) if (panel) fitOpenWindow(panel,panel == chatWindow ? chatDpi : panel == controlsWindow ? controlsDpi : attentionDpi,false);
        for (auto& monitor : monitors) if (monitor.window) fitOpenWindow(monitor.window,monitor.dpi,false);
        for(auto& p:friendsWindows)if(p.window)fitOpenWindow(p.window,p.dpi,false);
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
        const auto previous=chrome;chrome=mode;
        if(controlsDetached && controlsWindow && (previous!=chrome || chrome==0) && !IsIconic(controlsWindow))
        {
            RECT current{};GetWindowRect(controlsWindow,&current);
            if(chrome==1 && previous!=1)controlsExpandedHeight=static_cast<int>(current.bottom-current.top);
            RECT frame{0,0,1,characterHeight(controlsWindow,chrome==1)+theme.scale(8,controlsDpi)};
            AdjustWindowRectEx(&frame,static_cast<DWORD>(GetWindowLongPtrW(controlsWindow,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(controlsWindow,GWL_EXSTYLE)));
            int height=(std::max)(80,static_cast<int>(frame.bottom-frame.top));
            if(chrome!=1)height=(std::max)(height,(std::max)(controlsExpandedHeight,static_cast<int>(current.bottom-current.top)));
            MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);if(GetMonitorInfoW(MonitorFromWindow(controlsWindow,MONITOR_DEFAULTTONEAREST),&monitor))height=(std::min)(height,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
            SetWindowPos(controlsWindow,nullptr,0,0,current.right-current.left,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
        for (unsigned int i = 0; i < 3; ++i)
            CheckMenuItem(GetSystemMenu(window, FALSE), 0xA100u + i * 0x10u, MF_BYCOMMAND | (chrome == i ? MF_CHECKED : MF_UNCHECKED));
        SetWindowTextW(window, (L"Folderstorm · " + std::to_wstring(MaxCharacters) + (saveError.empty()?L" character slots":L" · settings not saved")).c_str());
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
        if (kind == Kind::WorkspaceInfo || kind==Kind::AssistantInfo) { request.account = slot.snapshot.account; request.grid = slot.snapshot.grid; }
        if(kind==Kind::AssistantInfo)request.title=utf8(profileName(index));
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
        if (kind == Kind::AudioPolicy) request.unread = audioPolicyFor(index);
        DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if ((kind == Kind::ReviewChat || kind == Kind::ReviewAttention || kind==Kind::AssistantReview) && foregroundPid == GetCurrentProcessId()) AllowSetForegroundWindow(slot.pid);
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
    bool saveOptions(bool announce=true)
    {
        capturePreferencePlacement();const auto data=preferences().encode();
        const auto fail=[&]()
        {
            saveError=L"Host settings not saved. Retry in Preferences → Advanced → Files & recovery.";
            autosave.failed(GetTickCount64());message(lastNotice);
            SetWindowTextW(window,L"Folderstorm · settings not saved");return false;
        };
        if(data.empty())return fail();
        const auto target=profiles/L"host-preferences.dat";
        if(GetFileAttributesW(target.c_str())!=INVALID_FILE_ATTRIBUTES)
        {
            HostPreferences prior;
            const auto backup=profiles/(readPreferences(target,prior)?L"host-preferences.dat.bak":L"host-preferences.dat.invalid");
            if(!CopyFileW(target.c_str(),backup.c_str(),FALSE))return fail();
        }
        if(!saveStore(L"host-preferences.dat",data))return fail();
        autosave.committed(data);saveError.clear();
        SetWindowTextW(window,(L"Folderstorm · "+std::to_wstring(MaxCharacters)+L" character slots").c_str());
        if(announce)message(L"Host preferences saved automatically. Character data remains in its private profile.");
        else message(lastNotice);
        return true;
    }
    void autosaveChoices()
    {
        if(preferencesOpen || closing || detaching || GetTickCount64()-autosaveAt<250)return;
        autosaveAt=GetTickCount64();capturePreferencePlacement();autosave.observe(preferences().encode(),GetTickCount64());
        if(autosave.due(GetTickCount64()))saveOptions(false);
    }
    void toggleAttention()
    {
        if(attentionWindow && IsWindowVisible(attentionWindow) && !IsIconic(attentionWindow))ShowWindow(attentionWindow,SW_HIDE);
        else openAttention();
        layout();
    }
    void toggleChat()
    {
        saveDraft(true);showChat=!showChat;layout();
        if(showChat && chatDetached && chatWindow){ShowWindow(chatWindow,SW_SHOWNORMAL);SetForegroundWindow(chatWindow);}
    }
    void monitorsMenu()
    {
        focusRequested=false;RemovePropW(window,L"FolderstormViewportFocusIntent");
        const auto menu=CreatePopupMenu();std::array<Message,MaxCharacters> identities{};
        for(int i=0;i<MaxCharacters;++i)
        {
            if(slots[i])identities[i]=slots[i]->snapshot;
            const int id=monitorSet.find(i);const bool open=id>=0;
            const bool ready=slots[i] && slots[i]->running() && !slots[i]->detached && slots[i]->snapshot.state==State::Ready;
            AppendMenuW(menu,MF_STRING|(open?MF_CHECKED:0)|(!open && (!ready || i==active || busy())?MF_GRAYED:0),MonitorBase+static_cast<UINT>(i),characterName(i).c_str());
        }
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,213,L"Open all inactive monitors");AppendMenuW(menu,MF_STRING,214,L"Tile as grid");AppendMenuW(menu,MF_STRING,215,L"Arrange as row");AppendMenuW(menu,MF_STRING,216,L"Cycling monitor");AppendMenuW(menu,MF_STRING,212,L"Close all monitors");AppendMenuW(menu,MF_STRING,180,L"Monitor settings…");
        POINT point{};GetCursorPos(&point);menuOpen=true;const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr);menuOpen=false;DestroyMenu(menu);
        if(choice==213){openAllMonitors();return;}if(choice==214 || choice==215){arrangeMonitors(point,choice==215);return;}if(choice==216){openCycler();return;}
        if(choice==212){stopMonitors();return;}if(choice==180){editPreferences(PrefMonitors);return;}
        if(choice>=MonitorBase && choice<MonitorBase+MaxCharacters)
        {
            const int i=static_cast<int>(choice)-MonitorBase;const auto& m=identities[i];
            if(!slots[i] || !ChatKey{m.worker,m.generation,m.account,m.grid,{}}.owns(slots[i]->snapshot)){message(L"Character changed; choose its monitor again.");return;}
            const int id=monitorSet.find(i);if(id>=0)stopMonitor(id,true);else openMonitor(i);
        }
    }
    void quickOverflowMenu()
    {
        focusRequested=false;RemovePropW(window,L"FolderstormViewportFocusIntent");
        const auto actions=overflowActions;const int target=lifecycle.selection();auto* original=slots[target].get();const Message identity=original?original->snapshot:Message{};const auto menu=CreatePopupMenu();
        for(std::size_t i=0;i<actions.size();++i){const auto* c=shellCommand(actions[i]);if(c)AppendMenuW(menu,MF_STRING,static_cast<UINT>(i+1),c->label);}
        POINT point{};GetCursorPos(&point);menuOpen=true;const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr);menuOpen=false;DestroyMenu(menu);
        if(choice && choice<=actions.size())
        {
            if(slots[target].get()!=original || (original && !ChatKey{identity.worker,identity.generation,identity.account,identity.grid,{}}.owns(original->snapshot)))
            {message(L"Character changed while the menu was open; choose the action again.");return;}
            sessionMenu(0,actions[choice-1],target);
        }
    }
    void mainOverflowMenu()
    {
        focusRequested=false;RemovePropW(window,L"FolderstormViewportFocusIntent");const auto actions=overflowMain;
        HMENU popup=CreatePopupMenu();
        for(const auto action:actions)
        {
            const auto* name=action==LaunchNext?L"Login":action==ShowChat?L"Chat":action==MonitorsMenu?L"Monitors":L"Preferences";
            AppendMenuW(popup,MF_STRING|(action==LaunchNext&&!IsWindowEnabled(launchButton)?MF_GRAYED:0),action,name);
        }
        POINT point{};GetCursorPos(&point);menuOpen=true;const UINT action=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr);menuOpen=false;DestroyMenu(popup);
        if(action)SendMessageW(window,WM_COMMAND,action,0);
    }
    void sessionMenu(unsigned int group=0,UINT direct=0,int target=-1)
    {
        if(direct==SelectedActions){editAppearance(validSlot(target)?target:lifecycle.selection());return;}
        if(direct==ViewActions){editPreferences(PrefLayout);return;}
        if(direct==SettingsActions){editPreferences();return;}
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
        add(characters,273,L"Settings for "+name+L"…");
        add(characters,201,L"Workspace controls…",slot && slot->snapshot.state==State::Ready && !busy());
        add(characters,MonitorSelected,L"Open monitor",slot && slot->snapshot.state==State::Ready && index!=active && !busy());
        AppendMenuW(characters,MF_SEPARATOR,0,nullptr);
        add(characters,240,slot&&slot->running()?L"Restart…":L"Open login…",!busy());add(characters,241,L"Cancel pending restart",slot && slot->restart.phase()!=Restart::Phase::Idle);
        add(characters,202,L"Log out…",slot && slot->pipe.alive() && !slot->detached && !busy());
        add(login,LaunchNext,L"Open next available login",!busy());
        for(int i=0;i<MaxCharacters;++i){add(login,LaunchBase+static_cast<UINT>(i),L"Open "+profileName(i)+L" login",!busy() && (!slots[i] || !slots[i]->running()));add(mon,MonitorBase+static_cast<UINT>(i),characterName(i),slots[i] && slots[i]->snapshot.state==State::Ready && i!=active && !busy());}
        add(mon,212,L"Stop all monitors");
        add(view,250,L"Expanded controls",true,chrome==0);add(view,251,L"Condensed controls",true,chrome==1);add(view,252,L"Minimal with Show controls",true,chrome==2);
        add(view,270,chatDetached?L"Dock shared Chat":L"Pop out shared Chat");add(view,271,controlsDetached?L"Dock character controls":L"Pop out character controls");add(view,272,L"Show shared Chat",true,showChat);
        add(view,435,L"Host active viewer",true,embedding);add(view,436,L"Show active viewer separately");AppendMenuW(view,MF_POPUP,reinterpret_cast<UINT_PTR>(mon),L"Monitors");
        add(view,Details,L"Show diagnostics",true,experience.diagnostics);add(view,RestoreShell,L"Restore host controls");add(view,RecoverShell,L"Recover window positions");
        add(view,208,L"Skip current transition",handoff.step()!=Handoff::Step::Idle);AppendMenuW(view,MF_POPUP,reinterpret_cast<UINT_PTR>(conversationFilters()),L"Show conversations");
        add(settings,AppearanceSettings,L"Host Preferences…");add(settings,QuickSettings,L"Customize quick actions…");add(settings,AttentionSettings,L"Attention filters and ordering…");add(settings,276,L"Keyboard shortcuts…");add(settings,ChatSettings,L"Chats and notifications…");add(settings,CharacterSoundSettings,L"Sounds: "+name+L"…",slot && slot->snapshot.state==State::Ready);add(settings,206,L"Save preferences now");
        add(settings,277,L"Copy from main profile: "+name+L"…",!busy() && (!slot || !slot->running()));
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
        if(filterCommand(choice))return;
        if(choice==RestoreShell){recover();return;}if(choice==RecoverShell){recover(true);return;}
        if(choice==AppearanceSettings){editHostSettings(PrefAppearance);return;}if(choice==QuickSettings){customizeQuick();return;}
        if(choice==NextAttention || choice==PreviousAttention){navigateAttention(choice==NextAttention?1:-1);return;}
        if(choice==AttentionSettings){editAttentionFilters();return;}
        if(choice==ChatSettings){editChatSettings();return;}
        if(choice==CharacterSoundSettings){editCharacterSound(index);return;}
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
        else if (choice == 274) toggleAttention();
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
    bool contactsLive(int index) const
    {
        const auto* s=validSlot(index)?slots[index].get():nullptr;
        return s && s->running() && !s->detached && s->pipe.alive() && s->contacts.owns(s->snapshot);
    }
    bool contactsVisible(int index) const
    {
        const auto visible=[index](const fs_host_ui::FriendsPane& p){return p.slot==index && p.window && IsWindowVisible(p.window) && !IsIconic(GetAncestor(p.window,GA_ROOT));};
        if(visible(sharedFriends))return true;
        for(const auto& p:friendsWindows)if(visible(p))return true;
        for(const auto& m:monitors)if(visible(m.friends))return true;
        return false;
    }
    Message contactRequest(int index,Kind kind) const
    {
        const auto& source=slots[index]->snapshot;Message request;request.kind=kind;request.worker=source.worker;request.generation=source.generation;request.account=source.account;request.grid=source.grid;request.name=source.name;return request;
    }
    bool pollContacts(int index)
    {
        auto& s=*slots[index];const auto now=GetTickCount64();
        if(!contactsLive(index) || now-s.contactsAt<250)return false;
        if(s.hasPendingContact)
        {
            const auto request=s.pendingContact;s.hasPendingContact=false;
            if(s.contacts.source.owns(request)){s.contactsAt=now;if(send(index,request.kind,Mode::Warm,0,&request))return true;if(s.pipe.alive())s.hasPendingContact=true;}
            return false;
        }
        if(!contactsVisible(index))return false;
        auto& d=s.contacts;
        if(!d.friends.loading && (!s.friendRefreshAt || now-s.friendRefreshAt>=5000))d.beginFriends();
        auto* transfer=d.search.loading?&d.search:d.friends.loading?&d.friends:nullptr;
        if(!transfer)return false;
        auto request=contactRequest(index,Kind::Contacts);request.width=transfer==&d.search?1u:0u;request.event=transfer->token;request.cursor=transfer->offset;
        s.contactsAt=now;return send(index,Kind::Contacts,Mode::Warm,0,&request);
    }
    void friendsFont(fs_host_ui::FriendsPane& p)
    {
        const auto next=CreateFontW(-theme.scale(12,p.dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!next)return;
        for(auto child:{p.label,p.filter,p.onlineOnlyControl,p.searchButton,p.friendsButton,p.list,p.open,p.popout,p.pin})if(child)fs_host_ui::setNativeFont(child,next);
        if(p.list)SendMessageW(p.list,LB_SETITEMHEIGHT,0,theme.scale(24,p.dpi));
        if(p.font)DeleteObject(p.font);p.font=next;
    }
    bool drawFriend(fs_host_ui::FriendsPane& p,const DRAWITEMSTRUCT& draw)
    {
        if(draw.CtlType!=ODT_LISTBOX || draw.hwndItem!=p.list)return false;
        theme.selection(draw);
        if(draw.itemID>=p.presentation.rows.size() || draw.itemID>=p.labels.size())return true;
        const int saved=SaveDC(draw.hDC);
        const auto scale=[&](int n){return theme.scale(n,p.dpi);};
        const auto* c=p.presentation.at(draw.itemID);
        RECT text=draw.rcItem;text.left+=scale(8);text.right-=scale(8);
        SetBkMode(draw.hDC,TRANSPARENT);
        SelectObject(draw.hDC,reinterpret_cast<HFONT>(SendMessageW(p.list,WM_GETFONT,0,0)));
        if(!c)
        {
            FillRect(draw.hDC,&draw.rcItem,theme.highContrast?theme.inputBrush:theme.panelBrush);
            SetTextColor(draw.hDC,theme.highContrast?theme.text:theme.accent);
            DrawTextW(draw.hDC,p.labels[draw.itemID].c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        else
        {
            const bool selected=(draw.itemState&ODS_SELECTED)!=0,disabled=(draw.itemState&ODS_DISABLED)!=0;
            if(!p.searching)
            {
                const int diameter=scale(9),left=text.left,top=(draw.rcItem.top+draw.rcItem.bottom-diameter)/2;
                const COLORREF marker=theme.highContrast?GetTextColor(draw.hDC):disabled?theme.muted:c->online?(theme.dark?RGB(89,211,149):RGB(26,125,74)):theme.muted;
                const HPEN pen=CreatePen(PS_SOLID,(std::max)(1,scale(1)),marker);
                const HBRUSH brush=c->online?CreateSolidBrush(marker):nullptr;
                const auto oldPen=SelectObject(draw.hDC,pen),oldBrush=SelectObject(draw.hDC,brush?static_cast<HGDIOBJ>(brush):GetStockObject(NULL_BRUSH));
                Ellipse(draw.hDC,left,top,left+diameter,top+diameter);
                SelectObject(draw.hDC,oldBrush);SelectObject(draw.hDC,oldPen);
                if(brush)DeleteObject(brush);
                DeleteObject(pen);
                text.left+=scale(19);
                if(!c->online && !selected && !theme.highContrast)SetTextColor(draw.hDC,theme.muted);
            }
            const auto name=wide(c->display)+L" ("+wide(c->username)+L")"+(c->allowed?L"":L" · restricted");
            DrawTextW(draw.hDC,name.c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            theme.selectionFocus(draw);
        }
        if(saved)RestoreDC(draw.hDC,saved);
        return true;
    }
    LRESULT friendsKey(const fs_host_ui::FriendsPane& p,WORD key,int current) const
    {
        const int count=static_cast<int>(p.presentation.rows.size());
        int start=current,direction=1;
        if(key==VK_DOWN)start=current+1;
        else if(key==VK_UP){start=current<0?count-1:current-1;direction=-1;}
        else if(key==VK_HOME)start=0;
        else if(key==VK_END){start=count-1;direction=-1;}
        else if(key==VK_PRIOR || key==VK_NEXT)
        {
            RECT rect{};GetClientRect(p.list,&rect);
            const auto height=SendMessageW(p.list,LB_GETITEMHEIGHT,0,0);
            const int page=(std::max)(1,static_cast<int>(rect.bottom)/(std::max)(1,static_cast<int>(height))-1);
            direction=key==VK_PRIOR?-1:1;
            start=(std::max)(0,(std::min)(count-1,current+direction*page));
        }
        else return -1;
        if(count==0)return -2;
        const auto selected=p.presentation.selectable(start,direction);
        // A heading at a page boundary must not become a resident selection.
        const auto fallback=selected<0 && (key==VK_PRIOR || key==VK_NEXT)?p.presentation.selectable(start,-direction):selected;
        return fallback<0?-2:fallback;
    }
    LRESULT friendsCharacter(const fs_host_ui::FriendsPane& p,wchar_t character,int current) const
    {
        const int count=static_cast<int>(p.presentation.rows.size());
        for(int step=1;step<=count;++step)
        {
            const int row=(current+step+count)%count;
            if(!p.presentation.at(static_cast<std::size_t>(row)) || static_cast<std::size_t>(row)>=p.labels.size())continue;
            const auto& label=p.labels[static_cast<std::size_t>(row)];
            if(!label.empty() && CompareStringOrdinal(&character,1,label.c_str(),1,TRUE)==CSTR_EQUAL)return row;
        }
        return -2;
    }
    void layoutFriends(fs_host_ui::FriendsPane& p)
    {
        if(!p.label)return;RECT r{};GetClientRect(p.window,&r);const auto scale=[&](int n){return theme.scale(n,p.dpi);};const int width=static_cast<int>(r.right),height=static_cast<int>(r.bottom);
        MoveWindow(p.label,scale(8),scale(6),(std::max)(1,width-scale(16)),scale(38),TRUE);
        MoveWindow(p.filter,scale(8),scale(48),(std::max)(1,width-scale(p.searching?16:124)),scale(24),TRUE);
        MoveWindow(p.onlineOnlyControl,width-scale(116),scale(48),scale(108),scale(24),TRUE);
        ShowWindow(p.onlineOnlyControl,p.searching?SW_HIDE:SW_SHOWNOACTIVATE);
        const int cell=(width-scale(20))/2;
        MoveWindow(p.friendsButton,scale(8),scale(76),cell,scale(24),TRUE);MoveWindow(p.searchButton,scale(12)+cell,scale(76),cell,scale(24),TRUE);
        MoveWindow(p.list,scale(8),scale(104),(std::max)(1,width-scale(16)),(std::max)(1,height-scale(146)),TRUE);
        MoveWindow(p.open,scale(8),height-scale(34),(std::max)(1,width-scale(54)),scale(26),TRUE);
        MoveWindow(p.standalone?p.pin:p.popout,width-scale(38),height-scale(34),scale(30),scale(26),TRUE);
        ShowWindow(p.pin,p.standalone?SW_SHOWNOACTIVATE:SW_HIDE);ShowWindow(p.popout,p.standalone?SW_HIDE:SW_SHOWNOACTIVATE);
    }
    void createFriends(fs_host_ui::FriendsPane& p,HWND parent,bool standalone,int index,int origin)
    {
        p.slot=index;p.monitor=origin;p.standalone=standalone;if(p.window)return;p.source={};p.presentation={};p.labels.clear();p.token=p.revision=p.queryToken=0;
        p.window=CreateWindowExW(standalone?WS_EX_TOOLWINDOW:WS_EX_CONTROLPARENT,L"FolderstormSessionFriends",L"Friends / New IM",(standalone?WS_OVERLAPPEDWINDOW:WS_CHILD)|WS_CLIPCHILDREN,
            standalone?CW_USEDEFAULT:0,standalone?CW_USEDEFAULT:0,scaled(340),scaled(460),parent,nullptr,GetModuleHandleW(nullptr),&p);
        if(!p.window)return;
        using WindowDpi=UINT(WINAPI*)(HWND);const auto getDpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));if(getDpi)p.dpi=(std::max)(96u,getDpi(p.window));
        const auto make=[&](const wchar_t* type,const wchar_t* text,DWORD style,int command){return CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,p.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)),GetModuleHandleW(nullptr),nullptr);};
        p.label=make(L"STATIC",L"Send as: unavailable",SS_LEFT|SS_ENDELLIPSIS|SS_NOPREFIX,0);
        p.filter=make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,ContactFilter);SendMessageW(p.filter,EM_SETLIMITTEXT,127,0);
        p.onlineOnlyControl=make(L"BUTTON",L"Online only",BS_AUTOCHECKBOX|WS_TABSTOP,ContactOnlineOnly);
        SendMessageW(p.onlineOnlyControl,BM_SETCHECK,p.onlineOnly?BST_CHECKED:BST_UNCHECKED,0);
        p.friendsButton=make(L"BUTTON",L"Friends",BS_PUSHBUTTON|WS_TABSTOP,ContactFriends);
        p.searchButton=make(L"BUTTON",L"Search names",BS_PUSHBUTTON|WS_TABSTOP,ContactSearch);
        p.list=make(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_WANTKEYBOARDINPUT|WS_TABSTOP,ContactList);
        const bool keyboardInstalled=p.list && SetWindowSubclass(p.list,friendsListProc,9,reinterpret_cast<DWORD_PTR>(&p));
        p.open=make(L"BUTTON",L"Open IM",BS_PUSHBUTTON|WS_TABSTOP,ContactOpen);
        p.popout=make(L"BUTTON",L"Pop out friends",BS_PUSHBUTTON|WS_TABSTOP,ContactPopout);
        p.pin=make(L"BUTTON",L"Always on top",BS_PUSHBUTTON|WS_TABSTOP,PanelPin);
        fs_host_ui::setButtonIcon(p.popout,fs_host_ui::Icon::ExternalLink);fs_host_ui::setButtonIcon(p.pin,fs_host_ui::Icon::Pin);
        addTooltip(p.friendsButton,L"Friends are grouped by online status. Offline friends can receive IMs too.");addTooltip(p.filter,L"Filter friends, or enter a resident name and choose Search names");addTooltip(p.searchButton,L"Search this character's grid; display names and usernames are shown together");
        addTooltip(p.onlineOnlyControl,L"Show online friends only; offline friends remain available when unchecked");
        addTooltip(p.open,L"Open an IM as the sending character; no message is sent until you choose Send");addTooltip(p.popout,L"Open an independent friends window for this character");
        if(!p.label || !p.filter || !p.onlineOnlyControl || !p.friendsButton || !p.searchButton || !keyboardInstalled || !p.open || !p.popout || !p.pin){DestroyWindow(p.window);message(L"Unable to create the friends controls. Choose Friends or New IM again.");return;}
        friendsFont(p);theme.window(p.window);layoutFriends(p);
        if(standalone){installPanelMenu(p.window);restorePlacement(p.window,9u+static_cast<unsigned int>(index),p.dpi);pinWindow(p.window,false);}
    }
    void refreshFriendsPane(fs_host_ui::FriendsPane& p)
    {
        if(!p.window)return;
        auto* s=validSlot(p.slot)?slots[p.slot].get():nullptr;const bool live=contactsLive(p.slot);
        if(!s || !live || !p.source.owns(s->snapshot))
        {
            p.presentation={};p.labels.clear();p.token=0;p.revision=0;p.queryToken=0;p.lastFilter.clear();p.searching=false;p.source=live?s->contacts.source:ChatKey{};
            SendMessageW(p.list,LB_RESETCONTENT,0,0);p.updating=true;SetWindowTextW(p.filter,L"");p.updating=false;
        }
        if(!IsWindowVisible(p.window))return;
        const auto label=L"Send as: "+(live?wide(s->snapshot.name):std::wstring(L"unavailable"))+(p.searching?L" · search":L" · friends");
        SetWindowTextW(p.label,label.c_str());
        if(p.standalone)SetWindowTextW(p.window,(L"Friends: "+characterName(p.slot)).c_str());
        const bool changedSearch=live && p.searching && p.queryToken!=s->contacts.search.token;
        const auto* transfer=live && !changedSearch?(p.searching?&s->contacts.search:&s->contacts.friends):nullptr;
        if(live)addTooltip(p.label,(L"Send as: "+wide(s->snapshot.name)+L" · "+wide(s->snapshot.grid)).c_str());
        const auto filter=p.searching?std::string{}:editText(p.filter);
        const bool changed=!transfer || p.token!=transfer->token || p.revision!=transfer->revision || filter!=p.lastFilter || p.onlineOnly!=p.lastOnlineOnly || p.searching!=p.lastSearching;
        if(changed)
        {
            std::string selected,topId;
            const auto at=SendMessageW(p.list,LB_GETCURSEL,0,0),top=SendMessageW(p.list,LB_GETTOPINDEX,0,0);
            if(const auto* c=p.presentation.at(static_cast<std::size_t>(at)))selected=c->id;
            if(const auto* c=p.presentation.at(static_cast<std::size_t>(top)))topId=c->id;
            auto nextList=transfer?presentFriends(transfer->rows,filter,p.onlineOnly,p.searching):FriendsList{};
            std::vector<std::wstring> labels;
            for(std::size_t i=0;i<nextList.rows.size();++i)
            {
                const auto& row=nextList.rows[i];
                if(const auto* c=nextList.at(i))
                    labels.push_back(wide(c->display)+L" ("+wide(c->username)+L")"+(p.searching?L"":c->online?L" · Online":L" · Offline")+(c->allowed?L"":L" · restricted"));
                else labels.push_back(std::wstring(row.kind==FriendsRowKind::OnlineHeading?L"Online · ":L"Offline · ")+std::to_wstring(row.count));
            }
            p.updating=true;SendMessageW(p.list,WM_SETREDRAW,FALSE,0);SendMessageW(p.list,LB_RESETCONTENT,0,0);
            p.presentation=std::move(nextList);p.labels=std::move(labels);
            for(const auto& name:p.labels)SendMessageW(p.list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
            const auto restored=p.presentation.find(selected),restoredTop=p.presentation.find(topId);
            if(restored>=0)SendMessageW(p.list,LB_SETCURSEL,static_cast<WPARAM>(restored),0);
            const auto topIndex=restoredTop>=0?restoredTop:(std::max)(0,(std::min)(static_cast<int>(top),static_cast<int>(p.labels.size())-1));
            SendMessageW(p.list,LB_SETTOPINDEX,static_cast<WPARAM>(topIndex),0);
            SendMessageW(p.list,WM_SETREDRAW,TRUE,0);p.updating=false;InvalidateRect(p.list,nullptr,TRUE);
            p.token=transfer?transfer->token:0;
        }
        if(p.searching!=p.lastSearching)layoutFriends(p);
        p.revision=transfer?transfer->revision:0;p.lastFilter=filter;p.lastOnlineOnly=p.onlineOnly;p.lastSearching=p.searching;
        std::wstring action=changedSearch?L"Search changed in another panel. Search again.":L"Open IM";if(transfer && transfer->loading && p.presentation.contacts.empty())action=transfer->pending?L"Searching…":L"Loading…";
        if(transfer && !transfer->error.empty())action=wide(transfer->error);
        SetWindowTextW(p.open,L"Open IM");
        if(action!=L"Open IM")SetWindowTextW(p.label,(label+L"\r\n"+action).c_str());
        addTooltip(p.open,transfer && !transfer->error.empty()?wide(transfer->error).c_str():L"Open an IM as this character; sending stays explicit");
        const auto selected=SendMessageW(p.list,LB_GETCURSEL,0,0);
        const auto* resident=p.presentation.at(static_cast<std::size_t>(selected));
        const bool allowed=live && resident && s->contacts.selected(s->snapshot,*resident,p.searching?1u:0u,p.token);
        const bool directVisible=(chatPreferences.showMask&conversationCategory(Topic::Private))!=0;
        EnableWindow(p.open,allowed && directVisible);if(!directVisible)addTooltip(p.open,L"Enable Direct IMs under Chats & notifications before opening an IM in the host");EnableWindow(p.filter,live);EnableWindow(p.searchButton,live);EnableWindow(p.friendsButton,live);EnableWindow(p.list,live);
        EnableWindow(p.onlineOnlyControl,live && !p.searching);
        setToggle(p.friendsButton,!p.searching);setToggle(p.searchButton,p.searching);
    }
    void refreshFriends()
    {
        sharedFriends.slot=chatIndex;refreshFriendsPane(sharedFriends);
        for(auto& p:friendsWindows)refreshFriendsPane(p);
        for(int id=0;id<MonitorViews;++id){auto& p=monitors[id].friends;p.slot=monitorSet.bindings[id].enabled?monitorSet.bindings[id].slot:-1;refreshFriendsPane(p);}
    }
    void openFriendsWindow(int index,int origin=-1,bool search=false)
    {
        if(!contactsLive(index)){message(L"Log in this character before opening friends or New IM.");return;}
        auto& p=friendsWindows[index];createFriends(p,window,true,index,origin);if(!p.window)return;p.monitor=origin;
        if(search){p.searching=false;SetWindowTextW(p.filter,L"");}
        ShowWindow(p.window,SW_SHOWNORMAL);SetForegroundWindow(p.window);refreshFriendsPane(p);SetFocus(p.filter);
    }
    void toggleFriends(int origin=-1)
    {
        if(origin>=0)
        {
            auto& m=monitors[origin];if(origin==CyclingView && cycler.playing){cycler.pause();refreshMonitorChat(origin);}
            m.showFriends=!m.showFriends;createFriends(m.friends,m.window,false,monitorSet.bindings[origin].slot,origin);
            if(m.showFriends){RECT r{};GetWindowRect(m.window,&r);SetWindowPos(m.window,nullptr,0,0,(std::max)(static_cast<int>(r.right-r.left),theme.scale(640,m.dpi)),(std::max)(static_cast<int>(r.bottom-r.top),theme.scale(480,m.dpi)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);}
            layoutMonitorChat(origin);
        }
        else
        {
            showFriends=!showFriends;createFriends(sharedFriends,chatDetached?chatWindow:window,false,chatIndex,-1);
            if(showFriends && chatDetached){RECT r{};GetWindowRect(chatWindow,&r);SetWindowPos(chatWindow,nullptr,0,0,(std::max)(static_cast<int>(r.right-r.left),theme.scale(720,chatDpi)),static_cast<int>(r.bottom-r.top),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);}
            layout();
        }
        refreshFriends();
    }
    void friendsCommand(fs_host_ui::FriendsPane& p,WORD command,WORD notification)
    {
        if(command==PanelPin){pinWindow(p.window);return;}
        if(p.updating || !contactsLive(p.slot) || !p.source.owns(slots[p.slot]->snapshot))return;
        auto& s=*slots[p.slot];
        if(command==ContactFilter && notification==EN_CHANGE){if(!p.searching)refreshFriendsPane(p);return;}
        if(command==ContactOnlineOnly){p.onlineOnly=SendMessageW(p.onlineOnlyControl,BM_GETCHECK,0,0)==BST_CHECKED;refreshFriendsPane(p);return;}
        if(command==ContactFriends){p.searching=false;refreshFriendsPane(p);return;}
        if(command==ContactSearch)
        {
            const auto query=editText(p.filter);if(query.size()<2 || query.size()>127){message(L"Enter a resident name (at least two characters).");return;}
            s.contacts.beginSearch();p.searching=true;p.queryToken=s.contacts.search.token;auto request=contactRequest(p.slot,Kind::ContactSearch);request.width=1;request.event=s.contacts.search.token;request.text=query;
            s.pendingContact=request;s.hasPendingContact=true;s.contactsAt=0;refreshFriendsPane(p);return;
        }
        if(command==ContactPopout){openFriendsWindow(p.slot,p.monitor);return;}
        if(command==ContactList && notification!=LBN_DBLCLK){refreshFriendsPane(p);return;}
        if(command!=ContactOpen && !(command==ContactList && notification==LBN_DBLCLK))return;
        const auto at=SendMessageW(p.list,LB_GETCURSEL,0,0);
        const auto* resident=p.presentation.at(static_cast<std::size_t>(at));if(!resident)return;
        const auto selected=*resident;
        if(!(chatPreferences.showMask&conversationCategory(Topic::Private))){message(L"Direct IMs are hidden. Enable them in Chats & notifications before opening an IM.");return;}
        if(!s.contacts.selected(s.snapshot,selected,p.searching?1u:0u,p.token)){refreshFriendsPane(p);return;}
        if(s.hasPendingAction || (s.waiting && s.request.kind==Kind::OpenIM)){message(L"Character is busy; choose Open IM again shortly.");return;}
        auto request=contactRequest(p.slot,Kind::OpenIM);request.width=p.searching?1u:0u;request.event=p.token;request.recipient=selected.id;
        s.openIMOrigin=p.monitor;s.pendingAction=request;s.hasPendingAction=true;
    }
    bool cycleEligible(int index) const
    {return index!=active && contactsLive(index) && !(slots[index]->snapshot.flags&(PreviewUnavailable|Promoting));}
    void selectCycle(int target)
    {
        if(!cycleEligible(target))return;
        auto& m=monitors[CyclingView];auto& b=monitorSet.bindings[CyclingView];
        if(b.enabled && b.slot==target && b.key.owns(slots[target]->snapshot))return;
        m.editor.releaseFocus(m.window);
        // Draft edits have already entered the source-owned ChatView via EN_CHANGE.
        // Clear pixels and selection before publishing a different sender label.
        clearMonitor(CyclingView);if(m.window){InvalidateRect(m.window,nullptr,FALSE);UpdateWindow(m.window);}m.frame={};m.chat.bind(MonitorBinding{},nullptr,nullptr);
        if(m.friends.window)ShowWindow(m.friends.window,SW_HIDE);m.showFriends=false;
        monitorSet.cycle(target,slots[target]->snapshot,b.enabled?b.size:monitorSize,b.enabled?b.rate:monitorRate);m.dirty=true;
        refreshMonitorChat(CyclingView);layoutMonitorChat(CyclingView);
    }
    void openCycler()
    {
        if(busy())return;const int target=cycler.next(-1,1,experience.order,[this](int i){return cycleEligible(i);});
        if(target<0){message(L"Log in an inactive character before opening a cycling monitor.");return;}
        cycleOpen=true;cycler.seconds=experience.cycleSeconds;cycler.play(GetTickCount64());openMonitor(target,true);
    }
    void cycleCommand(int command)
    {
        if(!cycleOpen)return;
        if(command==CyclePlay)
        {
            if(cycler.playing)cycler.pause();
            else
            {
                auto& m=monitors[CyclingView];m.showFriends=false;layoutMonitorChat(CyclingView);if(m.compose && IsChild(m.window,GetFocus()))SetFocus(m.cyclePlay);
                cycler.seconds=experience.cycleSeconds;cycler.play(GetTickCount64());
            }
        }
        else if(command==CycleInterval)
        {
            auto& m=monitors[CyclingView];const auto text=editText(m.cycleInterval);unsigned int value=0;
            for(const auto c:text){if(c<'0' || c>'9' || value>3600){value=0;break;}value=value*10+static_cast<unsigned int>(c-'0');}
            if(!CyclingMonitor::validInterval(value)){SetWindowTextW(m.cycleInterval,std::to_wstring(experience.cycleSeconds).c_str());return;}
            experience.cycleSeconds=value;cycler.seconds=value;if(cycler.playing)cycler.play(GetTickCount64());
        }
        else
        {
            cycler.pause();const int next=cycler.next(monitorSet.bindings[CyclingView].slot,command==CyclePrevious?-1:1,experience.order,[this](int i){return cycleEligible(i);});selectCycle(next);
        }
        refreshMonitorChat(CyclingView);
    }
    void tickCycler()
    {
        if(!cycleOpen)return;
        cycler.seconds=experience.cycleSeconds;
        if(busy() || menuOpen || preferencesOpen || monitors[CyclingView].conversationChoice.open){if(cycler.playing)cycler.play(GetTickCount64());return;}
        const auto& b=monitorSet.bindings[CyclingView];
        const bool invalid=!b.enabled || !cycleEligible(b.slot) || !b.key.owns(slots[b.slot]->snapshot);
        auto& m=monitors[CyclingView];
        const auto action=cycler.action(!invalid,m.window && IsWindowVisible(m.window) && !IsIconic(m.window),GetTickCount64());
        if(action==CyclingMonitor::Action::Disable)
        {
            // Keep the last assignment disabled until an explicit Next/Play;
            // neither a disconnect nor reconnect can change a paused sender.
            if(b.enabled || m.chat.bound || !m.pixels.empty())
            {
                m.editor.releaseFocus(m.window);clearMonitor(CyclingView);monitorSet.stop(CyclingView);
                m.frame={};m.dirty=true;refreshMonitorChat(CyclingView);
            }
            return;
        }
        if(action==CyclingMonitor::Action::Advance)
        {
            const int next=cycler.next(b.slot,1,experience.order,[this](int i){return cycleEligible(i);});
            if(next>=0)selectCycle(next);else{m.editor.releaseFocus(m.window);clearMonitor(CyclingView);monitorSet.stop(CyclingView);m.frame={};m.dirty=true;refreshMonitorChat(CyclingView);}
        }
    }
    void openAllMonitors()
    {for(const auto i:experience.order)if(cycleEligible(static_cast<int>(i)))openMonitor(static_cast<int>(i));}
    MonitorMinimum monitorMinimumFor(int id,UINT targetDpi) const
    {return monitorMinimum(monitors[id].showChat,id==CyclingView,static_cast<unsigned int>(theme.uiDpi(targetDpi)));}
    UINT tileDestinationDpi(const RECT& work,UINT fallback) const
    {
        using WindowDpi=UINT(WINAPI*)(HWND);
        const auto getDpi=reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        if(!getDpi)return fallback;
        // A hidden, non-activating probe queries the target screen in this
        // process's DPI awareness context without moving any actual monitor.
        const HWND probe=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
            work.left+(work.right-work.left)/2,work.top+(work.bottom-work.top)/2,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!probe)return 0;
        const UINT result=getDpi(probe);DestroyWindow(probe);return result>=96?result:0;
    }
    void arrangeMonitors(POINT point,bool row)
    {
        MONITORINFO info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info))return;
        std::vector<int> ids;for(int id=0;id<MonitorViews;++id)if(monitors[id].window && IsWindowVisible(monitors[id].window) && (monitorSet.bindings[id].enabled || (id==CyclingView && cycleOpen)))ids.push_back(id);
        if(ids.empty()){message(L"Open a monitor before arranging windows.");return;}
        const auto& r=info.rcWork;const auto tiles=monitorTiles({static_cast<int>(r.left),static_cast<int>(r.top),static_cast<int>(r.right-r.left),static_cast<int>(r.bottom-r.top)},static_cast<unsigned int>(ids.size()),row);
        UINT fallback=96;for(const auto id:ids)fallback=(std::max)(fallback,monitors[id].dpi);
        const UINT targetDpi=tileDestinationDpi(r,fallback);
        if(!targetDpi){message(L"Unable to determine this screen's scaling; monitors were left in place.");return;}
        std::vector<MonitorMinimum> minimums;for(const auto id:ids)minimums.push_back(monitorMinimumFor(id,targetDpi));
        if(!tilesRespectMinimums(tiles,minimums))
        {message(L"These monitors cannot fit on this screen at the current scaling.");return;}
        // Place outer rectangles in the invoked screen’s work area without
        // changing each monitor’s preview size, rate, source or pin.
        tilingMonitors=true;
        for(std::size_t i=0;i<tiles.size();++i){const auto& t=tiles[i];ShowWindow(monitors[ids[i]].window,SW_SHOWNOACTIVATE);SetWindowPos(monitors[ids[i]].window,nullptr,t.x,t.y,t.width,t.height,SWP_NOZORDER|SWP_NOACTIVATE);}
        tilingMonitors=false;
    }
    void clearMonitor(int id)
    {
        auto& monitor = monitors[id];
        if (!monitor.pixels.empty()) { monitor.pixels.clear(); monitor.dirty = true; }
    }
    void stopMonitor(int id, bool hide = false)
    {
        if (id < 0 || id >= MonitorViews) return;
        if(id==CyclingView){cycleOpen=false;cycler.pause();}
        monitorSet.stop(id); clearMonitor(id); monitors[id].dirty = true;
        refreshMonitorChat(id);
        if (hide && monitors[id].window) ShowWindow(monitors[id].window, SW_HIDE);
    }
    void stopMonitors()
    { for (int i = 0; i < MonitorViews; ++i) stopMonitor(i, true); }
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
    {return monitorSet.visibleSources([this](int id){return wantsMonitor(id);});}
    Message monitorPolicy(int index) const
    {
        return monitorSet.policyFor(index,[this](int id){return wantsMonitor(id);});
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
        if (id < 0 || id >= MonitorViews || !monitorSet.bindings[id].enabled) return;
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
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(conversationFilters()), L"Show conversations");
        AppendMenuW(popup,MF_STRING,23,L"Tile monitors as grid");AppendMenuW(popup,MF_STRING,24,L"Arrange monitors as row");
        AppendMenuW(popup, MF_STRING, 20, L"Close this monitor");
        POINT point{}; GetCursorPos(&point);
        menuOpen = true;
        const auto choice = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, monitors[id].window, nullptr);
        menuOpen = false;
        DestroyMenu(popup);
        if(filterCommand(choice))return;if(choice==23 || choice==24){arrangeMonitors(point,choice==24);return;}
        const auto& current = monitorSet.bindings[id];
        if (!current.enabled || current.slot != captured.slot || !validSlot(current.slot) || !slots[current.slot] ||
            !captured.key.owns(slots[current.slot]->snapshot) || !current.key.owns(slots[current.slot]->snapshot))
        { message(L"Monitor session changed; its old menu choice was discarded."); return; }
        if (choice >= 1 && choice <= 3) monitorSet.bindings[id].size = static_cast<unsigned int>(choice - 1);
        else if (choice >= 11 && choice <= 14) monitorSet.bindings[id].rate = static_cast<unsigned int>(choice - 11);
        else if (choice == 20) stopMonitor(id, true);
        else if (choice == 21) setMonitorChat(id,!monitors[id].showChat);
        else if (choice == 22)
        {monitorSize=current.size;monitorRate=current.rate;message(L"Monitor defaults updated; host preferences save automatically.");}
    }
    void openMonitor(int index,bool cycle=false)
    {
        if (!validSlot(index) || !slots[index] || !slots[index]->running() || slots[index]->detached || !slots[index]->pipe.alive() || slots[index]->snapshot.state != State::Ready)
        { message(L"Log in this character before opening its read-only monitor."); return; }
        if (busy() || index == active) { message(L"Choose an inactive character after the switch finishes to open its monitor."); return; }
        if(cycle)monitors[CyclingView].editor.releaseFocus(monitors[CyclingView].window);
        const int id=cycle?(monitorSet.cycle(index,slots[index]->snapshot,monitorSize,monitorRate)?CyclingView:-1):monitorSet.open(index,slots[index]->snapshot,monitorSize,monitorRate);
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
        if (newMonitor) { installPanelMenu(monitor.window); restorePlacement(monitor.window,id==CyclingView?8u:static_cast<unsigned int>(id)+3,monitor.dpi); }
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
    std::array<HWND,10> monitorChatControls(int id) const
    {
        const auto& m = monitors[id]; return {m.label,m.choices,m.history,m.compose,m.send,m.read,m.latest,m.close,m.newIM,m.friendsToggle};
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
        return (std::max)(0,static_cast<int>(r.bottom)-theme.scale((m.showChat?266:60)+(id==CyclingView?32:0),m.dpi));
    }
    void monitorFont(int id)
    {
        auto& m = monitors[id];
        m.friends.dpi=m.dpi;if(m.friends.window)friendsFont(m.friends);
        const HFONT next = CreateFontW(-theme.scale(12,m.dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if (!next) return;
        for(auto item:monitorChatControls(id))if(item)fs_host_ui::setNativeFont(item,next);
        for(auto item:{m.exchange,m.chatToggle,m.pin,m.options,m.cyclePlay,m.cyclePrevious,m.cycleNext,m.cycleInterval})if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
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
        m.newIM=make(L"BUTTON",L"New IM",BS_PUSHBUTTON|WS_TABSTOP,ChatNewIM);m.friendsToggle=make(L"BUTTON",L"Friends",BS_PUSHBUTTON|WS_TABSTOP,ChatFriends);
        if(id==CyclingView){m.cyclePrevious=make(L"BUTTON",L"‹",BS_PUSHBUTTON|WS_TABSTOP,CyclePrevious);m.cycleNext=make(L"BUTTON",L"›",BS_PUSHBUTTON|WS_TABSTOP,CycleNext);m.cyclePlay=make(L"BUTTON",L"Pause",BS_PUSHBUTTON|WS_TABSTOP,CyclePlay);m.cycleInterval=make(L"EDIT",std::to_wstring(experience.cycleSeconds).c_str(),ES_NUMBER|WS_BORDER|WS_TABSTOP,CycleInterval);SendMessageW(m.cycleInterval,EM_SETLIMITTEXT,4,0);addTooltip(m.cycleInterval,L"Seconds between characters (2–3,600). Apply by leaving this field.");addTooltip(m.cyclePrevious,L"Previous inactive character; pauses cycling");addTooltip(m.cycleNext,L"Next inactive character; pauses cycling");}
        m.label = make(L"STATIC",L"Send as: unavailable",SS_LEFT|SS_ENDELLIPSIS|SS_NOPREFIX,0);
        m.choices = make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL|WS_TABSTOP,ChatConversation);
        m.read = make(L"BUTTON",L"Read",BS_PUSHBUTTON|WS_TABSTOP,ChatRead);
        m.close = make(L"BUTTON",L"×",BS_PUSHBUTTON|WS_TABSTOP,ChatClose);
        m.latest = make(L"BUTTON",L"Latest",BS_PUSHBUTTON|WS_TABSTOP,ChatLatest);
        m.history = make(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP,0);
        m.compose = make(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_BORDER|WS_TABSTOP,ChatCompose);
        m.send = make(L"BUTTON",L"Send",BS_PUSHBUTTON|WS_TABSTOP,ChatSend);
        auto items = std::vector<HWND>{m.label,m.choices,m.history,m.compose,m.send,m.read,m.close,m.exchange,m.chatToggle,m.pin,m.options,m.latest,m.newIM,m.friendsToggle};
        if(id==CyclingView){items.insert(items.end(),{m.cyclePrevious,m.cycleNext,m.cyclePlay,m.cycleInterval});}
        if (std::any_of(items.begin(),items.end(),[](HWND item) { return !item; }))
        {
            for (auto item : items) if (item) DestroyWindow(item);
            m.label = m.choices = m.history = m.compose = m.send = m.read = m.close = m.exchange = m.chatToggle = m.pin = m.options = nullptr;
            m.latest=m.newIM=m.friendsToggle=m.cyclePrevious=m.cycleNext=m.cyclePlay=m.cycleInterval=nullptr; m.showChat = m.updatingChat = false;
            message(L"Unable to create monitor Chat controls. The read-only image remains available."); return;
        }
        if (m.compose) SendMessageW(m.compose,EM_SETLIMITTEXT,1023,0);
        SendMessageW(m.history,EM_SETLIMITTEXT,2*1024*1024,0);
        m.transcript.attach(m.history,m.latest);
        m.editor.attach(m.compose,[this,id] { monitorChatCommand(id,ChatSend,BN_CLICKED); });
        m.editor.enter=[this,id]{if(id==CyclingView && cycler.playing && !experience.requirePauseToChat){cycler.enteredComposer(false);refreshMonitorChat(id);}};
        if(id==CyclingView)m.editor.acceptsInput=[this,id]
        {
            const auto& monitor=monitors[id];const auto* slot=monitorChatSlot(id);
            return slot && IsWindowEnabled(monitor.compose) && cyclingComposerInputAllowed(cycler,GetFocus()==monitor.compose,
                monitor.chat,monitorSet.bindings[id],slot->snapshot,slot->chat);
        };
        addTooltip(m.newIM,L"Choose a friend or search for a resident; open an IM as this character");addTooltip(m.friendsToggle,L"Show or hide this character’s friends sidebar");
        addTooltip(m.latest,L"Jump to the latest message and resume following chat");
        m.updatingChat = false; monitorFont(id);
        addTooltip(m.exchange,L"Exchange this monitored character with the active world; chat drafts remain bound to their owners");addTooltip(m.pin,L"Keep only this monitor above other windows");
        addTooltip(m.read,L"Mark this character's selected conversation read");
        addTooltip(m.close,L"Close this conversation in the host; native groups and calls stay open");
        addTooltip(m.send,L"Send as the character shown above; the active world does not change");
        addTooltip(m.choices,L"Nearby chat and this character's existing conversations");
    }
    void layoutMonitorChat(int id)
    {
        auto& m=monitors[id];if(!m.label)return;const auto scale=[this,&m](int v){return theme.scale(v,m.dpi);};RECT r{};GetClientRect(m.window,&r);
        const int fullWidth=static_cast<int>(r.right),side=m.showChat && m.showFriends?(std::min)(scale(260),fullWidth/2):0;
        const int width=(std::max)(1,fullWidth-scale(12)-side),image=monitorImageHeight(id),header=image+scale(28),cycle=id==CyclingView?scale(32):0,top=image+scale(60)+cycle;
        for(HWND child:{m.exchange,m.chatToggle,m.pin,m.options})ShowWindow(child,SW_SHOWNOACTIVATE);
        const int cell=(fullWidth-scale(12))/4;
        MoveWindow(m.exchange,scale(6),header,cell-scale(3),scale(26),TRUE);MoveWindow(m.chatToggle,scale(6)+cell,header,cell-scale(3),scale(26),TRUE);MoveWindow(m.pin,scale(6)+cell*2,header,cell-scale(3),scale(26),TRUE);MoveWindow(m.options,scale(6)+cell*3,header,cell-scale(3),scale(26),TRUE);
        if(id==CyclingView)
        {
            MoveWindow(m.cyclePrevious,scale(6),header+scale(30),scale(32),scale(26),TRUE);MoveWindow(m.cyclePlay,scale(42),header+scale(30),(std::max)(scale(40),fullWidth-scale(174)),scale(26),TRUE);
            MoveWindow(m.cycleNext,fullWidth-scale(128),header+scale(30),scale(32),scale(26),TRUE);MoveWindow(m.cycleInterval,fullWidth-scale(92),header+scale(30),scale(86),scale(26),TRUE);
            for(auto child:{m.cyclePrevious,m.cyclePlay,m.cycleNext,m.cycleInterval})ShowWindow(child,SW_SHOWNOACTIVATE);
        }
        for(auto item:monitorChatControls(id))if(item)ShowWindow(item,m.showChat?SW_SHOWNOACTIVATE:SW_HIDE);
        if(m.showChat)
        {
            MoveWindow(m.label,scale(10),top+scale(4),(std::max)(1,width-scale(66)),scale(20),TRUE);
            MoveWindow(m.latest,width-scale(56),top,scale(56),scale(24),TRUE);
            MoveWindow(m.newIM,scale(6),top+scale(26),scale(64),scale(24),TRUE);MoveWindow(m.friendsToggle,scale(76),top+scale(26),scale(68),scale(24),TRUE);
            MoveWindow(m.choices,scale(6),top+scale(54),(std::max)(1,width-scale(98)),scale(180),TRUE);
            MoveWindow(m.read,width-scale(86),top+scale(54),scale(56),scale(24),TRUE);MoveWindow(m.close,width-scale(26),top+scale(54),scale(26),scale(24),TRUE);
            MoveWindow(m.history,scale(6),top+scale(82),(std::max)(1,width-scale(12)),scale(82),TRUE);m.transcript.latestVisibility();
            MoveWindow(m.compose,scale(6),top+scale(170),(std::max)(1,width-scale(62)),scale(28),TRUE);MoveWindow(m.send,width-scale(56),top+scale(170),scale(56),scale(28),TRUE);
        }
        if(m.friends.window){ShowWindow(m.friends.window,m.showChat && m.showFriends?SW_SHOWNOACTIVATE:SW_HIDE);MoveWindow(m.friends.window,fullWidth-side,top,side,(std::max)(1,static_cast<int>(r.bottom)-top),TRUE);layoutFriends(m.friends);}
        setToggle(m.friendsToggle,m.showFriends);InvalidateRect(m.window,nullptr,FALSE);
    }
    void setMonitorChat(int id,bool enabled)
    {
        auto& m = monitors[id]; createMonitorChat(id); if (!m.label) return;
        m.showChat = enabled;
        if (enabled)
        {
            RECT r{}; GetWindowRect(m.window,&r);
            const int width = (std::max)(static_cast<int>(r.right-r.left),theme.scale(320,m.dpi));
            const int height = (std::max)(static_cast<int>(r.bottom-r.top),theme.scale(id==CyclingView?424:392,m.dpi));
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
        if(id==CyclingView && (!slot || !binding.enabled || !m.chat.identity.owns(slot->snapshot)))m.editor.releaseFocus(m.window);
        const bool changed = m.chat.bind(binding,slot ? &slot->snapshot : nullptr,slot ? &slot->chat : nullptr);
        const bool restricted = slot && (slot->snapshot.flags & ChatRestricted);
        const bool live = m.showChat && slot && !restricted;
        std::vector<std::string> ids; std::vector<std::wstring> choices;
        if (live) for (const auto& entry : slot->chat.conversations) if(shownConversation(entry.second))
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
        if (syncChoices(m.choices,choices,selection,changed || !live || privacyChanged ||
            (live && (previous==slot->chat.conversations.end() || !shownConversation(previous->second)))))
        {
            m.conversationIds = std::move(ids);
            if (live && !m.conversationIds.empty())
            {
                const auto& selected=m.conversationIds[static_cast<std::size_t>(selection)];
                if(id==CyclingView && selected!=m.chat.identity.conversation)m.editor.releaseFocus(m.window);
                m.chat.select(binding,slot->snapshot,slot->chat,selected);
            }
        }
        const auto selectedEntry=live?slot->chat.conversations.find(m.chat.identity.conversation):std::map<std::string,Conversation>::iterator{};
        const bool selectedShown=live && selectedEntry!=slot->chat.conversations.end() && shownConversation(selectedEntry->second);
        const auto send = selectedShown ? m.chat.request(Kind::SendChat,binding,slot->snapshot,slot->chat) : std::optional<Message>{};
        const auto read = selectedShown ? m.chat.request(Kind::MarkRead,binding,slot->snapshot,slot->chat) : std::optional<Message>{};
        const auto current = live ? slot->chat.conversations.find(m.chat.identity.conversation) : std::map<std::string,Conversation>::iterator{};
        const bool blocked = live && current != slot->chat.conversations.end() && current->second.restricted;
        std::wstring label = L"Send as: "+(slot ? characterName(binding.slot) : L"unavailable");
        const bool cyclingLocked=id==CyclingView && cycler.playing && experience.requirePauseToChat;
        if(cyclingLocked)label+=L" · Pause to chat.";
        if (restricted || blocked) label += L" · restricted; use native viewer";
        else if (slot && slot->chat.gap) label += L" · history gap";
        if (label != m.chatStatus) { m.chatStatus = label; SetWindowTextW(m.label,label.c_str()); }
        const auto color = slot ? accountColor(binding.slot) : GetSysColor(COLOR_GRAYTEXT);
        if (color != m.color) { m.color = color; InvalidateRect(m.window,nullptr,FALSE); }
        const bool historyVisible = selectedShown && !blocked;
        // The render/status timer must not rebuild four transcripts while no
        // chat changed. Privacy/source changes still clear them immediately.
        if (changed || m.historyVisible != historyVisible || (historyVisible &&
            (m.historyCursor != slot->chat.cursor || m.historyLines != slot->chat.lines.size() || m.historyConversation != m.chat.identity.conversation)))
        {
            std::wstring history; std::vector<std::pair<std::uint64_t,LONG>> offsets;
            if (historyVisible) for (const auto& line : slot->chat.lines) if (line.conversation == m.chat.identity.conversation)
            {
                offsets.emplace_back(line.event,static_cast<LONG>(history.size()));
                history += wide(line.sender)+(line.sender.empty() ? L"" : L": ")+wide(line.text)+L"\r\n";
            }
            const auto through = historyVisible && current != slot->chat.conversations.end() ? current->second.lastIncomingEvent : 0;
            m.transcript.update(m.chat.identity,std::move(history),std::move(offsets),through);
            m.historyVisible = historyVisible;
            m.historyCursor = slot ? slot->chat.cursor : 0; m.historyLines = slot ? slot->chat.lines.size() : 0;
            m.historyConversation = m.chat.identity.conversation;
        }
        const auto draft = send ? slot->chat.conversations.at(m.chat.identity.conversation).draft : std::string{};
        if (editText(m.compose) != draft) SetWindowTextW(m.compose,wide(draft).c_str());
        const bool sending = slot && (slot->hasPendingSend || (slot->waiting && slot->request.kind == Kind::SendChat));
        EnableWindow(m.choices,live && !cyclingLocked); EnableWindow(m.compose,send.has_value() && !cyclingLocked);
        SendMessageW(m.compose,EM_SETREADONLY,sending?TRUE:FALSE,0);
        EnableWindow(m.send,send.has_value() && !sending && !cyclingLocked); EnableWindow(m.read,read.has_value() && !cyclingLocked);EnableWindow(m.close,selectedShown && selectedEntry->second.topic!=Topic::Nearby && !cyclingLocked);
        EnableWindow(m.newIM,live && !cyclingLocked);EnableWindow(m.friendsToggle,live && !cyclingLocked);
        if(id==CyclingView){SetWindowTextW(m.cyclePlay,cycler.playing?L"Pause":L"Play");addTooltip(m.cyclePlay,cycler.playing?L"Pause on this character to use Chat":L"Resume cycling; Chat keeps each character’s draft");}
        m.updatingChat = false;
    }
    void monitorChatCommand(int id,WORD command,WORD notification)
    {
        auto& m = monitors[id]; if (!m.label || !m.showChat || m.updatingChat || (id==CyclingView && cycler.playing && experience.requirePauseToChat)) return;
        if(command==ChatNewIM){if(id==CyclingView)cycler.pause();openFriendsWindow(monitorSet.bindings[id].slot,id,true);refreshMonitorChat(id);return;}
        if(command==ChatFriends){toggleFriends(id);return;}
        auto* slot = monitorChatSlot(id); if (!slot) { refreshMonitorChat(id); return; }
        const auto& binding = monitorSet.bindings[id];
        if (!m.chat.owns(binding,slot->snapshot,slot->chat)) { refreshMonitorChat(id); return; }
        if (command == ChatConversation)
        {
            const auto selected = choiceNotification(m.choices,m.conversationChoice,notification,m.conversationIds,m.chat.identity.conversation);
            if (selected && m.chat.select(binding,slot->snapshot,slot->chat,*selected)) refreshMonitorChat(id);
        }
        else if (command == ChatLatest) m.transcript.tail();
        else if (command == ChatCompose && notification == EN_CHANGE)
        {
            if (m.chat.draft(binding,slot->snapshot,slot->chat,editText(m.compose)))
            {
                if (chatIndex == binding.slot && conversation == m.chat.identity.conversation) refreshChat(true);
                // Other visible editors share the same canonical conversation draft.
                for (int other = 0; other < MonitorViews; ++other) if (other != id) refreshMonitorChat(other);
            }
        }
        else if (command == ChatSend && notification == BN_CLICKED)
        {
            auto request = m.chat.request(Kind::SendChat,binding,slot->snapshot,slot->chat);
            if (request) { request->text = editText(m.compose); if (queueChat(binding.slot,std::move(*request))) m.transcript.tail(); }
        }
        else if(command==ChatClose && notification==BN_CLICKED)closeConversation(binding.slot,m.chat.identity.conversation);
        else if (command == ChatRead && notification == BN_CLICKED)
        {
            const auto request = m.chat.request(Kind::MarkRead,binding,slot->snapshot,slot->chat);
            if (request && !send(binding.slot,Kind::MarkRead,Mode::Warm,0,&*request)) message(L"Viewer is busy; try Mark read again.");
        }
    }
    void updateMonitors()
    {
        for (int id = 0; id < MonitorViews; ++id)
        {
            auto& monitor = monitors[id]; auto& binding = monitorSet.bindings[id];
            if (!monitor.window) continue;
            refreshMonitorChat(id);
            if (!binding.enabled)
            {
                clearMonitor(id);
                if (monitor.dirty)
                {
                    const bool pausedUnavailable=id==CyclingView && cycleOpen && !cycler.playing;
                    SetWindowTextW(monitor.window,pausedUnavailable?L"Cycling monitor · paused · character unavailable":L"Character monitor · session ended or stopped; choose again");
                    monitor.title.clear();monitor.previewStatus=pausedUnavailable?L"Paused · choose Next or Play":L"Session ended or stopped"; InvalidateRect(monitor.window, nullptr, FALSE); monitor.dirty = false;
                }
                continue;
            }
            const auto* slot = validSlot(binding.slot) ? slots[binding.slot].get() : nullptr;
            std::wstring status=L"Read-only preview";
            std::wstring title = id==CyclingView?L"Cycling monitor: ":L"Monitor: ";
            title += slot && !(slot->snapshot.flags & ChatRestricted) && binding.key.owns(slot->snapshot) && !slot->snapshot.name.empty() ? characterName(binding.slot) : L"unavailable";
            title += monitor.showChat ? L" · read-only image + chat" : L" · read only";
            if (!slot || !slot->running() || slot->detached || !slot->pipe.alive() || !binding.key.owns(slot->snapshot))
            { if(id==CyclingView){monitorSet.stop(id);clearMonitor(id);}else stopMonitor(id); title += L" · session ended; choose again";status=L"Session ended · choose again"; }
            else if (slot->snapshot.state != State::Ready) { if(id==CyclingView){monitorSet.stop(id);clearMonitor(id);}else stopMonitor(id); title += L" · disconnected";status=L"Disconnected"; }
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
            const std::wstring name = characterName(i);
            accounts.push_back(name);
        }
        syncChoices(chatAccount, accounts, chatIndex);
        auto* slot = slots[chatIndex].get();
        const bool ready = slot && !slot->detached && slot->pipe.alive() && slot->snapshot.state == State::Ready;
        const bool restricted = slot && (slot->snapshot.flags & ChatRestricted);
        const auto selectedChat = slot ? slot->chat.conversations.find(conversation) : std::map<std::string,Conversation>::iterator{};
        bool conversationBlocked = !slot || selectedChat==slot->chat.conversations.end() || selectedChat->second.restricted || !shownConversation(selectedChat->second);
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
            if (slot && !restricted) for (const auto& entry : slot->chat.conversations) if(shownConversation(entry.second))
            {
                ids.push_back(entry.first);
                std::wstring name = wide(entry.second.title);
                if (entry.second.unread) name += L" · " + std::to_wstring(entry.second.unread) + L" unread";
                choices.push_back(std::move(name));
            }
            const auto found = std::find(ids.begin(), ids.end(), conversation);
            const int selection = found == ids.end() ? 0 : static_cast<int>(found - ids.begin());
            if (syncChoices(chatConversation, choices, selection, forceChoices || restricted || !ready || conversationBlocked))
            {
                conversationIds = std::move(ids);
                conversation = conversationIds.empty()?std::string{}:conversationIds[static_cast<std::size_t>(selection)];
            }
            const auto draft = slot ? slot->chat.conversations.find(conversation) : std::map<std::string, Conversation>::iterator{};
            conversationBlocked = !slot || draft==slot->chat.conversations.end() || draft->second.restricted || !shownConversation(draft->second);
            const std::string wanted = slot && ready && !restricted && !conversationBlocked && draft != slot->chat.conversations.end() ? draft->second.draft : "";
            if (editText() != wanted) SetWindowTextW(chatCompose, wide(wanted).c_str());
        }
        std::wstring history; std::vector<std::pair<std::uint64_t,LONG>> offsets;
        if (slot && !restricted && !conversationBlocked) for (const auto& line : slot->chat.lines)
            if (line.conversation == conversation)
            {
                offsets.emplace_back(line.event,static_cast<LONG>(history.size()));
                history += wide(line.sender)+(line.sender.empty() ? L"" : L": ")+wide(line.text)+L"\r\n";
            }
        ChatKey historyKey = slot ? slot->chat.identity : ChatKey{}; historyKey.conversation = conversation;
        const auto paintedChat=slot?slot->chat.conversations.find(conversation):std::map<std::string,Conversation>::iterator{};
        const auto through = slot && !restricted && !conversationBlocked && paintedChat != slot->chat.conversations.end() ? paintedChat->second.lastIncomingEvent : 0;
        transcript.update(historyKey,std::move(history),std::move(offsets),through);
        const bool sending = slot && (slot->hasPendingSend || (slot->waiting && slot->request.kind == Kind::SendChat));
        const auto selected = slot ? slot->chat.conversations.find(conversation) : std::map<std::string, Conversation>::iterator{};
        const bool isNotice = slot && selected != slot->chat.conversations.end() && selected->second.topic == Topic::Notice;
        sharedFriends.slot=chatIndex;refreshFriendsPane(sharedFriends);
        EnableWindow(chatNewIM,ready && !restricted);EnableWindow(chatFriends,ready && !restricted);
        EnableWindow(chatCompose, ready && !restricted && !conversationBlocked && !isNotice);
        SendMessageW(chatCompose,EM_SETREADONLY,sending?TRUE:FALSE,0);
        EnableWindow(chatSend, ready && !restricted && !conversationBlocked && !sending && !isNotice);
        EnableWindow(chatReview, ready && !conversationBlocked); EnableWindow(chatRead, ready && !restricted && !conversationBlocked);
        updatingChat = false;
    }
    void editChatSettings(unsigned int page=0) { editPreferences(page?PrefAttention:PrefConversations); }
    bool automaticRead(int index)
    {
        auto& slot = *slots[index]; const auto now = GetTickCount64();
        if (busy() || slot.detached || slot.snapshot.state != State::Ready ||
            (slot.snapshot.flags & (ChatRestricted|Promoting|Error)) || !slot.chat.identity.owns(slot.snapshot) || now-slot.autoReadAt < 1000) return false;
        std::string selected; bool displayed = false;
        const auto displayedBy = [&](const fs_host_ui::Transcript& history)
        {
            if (!chatPreferences.visibleRead || !history.bound || !history.source.owns(slot.snapshot) || !history.readReady()) return false;
            const auto found = slot.chat.conversations.find(history.source.conversation);
            return found != slot.chat.conversations.end() && shownConversation(found->second) && !found->second.restricted && found->second.topic != Topic::Notice &&
                found->second.unread && found->second.lastIncomingEvent && history.painted == found->second.lastIncomingEvent;
        };
        if (displayedBy(transcript)) { selected = transcript.source.conversation; displayed = true; }
        for(int id=0;id<MonitorViews;++id)if(!displayed && !(id==CyclingView && cycler.playing) && monitors[id].showChat && displayedBy(monitors[id].transcript))
        {selected=monitors[id].transcript.source.conversation;displayed=true;}
        bool delayed = false;
        if (index == active && chatPreferences.activeRead)
            for (const auto& entry : slot.chat.conversations)
            {
                const bool due = shownConversation(entry.second) && activeReadClock.due(entry.second,now,chatPreferences.delay);
                if (!displayed && !delayed && due) { selected = entry.first; delayed = true; }
            }
        if (!displayed && !delayed) return false;
        const auto& chat = slot.chat.conversations.at(selected);
        Message request; request.kind = Kind::MarkRead; request.worker = slot.chat.identity.worker;
        request.generation = slot.chat.identity.generation; request.account = slot.chat.identity.account; request.grid = slot.chat.identity.grid;
        request.conversation = selected; request.topic = chat.topic; request.event = chat.lastIncomingEvent;
        request.cursor = slot.chat.cursor; request.flags = AutomaticRead;
        if (!chatRequestAllowed(slot.chat,slot.snapshot,request)) return false;
        if (!send(index,Kind::MarkRead,Mode::Warm,0,&request)) return false;
        slot.autoReadAt = now; return true;
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
        for (int id = 0; id < MonitorViews; ++id) refreshMonitorChat(id);
    }
    bool queueChat(int index, Message request)
    {
        if (!validSlot(index) || !slots[index]) return false;
        auto& slot = *slots[index];
        if (request.kind != Kind::SendChat || !slot.running() || slot.detached || !slot.pipe.alive() || !chatRequestAllowed(slot.chat,slot.snapshot,request))
        { message(L"The chat account/session is unavailable or restricted. The draft was not sent."); return false; }
        if (slot.hasPendingSend || (slot.waiting && slot.request.kind == Kind::SendChat)) return false;
        if (request.text.empty() || request.text.size() > 1023 || !validUtf8(request.text, true))
        { message(L"Use a message of 1 to 1023 UTF-8 bytes."); return false; }
        slot.pendingSend = std::move(request); slot.hasPendingSend = true;
        message(L"Sending as " + characterName(index) + L" through this character's native chat…");
        refreshChat(); updateMonitors();
        return true;
    }
    void sendChat()
    {
        Message request = boundChat(Kind::SendChat); request.text = editText();
        if (queueChat(chatIndex,std::move(request))) transcript.tail();
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
        slot.hasPendingSend = slot.hasPendingTyping = slot.hasPendingAction = false;slot.hasPendingContact=false;slot.contacts={};
        for(int id=0;id<MonitorViews;++id)if(monitorSet.bindings[id].slot==index && monitorSet.bindings[id].enabled){if(id==CyclingView){monitorSet.stop(id);clearMonitor(id);}else stopMonitor(id);}
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
    HWND profileDialogOwner() const
    {
        const HWND panel = GetActiveWindow(); DWORD pid = 0;
        if (panel) GetWindowThreadProcessId(panel,&pid);
        return panel && pid == GetCurrentProcessId() ? panel : window;
    }
    std::filesystem::path pickSettingsFolder(const std::filesystem::path& initial)
    {
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
        { message(L"Unable to open the settings-folder picker."); return {}; }
        DWORD options = 0;
        if (FAILED(dialog->GetOptions(&options)) ||
            FAILED(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR)))
        { dialog->Release(); message(L"Unable to configure the settings-folder picker."); return {}; }
        dialog->SetTitle(L"Choose the main viewer's user_settings folder (or a settings backup)");
        IShellItem* start = nullptr;
        if (!initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&start))))
        { dialog->SetFolder(start); start->Release(); }
        std::filesystem::path source;
        if (SUCCEEDED(dialog->Show(profileDialogOwner())))
        {
            IShellItem* result = nullptr;
            if (SUCCEEDED(dialog->GetResult(&result)))
            {
                wchar_t* path = nullptr;
                if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH,&path))) { source = path; CoTaskMemFree(path); }
                result->Release();
            }
        }
        dialog->Release(); return source;
    }
    bool selectProfileCopy(int index, ProfileCopyRequest& result)
    {
        using namespace fs_host_ui;
        auto source = mainSettingsFolder();
        std::uint32_t groups = CopyAll;
        const auto plainDirectory = [](const std::filesystem::path& path)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        };
        const auto plainFile = [](const std::filesystem::path& path, std::uintmax_t limit)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) return false;
            std::error_code error; const auto size = std::filesystem::file_size(path,error);
            return !error && size && size <= limit;
        };
        if (source.empty() || !plainDirectory(source)) source = pickSettingsFolder(source);
        if (source.empty()) return false;
        std::filesystem::path chosenAccount;
        for (;;)
        {
            std::vector<std::filesystem::path> accounts;
            // Native installations keep account folders beside user_settings;
            // Backup & Restore puts them inside the selected backup directory.
            const auto accountRoot = _wcsicmp(source.filename().c_str(),L"user_settings") == 0 ? source.parent_path() : source;
            std::error_code error;
            std::filesystem::directory_iterator entry(accountRoot,error), end;
            unsigned int inspected = 0;
            while (!error && entry != end && inspected++ < 512)
            {
                const auto folder = entry->path();
                if (plainDirectory(folder) && (plainFile(folder/L"toolbars.xml",64u*1024u) || plainFile(folder/L"settings_per_account.xml",ProfileSettingsLimit))) accounts.push_back(folder);
                entry.increment(error);
            }
            std::sort(accounts.begin(),accounts.end());
            std::vector<std::wstring> choices{accounts.empty()?L"No saved account UI or toolbar layouts found":L"Choose the source account for toolbar / UI preferences"};
            unsigned int selected = 0;
            for (std::size_t i = 0; i < accounts.size(); ++i)
            {
                choices.push_back(accounts[i].filename().wstring());
                if (accounts[i] == chosenAccount) selected = static_cast<unsigned int>(i)+1;
            }
            if (!selected && accounts.size() == 1) selected = 1;
            if (accounts.empty()) groups &= ~(CopyToolbars|CopyAccountUI);
            std::vector<Field> fields{
                {L"Source settings",L"0",FieldType::Choice,{source.wstring(),L"Browse another installation / backup…"}},
                {L"Source account (toolbar / UI)",std::to_wstring(selected),FieldType::Choice,choices}};
            const std::pair<const wchar_t*,std::uint32_t> options[] = {
                {L"Graphics preferences",CopyGraphics},{L"Interface / skin",CopyInterface},
                {L"Custom colors",CopyColors},{L"Keyboard bindings",CopyBindings},
                {L"Quick preferences",CopyQuickPreferences},{L"Toolbar buttons / layout",CopyToolbars},
                {L"Movement / WASD / quick jump",CopyMovement},{L"Camera / view behavior",CopyCamera},
                {L"Avatar / beams / LookAt",CopyAvatar},{L"RLVa preferences",CopyRLVa},
                {L"Account interface preferences",CopyAccountUI}};
            for (const auto& option : options) fields.push_back({option.first,(groups&option.second)?L"1":L"0",FieldType::Check});
            const auto title = L"Copy from main profile → Character "+std::to_wstring(index+1);
            if (!edit(profileDialogOwner(),title.c_str(),fields)) return false;
            groups = 0;
            for (std::size_t i = 0; i < std::size(options); ++i) if (number(fields[i+2])) groups |= options[i].second;
            if (number(fields[0]))
            {
                const auto next = pickSettingsFolder(source);
                if (next.empty()) return false;
                source = next; chosenAccount.clear(); continue;
            }
            if (!validProfileCopyGroups(groups))
            { MessageBoxW(profileDialogOwner(),L"Choose at least one group to copy.",L"Copy from main profile",MB_OK|MB_ICONINFORMATION); continue; }
            if (!plainDirectory(source) || ((groups&CopyGlobalPreferences) && !plainFile(source/L"settings.xml",ProfileSettingsLimit)))
            { message(L"Choose a saved user_settings folder or backup containing settings.xml (up to 4 MiB)."); return false; }
            const auto account = number(fields[1]);
            if (account > 0 && account <= accounts.size()) chosenAccount = accounts[account-1];
            else chosenAccount.clear();
            if ((groups&(CopyToolbars|CopyAccountUI)) && chosenAccount.empty())
            { MessageBoxW(profileDialogOwner(),L"Choose a source account, or uncheck toolbar and account interface copying.",L"Copy account preferences",MB_OK|MB_ICONINFORMATION); continue; }
            break;
        }
        const auto destination = profiles/(L"Character"+std::to_wstring(index+1));
        const auto text = L"Copy the checked settings from:\n"+source.wstring()+
            ((groups&(CopyToolbars|CopyAccountUI))?L"\nAccount source: "+chosenAccount.wstring():L"")+
            L"\n\nInto:\n"+destination.wstring()+
            L"\n\nExisting files are backed up before replacement. Account UI and toolbars are applied only to the first account that successfully logs in. Selected preferences omitted from the source file use this viewer version's defaults. Credentials, other account data, chats, caches and security/automatic-action choices stay separate.\n\nSave and close the source viewer first for its latest choices. This opens a normal login; it does not log in automatically.";
        if (MessageBoxW(profileDialogOwner(),text.c_str(),L"Copy checked settings and open login?",MB_OKCANCEL|MB_ICONQUESTION) != IDOK) return false;
        result.settings = source; result.toolbars = chosenAccount; result.groups = groups;
        return true;
    }
    void copyProfileSettings(int index)
    {
        if (!validSlot(index) || busy() || closing || (slots[index] && slots[index]->running()))
        { message(L"Close the selected character's viewer before copying preferences into its profile."); return; }
        ProfileCopyRequest copy;
        const auto epoch = profileSetupEpoch;
        {
            ProfileSetupScope setup(profileSetupOpen);
            if (!selectProfileCopy(index,copy)) return;
        }
        if (epoch != profileSetupEpoch || closing)
        { message(L"Profile copy cancelled because management ended while setup was open."); return; }
        launch(index,copy);
    }
    int firstProfileChoice(int index)
    {
        using TaskDialog=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
        const auto dialog=reinterpret_cast<TaskDialog>(GetProcAddress(GetModuleHandleW(L"comctl32.dll"),"TaskDialogIndirect"));
        const TASKDIALOG_BUTTON buttons[] = {
            {3301,L"Copy from main profile…\nChoose appearance, movement, camera, avatar, RLVa, account UI and toolbar preferences."},
            {3302,L"Keep this profile's settings\nOpen normal login with its existing settings, or defaults for a new profile."}};
        const auto title = L"Set up Character "+std::to_wstring(index+1);
        if (dialog)
        {
            TASKDIALOGCONFIG options{}; options.cbSize=sizeof(options); options.hwndParent=profileDialogOwner();
            options.dwFlags=TDF_USE_COMMAND_LINKS|TDF_ALLOW_DIALOG_CANCELLATION; options.dwCommonButtons=TDCBF_CANCEL_BUTTON;
            options.pszWindowTitle=L"Folderstorm · first profile open"; options.pszMainInstruction=title.c_str();
            options.pszContent=L"You can copy settings later through Characters or Settings → Copy from main profile. Credentials and account security choices are never copied.";
            options.pButtons=buttons; options.cButtons=2; options.nDefaultButton=3301;
            int result=IDCANCEL;
            if (SUCCEEDED(dialog(&options,&result,nullptr,nullptr))) return result==3301?1:result==3302?2:0;
        }
        const int answer=MessageBoxW(profileDialogOwner(),L"Copy settings from your main installation before opening this character's login?\n\nYes: choose settings to copy. No: keep existing settings/defaults. Cancel: do not open login.\n\nThis choice is offered once per profile. You can copy later through Characters or Settings.",title.c_str(),MB_YESNOCANCEL|MB_ICONQUESTION);
        return answer==IDYES?1:answer==IDNO?2:0;
    }
    void launch(int index, const ProfileCopyRequest& requested = {})
    {
        if (!validSlot(index) || busy() || closing) { message(L"Finish the current switch or close action before launching."); return; }
        if (slots[index] && slots[index]->running()) { message(L"This slot still has a viewer open. Close that viewer before relaunching."); return; }
        const auto epoch = profileSetupEpoch;
        const auto openingToken = setOpen.active ? setOpen.token : 0;
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
        const auto marker = profile/L"profile-copy-offered-v1.txt";
        bool offered = false;
        const DWORD markerAttributes=GetFileAttributesW(marker.c_str());
        if (markerAttributes != INVALID_FILE_ATTRIBUTES && !(markerAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
        {
            std::ifstream input(marker,std::ios::binary); char bytes[6]{}; input.read(bytes,sizeof(bytes));
            offered = profilePromptWasOffered(std::string(bytes,static_cast<std::size_t>(input.gcount())));
        }
        ProfileCopyRequest copy = requested;
        if (!offered && !copy.groups)
        {
            ProfileSetupScope setup(profileSetupOpen);
            const int choice = firstProfileChoice(index);
            if (!choice || (choice==1 && !selectProfileCopy(index,copy))) return;
        }
        if (busy() || closing || epoch != profileSetupEpoch ||
            (openingToken && !setOpen.owns(openingToken,GetTickCount64())) || (slots[index] && slots[index]->running()))
        { message(L"Profile setup cancelled because the host or character state changed."); return; }
        PipePair pair;
        if (!pair.create(slot->id)) { message(L"Unable to create the private worker connection."); return; }
        if (!slot->pipe.open(pair.server.take())) { message(L"Unable to initialize asynchronous worker control."); return; }
        if (!slot->preview.create(slot->id, GetCurrentProcessId())) { message(L"Unable to create the bounded preview frame lane."); return; }
        auto childEnvironment = environment(profile, pair.client.value, lease.value, slot->preview.mapping(), slot->preview.mutex(), slot->id, copy, pauseAllAssistant);
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
        if (!offered)
        {
            const auto temporary=profile/(L"profile-copy-offered-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp");
            Handle output(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
            DWORD written=0;
            const auto markerSize=static_cast<DWORD>(std::char_traits<char>::length(ProfilePromptMarker));
            bool saved=output.value!=INVALID_HANDLE_VALUE && WriteFile(output.value,ProfilePromptMarker,markerSize,&written,nullptr) && written==markerSize && FlushFileBuffers(output.value);
            if (output.value!=INVALID_HANDLE_VALUE) CloseHandle(output.take());
            saved=saved && MoveFileExW(temporary.c_str(),marker.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
            if (!saved) { DeleteFileW(temporary.c_str()); message(L"Login opened, but the first-open choice could not be saved; the setup prompt may appear again."); }
        }
    }
    bool busy() const
    {
        if (profileSetupOpen || handoff.step() != Handoff::Step::Idle || detaching) return true;
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
        if (response.detail.rfind("Settings import:",0)==0 && response.detail!=slot.lastImportReport)
        {slot.lastImportReport=response.detail;message(wide(response.detail));}
        const bool changedSession = !slot.chat.identity.owns(response);
        if (changedSession) attention.forget(slot.chat.identity);
        slot.chat.bind(response);slot.contacts.bind(response);
        if (changedSession)
        {
            if (slot.restart.phase() != Restart::Phase::Idle && !slot.restart.owns(response))
            { slot.restart.cancel(); message(L"Login changed; restart cancelled without replaying a close on the new session."); }
            slot.soundPrepared = false; slot.autoReadAt = 0; slot.hasPendingSend = slot.hasPendingTyping = false; slot.catalogIndex = 0; slot.catalogAt = slot.eventsAt = 0;
            slot.assistant={};slot.assistantAt=slot.assistantSince=0;slot.assistantDiagnostic.clear();slot.assistantPauseFailed=false;
            slot.alertReasons={};slot.alertAt={};slot.latestChatEvent=0;
            slot.hasPendingAction = false; slot.hasPendingReview = false; slot.workspace.clear(); slot.workspaceModified = slot.preferencesOpen = false; slot.workspaceAt = 0;
            slot.previewWidth = slot.previewHeight = slot.previewRate = 0; slot.shortcutPolicy = -1;slot.audioPolicy=0;
            if (chatIndex == index) { conversation.clear(); refreshChat(true, true); }
        }
        slot.snapshot = response;
        if(response.state!=State::Ready || (response.flags&ChatRestricted) || slot.detached){slot.hasPendingContact=false;slot.contactsAt=slot.friendRefreshAt=0;}
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
        if (response.state == State::Ready && profileKey.valid())
        {
            auto& label=chatPreferences.profileNames[static_cast<std::size_t>(index)];if(label.empty())label=boundedText(appearance(index).alias.empty()?response.name:appearance(index).alias,64);
            const auto asset = characterSound(index).asset;
            if (!slot.soundPrepared || slot.soundAsset != asset)
            { cacheSound(asset); slot.soundAsset = asset; slot.soundPrepared = true; }
        }
        if(request.kind==Kind::Contacts){slot.contacts.accept(request,response);slot.contactsAt=GetTickCount64();if(!request.width && !slot.contacts.friends.loading)slot.friendRefreshAt=GetTickCount64();}
        if(request.kind==Kind::ContactSearch){slot.contactsAt=GetTickCount64();if(request.event==slot.contacts.search.token && slot.contacts.source.owns(request) && (response.flags&Error)){slot.contacts.search.error=response.detail;slot.contacts.search.loading=false;}}
        if(request.kind==Kind::OpenIM && !(response.flags&(Error|ChatRestricted)) && slot.contacts.owns(response))
        {
            const bool opened=slot.chat.reopen(response);
            if(!opened || !(chatPreferences.showMask&conversationCategory(Topic::Private))){message(L"Native IM opened; enable Direct IMs and free conversation capacity to show it in host Chat.");return;}
            const int origin=slot.openIMOrigin;
            if(origin>=0 && origin<MonitorViews && monitorSet.bindings[origin].enabled && monitorSet.bindings[origin].key.owns(response))
            {auto& m=monitors[origin];m.chat.select(monitorSet.bindings[origin],response,slot.chat,response.conversation);refreshMonitorChat(origin);}
            else if(origin<0){saveDraft(true);chatIndex=index;conversation=response.conversation;showChat=true;refreshChat(true,true);layout();if(chatDetached && chatWindow)ShowWindow(chatWindow,SW_SHOWNORMAL);}
            else message(L"IM opened for its original character; its monitor changed. Select it in shared Chat.");
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
            if(request.kind==Kind::Events)slot.latestChatEvent=response.cursor;
            if(response.eventType==EventType::Gap && (response.flags&ConversationRestricted))recordAlert(response,index,L"Conversation restricted");
            if (accepted)
            {
                attention.accept(response); alert(response,index);
                if (validSwitchIntent(response,slot.snapshot,index,active,GetTickCount64(),presentation.shortcuts,busy())) dispatchShortcut(response.unread);
                else if(validShellIntent(response,slot.snapshot,index,active,GetTickCount64(),experience.viewerShellKeys)){if(response.unread==7)recover();else navigateAttention(response.unread==8?1:-1);}
            }
            else if(response.eventType==EventType::Chat && response.event)
                recordAlert(response,index,slot.chat.conversationCount(false)>=ChatView::MaxConversations?L"Native conversation limit reached":L"Shared view rejected duplicate or restricted event");
            if (request.kind == Kind::Events) slot.eventsAt = GetTickCount64();
            else if (response.eventType == EventType::None) { slot.catalogIndex = 0; slot.catalogAt = GetTickCount64(); }
            else ++slot.catalogIndex;
            if (index == chatIndex)
            {
                const bool catalogChanged = response.eventType == EventType::Conversation || response.eventType == EventType::Chat || response.eventType == EventType::Notice || response.eventType == EventType::Gap || response.eventType == EventType::ConversationClosed || response.eventType == EventType::NoticeRemoved;
                refreshChat(catalogChanged);
            }
        }
        if (request.kind == Kind::ShortcutPolicy && !(response.flags & Error)) slot.shortcutPolicy = static_cast<int>(request.unread);
        if(request.kind==Kind::AssistantInfo || request.kind==Kind::AssistantConfigure || request.kind==Kind::AssistantPause || request.kind==Kind::AssistantReview)
        {
            slot.assistantAt=GetTickCount64();
            if(request.kind==Kind::AssistantConfigure)++slot.assistantEditRevision;
            if(request.kind==Kind::AssistantPause && (response.flags&Error))slot.assistantPauseFailed=true;
            if(!(response.flags&Error) && request.kind==Kind::AssistantInfo && request.unread==1)
            {
                const auto text=wide(response.text);bool copied=false;
                if(!text.empty() && OpenClipboard(window))
                {
                    const auto bytes=(text.size()+1)*sizeof(wchar_t);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
                    if(memory){if(auto* data=GlobalLock(memory)){memcpy(data,text.c_str(),bytes);GlobalUnlock(memory);if(EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)){copied=true;memory=nullptr;}}if(memory)GlobalFree(memory);}
                    CloseClipboard();
                }
                slot.assistantDiagnostic=copied?L"MCP configuration copied. Select the exact viewer_session in your assistant.":L"Could not copy MCP configuration.";message(slot.assistantDiagnostic);
            }
            if(!(response.flags&Error) && request.kind==Kind::AssistantInfo && request.unread==2){slot.assistantDiagnostic=wide(response.text);message(slot.assistantDiagnostic);}
            if(!(response.flags&Error))
            {slot.assistant=response;if(response.unread){if(!slot.assistantSince)slot.assistantSince=GetTickCount64();}else slot.assistantSince=0;}
            else {slot.assistantDiagnostic=wide(response.detail);message(L"Local Assistant · "+characterName(index)+L": "+wide(response.detail));}
            if(refreshAssistantEditor)refreshAssistantEditor();
        }
        if (request.kind == Kind::AudioPolicy && !(response.flags & Error)) slot.audioPolicy = request.unread;
        if (request.kind == Kind::MonitorPolicy && !(response.flags & Error))
        { slot.previewWidth = request.width; slot.previewHeight = request.height; slot.previewRate = request.unread; }
        if(request.kind==Kind::CloseChat && slot.chat.identity.owns(request))
        {
            const auto found=slot.chat.conversations.find(request.conversation);
            if(found!=slot.chat.conversations.end() && found->second.closedAt==(std::numeric_limits<std::uint64_t>::max)())
            {if(response.flags&Error){found->second.closed=false;found->second.closedAt=0;}else slot.chat.close(request.conversation,response.cursor);}
            refreshChat(true,true);updateMonitors();refreshAttention();
        }
        if (request.kind == Kind::MarkRead && !(response.flags & (Error|ReadDeferred)))
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
        if (!slots[index]) { descriptions[index] = profileName(index) + L" · not launched"; SetWindowTextW(status[index], descriptions[index].c_str());SetWindowTextW(cardButtons[index],descriptions[index].c_str()); return; }
        const auto& slot = *slots[index];
        std::wostringstream text;
        text << (std::to_wstring(index + 1) + L": ") << characterName(index);
        text << (!slot.running() ? L" · closed" : slot.detached ? L" · separate viewer" : !slot.pipe.alive() ? L" · control lost" : slot.snapshot.state == State::Disconnected ? L" · disconnected" :
            slot.snapshot.state != State::Ready ? L" · connecting" : slot.snapshot.mode == Mode::Warm ? L" · warm" :
                slot.snapshot.mode == Mode::Economy ? L" · economy (experimental)" : L" · active");
        if (experience.diagnostics && (slot.snapshot.flags & EconomyTrimmed)) text << L" · render targets released";
        if (experience.diagnostics && (slot.snapshot.flags & VoiceOwner)) text << L" · voice owner";
        if (!slot.workspace.empty()) text << L" · workspace " << wide(slot.workspace) << (slot.workspaceModified ? L" *" : L"");
        if (slot.preferencesOpen) text << L" · Preferences open";
        std::uint64_t unread = 0; if(experience.accountMask&(1u<<index))for (const auto& entry : slot.chat.conversations) if(!entry.second.restricted && !entry.second.closed && (chatPreferences.attentionMask&conversationCategory(entry.second.topic)))unread += entry.second.unread;
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
        autosaveChoices();
        DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        bool ownForeground = foregroundPid == GetCurrentProcessId();
        for (const auto& slot : slots) if (slot && slot->pid == foregroundPid) ownForeground = true;
        const bool activeReady = validSlot(active) && slots[active] && slots[active]->running() && !slots[active]->detached &&
            slots[active]->snapshot.state == State::Ready && slots[active]->snapshot.mode == Mode::Active && !busy() &&
            !(slots[active]->snapshot.flags & (Promoting|ChatRestricted|Error));
        const HWND surface = activeReady ? slots[active]->surface() : nullptr;
        const bool owner = surface && GetPropW(surface,InputLeaseProperty) == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(GetCurrentProcessId()));
        const bool visibleOwner = surface && IsWindowVisible(surface) && !IsIconic(surface) && !IsIconic(window);
        activeReadClock.update(activeReady ? slots[active]->snapshot : Message{},chatPreferences.activeRead && owner && visibleOwner && ownForeground,GetTickCount64());
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
                else if (slot.snapshot.state == State::Ready && slot.audioPolicy != audioPolicyFor(index))
                    send(index, Kind::AudioPolicy);
                else if (slot.snapshot.state == State::Ready && slot.shortcutPolicy != static_cast<int>(shortcutPolicy()))
                    send(index,Kind::ShortcutPolicy);
                else if(slot.snapshot.state==State::Ready && assistantInfoOwned(slot.assistant,slot.snapshot) &&
                    ((slot.assistant.width&AssistantPaused)!=0)!=pauseAllAssistant && !slot.assistantPauseFailed)
                {auto request=slot.snapshot;request.width=pauseAllAssistant?1u:0u;send(index,Kind::AssistantPause,Mode::Warm,0,&request);}
                else if (slot.hasPendingReview && index == active && !busy())
                {
                    const auto action = slot.pendingReview; slot.hasPendingReview = false;
                    send(index,action.kind,Mode::Warm,0,&action);
                }
                else if (slot.hasPendingAction)
                {
                    const auto action = slot.pendingAction; slot.hasPendingAction = false;
                    if(action.kind==Kind::WorkspaceMenu && index!=active)message(L"Workspace opening was cancelled because its character did not become active.");
                    else if(!send(index, action.kind, Mode::Warm, 0, &action) && action.kind==Kind::CloseChat && slot.chat.identity.owns(action))slot.hasPendingAction=true;
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
                else if(slot.snapshot.state==State::Ready && GetTickCount64()-slot.assistantAt>=1000)send(index,Kind::AssistantInfo);
                else if (pollContacts(index)) {}
                else if (automaticRead(index)) {}
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
        tickCycler();refreshFriends();
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
        ++profileSetupEpoch;
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
        std::vector<HWND> result{chatNewIM,chatFriends,chatDock,chatAccent,chatAccount,chatConversation,chatReview,chatRead,chatLatest,chatLabel,chatHistory,chatCompose,chatSend,voiceControl,muteControl,pinMenu};
        // The sidebar is created on demand; an absent window cannot be reparented.
        if (sharedFriends.window) result.insert(result.begin(), sharedFriends.window);
        for (auto button : pinButtons) result.push_back(button);
        return result;
    }
    std::vector<HWND> characterControls() const
    {
        std::vector<HWND> result{densityToggle,controlsDock,quickDivider,rightDivider,quickOverflow,mainOverflow,characterControl, launchButton, embedControl, detachControl, showChatControl, optionsControl, compactInfo,viewMenu,settingsMenu,attentionButton,nextButton,restartButton};
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
        for (const auto& entry : moved)tooltips.reparent(entry.first,theme);
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
        else for (HWND child : children) fs_host_ui::setNativeFont(child,font);
        if (newPanel) { installPanelMenu(panel);CreateWindowExW(0,L"BUTTON",L"Pin",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,50,24,panel,reinterpret_cast<HMENU>(static_cast<INT_PTR>(PanelPin)),GetModuleHandleW(nullptr),nullptr); restorePlacement(panel,chat ? 1u : 2u,chat ? chatDpi : controlsDpi); }
        if(newPanel && !chat){if(chrome==1){chrome=0;setChrome(1);}else setChrome(chrome);}
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
        const UINT local=parent==window?dpi:controlsDpi;const int row=experience.density?38:30;
        const bool needsRestart=slots[lifecycle.selection()] && (slots[lifecycle.selection()]->snapshot.state==State::Disconnected || !slots[lifecycle.selection()]->pipe.alive() || slots[lifecycle.selection()]->restart.phase()!=Restart::Phase::Idle);
        const int rows=1+(parent!=window && !experience.quick.empty()?1:0)+(!condensed && experience.cards?MaxCharacters:0)+(!condensed && experience.diagnostics?MaxCharacters:0)+(needsRestart?1:0);
        return theme.scale(rows*row,local);
    }
    ToolbarRange controlsArea(HWND parent) const
    {
        RECT client{};GetClientRect(parent,&client);const int width=static_cast<int>(client.right);
        if(parent!=window)return {0,width};
        struct Coverage {RECT rect;std::vector<ToolbarRange> monitors;};
        MapWindowPoints(parent,nullptr,reinterpret_cast<POINT*>(&client),2);Coverage coverage{client,{}};
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT monitor,LPARAM data)->BOOL
        {
            auto& c=*reinterpret_cast<Coverage*>(data);
            const LONG overlap=(std::min)(c.rect.bottom,monitor->bottom)-(std::max)(c.rect.top,monitor->top);
            if(overlap>0 && overlap*3>=(std::min)(c.rect.bottom-c.rect.top,monitor->bottom-monitor->top))
                c.monitors.push_back({static_cast<int>(monitor->left),static_cast<int>(monitor->right-monitor->left)});
            return TRUE;
        },reinterpret_cast<LPARAM>(&coverage));
        return dockedToolbarRange(width,static_cast<DockedArea>(experience.dockedArea),static_cast<int>(client.left),coverage.monitors);
    }
    void layoutCharacters(HWND parent,bool condensed,bool visible)
    {
        const UINT local=parent==window?dpi:controlsDpi;const auto area=controlsArea(parent);
        const auto scale=[this,local](int n){return theme.scale(n,local);};const int width=MulDiv(area.width,96,theme.uiDpi(local));
        const int row=experience.density?38:30,h=row-6;
        std::vector<HWND> shown;
        const auto show=[&](HWND child){shown.push_back(child);if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))ShowWindow(child,SW_SHOWNOACTIVATE);};
        const int faceHeight=(std::max)(scale(h),fs_host_ui::comboFaceHeight(characterControl,scale(h)));
        const auto position=[&](HWND child,int x,int y,int w,int height,int expectedHeight)
        {RECT old{};GetWindowRect(child,&old);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&old),2);if(old.left!=x || old.top!=y || old.right-old.left!=w || old.bottom-old.top!=expectedHeight)MoveWindow(child,x,y,w,height,TRUE);show(child);};
        const auto move=[&](HWND child,int x,int y,int w){position(child,area.x+scale(x),scale(y),(std::max)(1,scale(w)),faceHeight,faceHeight);};
        const auto name=[&](HWND child,const wchar_t* label){wchar_t old[256]{};GetWindowTextW(child,old,256);if(wcscmp(old,label))SetWindowTextW(child,label);};
        const auto icon=[&](HWND child,int x,int y,fs_host_ui::Icon glyph,const wchar_t* label,const wchar_t* tip)
        {name(child,label);fs_host_ui::setButtonIcon(child,glyph);move(child,x,y,28);addTooltip(child,tip);};
        if(visible)
        {
            const bool detached=parent!=window;const auto groups=toolbarGroups(width,!experience.quick.empty()&&!detached,detached);
            position(characterControl,area.x+scale(4),scale(3),(std::max)(1,scale(groups.selector)),scale(220),faceHeight);
            addTooltip(characterControl,(L"Select character: "+characterName(lifecycle.selection())+L". Right-click for actions.").c_str());
            SendMessageW(characterControl,CB_SETDROPPEDWIDTH,scale((std::max)(180,groups.selector)),0);
            const HWND mainButtons[]={launchButton,showChatControl,viewMenu,settingsMenu};
            const unsigned int mainCommands[]={LaunchNext,ShowChat,MonitorsMenu,SettingsActions};
            const fs_host_ui::Icon mainIcons[]={fs_host_ui::Icon::UserPlus,fs_host_ui::Icon::MessagesSquare,fs_host_ui::Icon::Cctv,fs_host_ui::Icon::Settings};
            const wchar_t* mainNames[]={L"Login",L"Chat",L"Monitors",L"Preferences"};
            const wchar_t* mainTips[]={L"Open the next available login; right-click to choose a character",L"Show or hide shared Chat",L"Open or close character monitors",L"Host Preferences"};
            int x=4+groups.selector+4;overflowMain.clear();
            for(int i=0;i<4;++i){if(i<groups.mainCount){icon(mainButtons[i],x,3,mainIcons[i],mainNames[i],mainTips[i]);x+=32;}else overflowMain.push_back(mainCommands[i]);}
            setToggle(showChatControl,showChat);
            if(groups.mainOverflow)icon(mainOverflow,x,3,fs_host_ui::Icon::Ellipsis,L"More controls",L"More character controls");
            int right=groups.rightStart;
            if(detached){move(GetDlgItem(parent,PanelPin),right,3,28);right+=32;}
            icon(controlsDock,right,3,controlsDetached?fs_host_ui::Icon::ArrowDownLeft:fs_host_ui::Icon::ExternalLink,
                controlsDetached?L"Dock character controls":L"Pop out character controls",controlsDetached?L"Dock character controls":L"Pop out character controls");right+=32;
            const bool requestedCondensed=chrome==1;
            icon(densityToggle,right,3,requestedCondensed?fs_host_ui::Icon::ChevronDown:fs_host_ui::Icon::ChevronUp,
                requestedCondensed?L"Expand character controls":L"Condense character controls",
                requestedCondensed?L"Expand character controls":condensed?L"Condense character controls. Currently compacted to fit; enlarge the window to show cards.":L"Condense character controls");
            int y=row;const int available=detached?(std::max)(0,width-8):groups.centerAvailable;
            const int quickY=detached?y+3:3;if(detached&&!experience.quick.empty())y+=row;
            std::vector<QuickButtonSize> sizes;std::vector<std::wstring> labels;
            const HDC dc=GetDC(parent);const auto previous=dc?SelectObject(dc,parent==window?font:controlsFont):nullptr;
            const auto textWidth=[&](const std::wstring& label){SIZE size{};if(dc)GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&size);return (std::max)(28,MulDiv(static_cast<int>(size.cx),96,theme.uiDpi(local))+14);};
            for(const auto command:experience.quick)
            {
                const auto badge=std::to_wstring(attentionCount);labels.push_back(command==274?L"Attention · "+badge:quickLabel(command));
                const int compact=command==274?textWidth(badge):28;
                sizes.push_back({experience.labels==2?compact:textWidth(labels.back())+(experience.labels==0?24:0),compact});
            }
            if(dc){if(previous)SelectObject(dc,previous);ReleaseDC(parent,dc);}
            const auto fit=quickToolbarLayout(available,sizes);int quickX=detached?(width-fit.used)/2:centeredQuickStart(width,groups,fit.used);
            const int first=quickX;overflowActions.clear();
            for(std::size_t i=0;i<experience.quick.size();++i)
            {
                const auto command=experience.quick[i];if(i>=fit.visible){overflowActions.push_back(command);continue;}
                const bool compact=experience.labels==2 || fit.widths[i]<sizes[i].full;
                name(quickButtons[i],(compact&&command==274?std::to_wstring(attentionCount):labels[i]).c_str());
                fs_host_ui::setButtonIcon(quickButtons[i],compact?(command==274?fs_host_ui::Icon::None:fs_host_ui::commandIcon(command)):experience.labels==0?fs_host_ui::commandIcon(command):fs_host_ui::Icon::None,!compact&&experience.labels==0);
                move(quickButtons[i],quickX,quickY,fit.widths[i]);quickX+=fit.widths[i]+4;
                if(const auto* commandInfo=shellCommand(command))addTooltip(quickButtons[i],commandInfo->tip);
                setToggle(quickButtons[i],command==274&&attentionWindow&&IsWindowVisible(attentionWindow));
            }
            if(fit.overflow&&available>0){fs_host_ui::setButtonIcon(quickOverflow,fs_host_ui::Icon::Ellipsis);move(quickOverflow,quickX,quickY,(std::min)(28,available));}
            if(!detached&&fit.used>0)
            {
                if(first-groups.leftEnd<12)move(quickDivider,(first+groups.leftEnd)/2-2,3,4);
                if(groups.rightStart-first-fit.used<12)move(rightDivider,(groups.rightStart+first+fit.used)/2-2,3,4);
            }
            const int selected=lifecycle.selection();const bool needsRestart=slots[selected]&&(slots[selected]->snapshot.state==State::Disconnected||!slots[selected]->pipe.alive()||slots[selected]->restart.phase()!=Restart::Phase::Idle);
            if(needsRestart){move(restartButton,4,y,160);name(restartButton,slots[selected]->restart.phase()==Restart::Phase::Idle?L"Restart character":L"Cancel restart");fs_host_ui::setButtonIcon(restartButton,fs_host_ui::Icon::RotateCw,true);addTooltip(restartButton,L"Restart this character or cancel its pending restart");y+=row;}
            if(!condensed&&experience.cards)for(unsigned int position=0;position<MaxCharacters;++position){const int i=static_cast<int>(experience.order[position]);move(cardButtons[i],4,y,width-48);move(cardActions[i],width-40,y,34);name(cardActions[i],L"Character actions");fs_host_ui::setButtonIcon(cardActions[i],fs_host_ui::Icon::Ellipsis);addTooltip(cardButtons[i],(L"Select "+characterName(i)).c_str());addTooltip(cardActions[i],(L"Actions for "+characterName(i)).c_str());y+=row;}
            if(!condensed&&experience.diagnostics)for(int i=0;i<MaxCharacters;++i){move(status[i],4,y,width-8);y+=row;}
        }
        for(auto child:characterControls())if(std::find(shown.begin(),shown.end(),child)==shown.end()&&(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))ShowWindow(child,SW_HIDE);
    }
    void setToggle(HWND child,bool selected)
    {
        const bool prior=GetPropW(child,L"FolderstormToggle")!=nullptr;if(prior==selected)return;
        if(selected)SetPropW(child,L"FolderstormToggle",reinterpret_cast<HANDLE>(1));else RemovePropW(child,L"FolderstormToggle");InvalidateRect(child,nullptr,FALSE);
    }
    void layoutChat(HWND parent,int top,bool visible,int bottomInset=0)
    {
        const UINT paneDpi=parent==window?dpi:chatDpi;if(sharedFriends.window && sharedFriends.dpi!=paneDpi){sharedFriends.dpi=paneDpi;friendsFont(sharedFriends);}
        RECT r{};GetClientRect(parent,&r);const int fullWidth=static_cast<int>(r.right),height=static_cast<int>(r.bottom)-bottomInset;const UINT local=parent==window?dpi:chatDpi;
        const auto scale=[this,local](int n){return theme.scale(n,local);};const int side=showFriends?(std::min)(scale(280),fullWidth/2):0,width=fullWidth-side;const bool narrow=width<scale(650);
        if(sharedFriends.window){ShowWindow(sharedFriends.window,visible && showFriends?SW_SHOWNOACTIVATE:SW_HIDE);MoveWindow(sharedFriends.window,width,top,side,(std::max)(1,height-top),TRUE);layoutFriends(sharedFriends);}
        for(auto child:chatControls())ShowWindow(child,visible?SW_SHOWNOACTIVATE:SW_HIDE);
        if(sharedFriends.window)ShowWindow(sharedFriends.window,visible && showFriends?SW_SHOWNOACTIVATE:SW_HIDE);
        std::vector<std::size_t> visiblePins;for(std::size_t i=0;i<pins.entries.size() && i<6;++i)if(shownPin(pins.entries[i]))visiblePins.push_back(i);
        for(std::size_t i=0;i<6;++i)ShowWindow(pinButtons[i],SW_HIDE);
        for(std::size_t position=0;position<visiblePins.size();++position)
        {const auto i=visiblePins[position];MoveWindow(pinButtons[i],scale(6)+static_cast<int>(position)*((width-scale(12))/6),top,(std::max)(1,(width-scale(12))/6-scale(3)),scale(25),TRUE);ShowWindow(pinButtons[i],visible?SW_SHOWNOACTIVATE:SW_HIDE);}
        if(!visiblePins.empty())top+=scale(28);
        const auto move=[&](HWND child,int x,int y,int w,int h){MoveWindow(child,x,y,(std::max)(1,w),h,TRUE);};
        const int inset=scale(6);const int usable=width-inset*2;
        move(chatAccount,inset,top,narrow?usable:scale(180),scale(150));
        move(chatConversation,narrow?inset:scale(196),top+(narrow?scale(28):0),narrow?usable:(std::max)(scale(80),usable-scale(196)),scale(200));
        top+=scale(narrow?56:28);
        const int reviewWidth=scale(narrow?84:120),readWidth=scale(narrow?44:60),pinWidth=scale(narrow?44:64);
        move(chatReview,inset,top,reviewWidth,scale(24));move(chatRead,inset+reviewWidth+scale(6),top,readWidth,scale(24));move(pinMenu,inset+reviewWidth+readWidth+scale(12),top,pinWidth,scale(24));
        SetWindowTextW(chatReview,narrow?L"View":L"Review in viewer");addTooltip(chatReview,L"Review the selected conversation in its native viewer");
        move(chatDock,width-scale(parent!=window?78:40),top,scale(32),scale(24));SetWindowTextW(chatDock,chatDetached?L"Dock shared Chat":L"Pop out shared Chat");fs_host_ui::setButtonIcon(chatDock,chatDetached?fs_host_ui::Icon::ArrowDownLeft:fs_host_ui::Icon::ExternalLink);addTooltip(chatDock,chatDetached?L"Dock shared Chat":L"Pop out shared Chat");
        if(parent!=window)move(GetDlgItem(parent,PanelPin),width-scale(44),top,scale(32),scale(24));
        top+=scale(28);move(chatNewIM,inset,top,scale(70),scale(24));move(chatFriends,inset+scale(76),top,scale(76),scale(24));setToggle(chatFriends,showFriends);top+=scale(28);move(chatAccent,inset,top,scale(5),scale(20));move(chatLabel,scale(18),top,(std::max)(1,width-scale(90)),scale(20));move(chatLatest,width-scale(66),top,scale(60),scale(20));top+=scale(24);
        const int input=(std::max)(top+scale(50),height-scale(42));
        move(chatHistory,inset,top,usable,(std::max)(1,input-top-scale(6)));transcript.latestVisibility();
        move(chatCompose,inset,input,usable-scale(70),scale(30));move(chatSend,width-scale(70),input,scale(64),scale(30));
        // Audio policies live in Settings, eliminating a permanently occupied footer.
        ShowWindow(voiceControl,SW_HIDE);ShowWindow(muteControl,SW_HIDE);
    }
    void layout()
    {
        RECT rect{}; GetClientRect(window, &rect);
        const int width = static_cast<int>(rect.right), height = static_cast<int>(rect.bottom);
        const bool collapsed = chrome == 2;
        // Optional rows yield to the world/composer; quick actions remain reachable in overflow.
        const bool condensed=chrome==1 || (!controlsDetached && characterHeight(window,false)+scaled(showChat && !chatDetached?402:124)>height);
        RECT controlsRect{};if(controlsWindow)GetClientRect(controlsWindow,&controlsRect);
        const bool controlsCondensed=controlsDetached && (chrome==1 || characterHeight(controlsWindow,false)>controlsRect.bottom);
        layoutCharacters(controlsDetached ? controlsWindow : window, controlsDetached ? controlsCondensed : condensed, controlsDetached || !collapsed);
        const int top = collapsed ? scaled(revealHeight(chrome,experience.density!=0)) : controlsDetached ? 0 : characterHeight(window,condensed);
        ShowWindow(revealControl,collapsed ? SW_SHOWNOACTIVATE : SW_HIDE);
        MoveWindow(revealControl,0,0,width,scaled(revealHeight(chrome,experience.density!=0)),TRUE);
        const bool dockChat = showChat && !chatDetached && !collapsed;
        const int footerHeight = chrome != 2 ? scaled(24) : 0;
        const int chatTop = dockChat ? (std::max)(top + scaled(100), height - footerHeight - scaled(pins.entries.empty() ? 278 : 306)) : height - footerHeight;
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
        tooltips.set(child,text,theme);
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
        if (message == WM_CLOSE) { ShowWindow(window,SW_HIDE);host->layout();return 0; }
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
        if(child==host->characterControl || child==host->launchButton || (id>=QuickBase && id<QuickBase+12) || (id>=CardBase && id<CardBase+MaxCharacters))return SendMessageW(host->window,message,wparam,lparam);
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
        int minimumHeight=host->theme.scale(chat?300:40,dpi);
        if(!chat)minimumHeight=(std::max)(80,host->characterHeight(window,host->chrome==1)+host->theme.scale(40,dpi));
        MONITORINFO work{};work.cbSize=sizeof(work);
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&work))minimumHeight=(std::min)(minimumHeight,static_cast<int>(work.rcWork.bottom-work.rcWork.top));
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {
            host->theme.scale(360,dpi),
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
LRESULT CALLBACK friendsListProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
    if(message==WM_NCDESTROY)
    {
        RemoveWindowSubclass(window,friendsListProc,id);
        return DefSubclassProc(window,message,wp,lp);
    }
    auto* pane=reinterpret_cast<fs_host_ui::FriendsPane*>(data);
    // LBS_HASSTRINGS retains accessible names, but prevents WM_CHARTOITEM.
    // Consume printable input here so native search cannot select a heading.
    if(message==WM_CHAR && wp>=L' ' && host && pane && pane->list==window)
    {
        if(!pane->updating && IsWindowEnabled(window))
        {
            const auto current=static_cast<int>(SendMessageW(window,LB_GETCURSEL,0,0));
            const auto row=host->friendsCharacter(*pane,static_cast<wchar_t>(wp),current);
            if(row>=0)SendMessageW(window,LB_SETCURSEL,static_cast<WPARAM>(row),0);
            // LB_SETCURSEL does not send LBN_SELCHANGE; refresh Open IM explicitly.
            host->refreshFriendsPane(*pane);
        }
        return 0;
    }
    return DefSubclassProc(window,message,wp,lp);
}
LRESULT CALLBACK friendsProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_NCCREATE){const auto* create=reinterpret_cast<const CREATESTRUCTW*>(lp);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));}
    auto* pane=reinterpret_cast<fs_host_ui::FriendsPane*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(!host || !pane)return DefWindowProcW(window,message,wp,lp);
    switch(message)
    {
    case WM_COMMAND:host->friendsCommand(*pane,LOWORD(wp),HIWORD(wp));return 0;
    case WM_SIZE:host->layoutFriends(*pane);return 0;
    case WM_DPICHANGED:
    {
        pane->dpi=(std::max)(96u,static_cast<UINT>(LOWORD(wp)));host->friendsFont(*pane);
        if(pane->standalone){const auto* r=reinterpret_cast<const RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}
        host->layoutFriends(*pane);return 0;
    }
    case WM_GETMINMAXINFO:if(pane->standalone){reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={host->theme.scale(280,pane->dpi),host->theme.scale(260,pane->dpi)};return 0;}break;
    case WM_SYSCOMMAND:
        if((wp&0xfff0u)==0xA160){host->pinWindow(window);return 0;}
        if((wp&0xfff0u)==0xA140 || (wp&0xfff0u)==0xA150){host->recover((wp&0xfff0u)==0xA150);return 0;}break;
    case WM_MEASUREITEM:
        if(reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlID==ContactList){reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=static_cast<UINT>(host->theme.scale(24,pane->dpi));return TRUE;}break;
    case WM_DRAWITEM:if(host->drawFriend(*pane,*reinterpret_cast<DRAWITEMSTRUCT*>(lp)) || host->theme.button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;break;
    case WM_VKEYTOITEM:
        if(reinterpret_cast<HWND>(lp)==pane->list)return host->friendsKey(*pane,LOWORD(wp),static_cast<short>(HIWORD(wp)));break;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return host->theme.color(message,wp,lp);
    case WM_CTLCOLORSTATIC:
    {
        const auto brush=host->theme.color(message,wp,lp);
        if(reinterpret_cast<HWND>(lp)==pane->label && host->contactsLive(pane->slot))SetTextColor(reinterpret_cast<HDC>(wp),host->accountColor(pane->slot));return brush;
    }
    case WM_ERASEBKGND:{RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wp),&r,host->theme.backBrush);return 1;}
    case WM_CLOSE:ShowWindow(window,SW_HIDE);return 0;
    case WM_NCDESTROY:pane->window=nullptr;break;
    default:break;
    }
    return DefWindowProcW(window,message,wp,lp);
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
    for (int i = 0; i < MonitorViews; ++i) if (&host->monitors[i] == monitor) { id = i; break; }
    if (id < 0) return DefWindowProcW(window, message, wparam, lparam);
    if(message==WM_SYSCOMMAND){const auto command=wparam&0xfff0u;if(command==0xA140 || command==0xA150){host->recover(command==0xA150);return 0;}if(command==0xA160){host->pinWindow(window);return 0;}}
    const auto& binding = host->monitorSet.bindings[id];
    auto scale = [monitor,context=host](int value) { return context->theme.scale(value,monitor->dpi); };
    switch (message)
    {
    case WM_COMMAND:
        if(LOWORD(wparam)>=CyclePlay && LOWORD(wparam)<=CycleInterval){if(LOWORD(wparam)!=CycleInterval || HIWORD(wparam)==EN_KILLFOCUS)host->cycleCommand(LOWORD(wparam));return 0;}
        if(LOWORD(wparam)==PanelPin){host->pinWindow(window);return 0;}
        if(LOWORD(wparam)==MonitorChatToggle){host->setMonitorChat(id,!monitor->showChat);return 0;}
        if(LOWORD(wparam)==MonitorOptions){host->monitorMenu(id);return 0;}
        if(LOWORD(wparam)==MonitorExchange)
        {if(id==CyclingView)host->cycler.pause();const auto* slot=host->monitorChatSlot(id);if(slot && !(slot->snapshot.flags&ChatRestricted) && binding.enabled){SetForegroundWindow(host->window);host->switchTo(binding.slot);}return 0;}
        host->monitorChatCommand(id,LOWORD(wparam),HIWORD(wparam));return 0;
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight=static_cast<UINT>(host->theme.scale(22,monitor->dpi));return TRUE;
    case WM_DRAWITEM: if(host->theme.button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)) || host->theme.drawCombo(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))return TRUE;break;
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
        if(!host->tilingMonitors)SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        host->layoutMonitorChat(id); return 0;
    }
    case WM_GETMINMAXINFO:
    {
        const auto minimum=host->monitorMinimumFor(id,monitor->dpi);
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize={minimum.width,minimum.height};return 0;
    }
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
        const UINT local=(item->CtlID==ChatAccount || item->CtlID==ChatConversation) && host->chatDetached?host->chatDpi:item->CtlID==ActiveCharacter && host->controlsDetached?host->controlsDpi:item->CtlID==AttentionList?host->attentionDpi:host->dpi;
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
        host->showChatControl = control(window, L"BUTTON", L"Chat", BS_PUSHBUTTON | WS_TABSTOP, 695, 10, 90, 28, ShowChat);
        SendMessageW(host->showChatControl, BM_SETCHECK, host->showChat ? BST_CHECKED : BST_UNCHECKED, 0);
        host->embedControl = control(window, L"BUTTON", L"Host active viewer", BS_AUTOCHECKBOX | WS_TABSTOP, 383, 10, 235, 28, HostSurface);
        host->detachControl = control(window, L"BUTTON", L"Separate windows", BS_PUSHBUTTON | WS_TABSTOP, 630, 10, 125, 28, DetachAll);
        host->viewMenu=control(window,L"BUTTON",L"Monitors",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,MonitorsMenu);
        host->settingsMenu=control(window,L"BUTTON",L"Preferences",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,SettingsActions);
        host->attentionButton=control(window,L"BUTTON",L"Attention",BS_PUSHBUTTON|WS_TABSTOP,0,0,80,28,274);
        host->nextButton=control(window,L"BUTTON",L"Next",BS_PUSHBUTTON|WS_TABSTOP,0,0,45,28,NextAttention);
        host->densityToggle=control(window,L"BUTTON",L"↑",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,DensityToggle);
        host->controlsDock=control(window,L"BUTTON",L"↗",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,ControlsDock);
        host->chatDock=control(window,L"BUTTON",L"↗",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,ChatDock);
        host->quickDivider=control(window,L"STATIC",L"|",SS_CENTER,0,0,4,28);
        host->rightDivider=control(window,L"STATIC",L"|",SS_CENTER,0,0,4,28);
        host->quickOverflow=control(window,L"BUTTON",L"…",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,QuickOverflow);
        host->mainOverflow=control(window,L"BUTTON",L"More controls",BS_PUSHBUTTON|WS_TABSTOP,0,0,28,28,MainOverflow);
        host->restartButton=control(window,L"BUTTON",L"Restart",BS_PUSHBUTTON|WS_TABSTOP,0,0,70,28,240);
        for(int i=0;i<MaxCharacters;++i){host->cardButtons[i]=control(window,L"BUTTON",L"Character",BS_PUSHBUTTON|WS_TABSTOP,0,0,200,30,CardBase+i);host->cardActions[i]=control(window,L"BUTTON",L"…",BS_PUSHBUTTON|WS_TABSTOP,0,0,30,30,CardActionBase+i);}
        for(int i=0;i<12;++i)host->quickButtons[i]=control(window,L"BUTTON",L"",BS_PUSHBUTTON|WS_TABSTOP,0,0,50,28,QuickBase+i);
        host->revealControl = control(window,L"BUTTON",L"Show controls",BS_PUSHBUTTON|WS_TABSTOP,0,0,200,24,RestoreControls);
        host->optionsControl = control(window, L"BUTTON", L"Characters", BS_PUSHBUTTON | WS_TABSTOP, 705, 10, 80, 28, SessionOptions);
        SendMessageW(host->embedControl, BM_SETCHECK, host->embedding ? BST_CHECKED : BST_UNCHECKED, 0);
        for (int i = 0; i < MaxCharacters; ++i)
        {
            const auto label = host->profileName(i) + L" · not launched";
            host->status[i] = control(window, L"STATIC", label.c_str(), SS_OWNERDRAW, 10, 48 + i * 24, 850, 20);
            host->standbyControl[i] = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 760, 46 + i * 24, 190, 130, StandbyBase + i);
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Background: Warm"));
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Economy (experimental)"));
            SendMessageW(host->standbyControl[i], CB_SETCURSEL, host->standby[i] == Mode::Economy ? 1 : 0, 0);
        }
        host->compactInfo = control(window, L"STATIC", L"Character status in Session menu", SS_LEFT, 536, 5, 250, 20);
        host->viewport = control(window, L"STATIC", L"", SS_BLACKRECT | SS_NOTIFY | WS_CLIPCHILDREN, 0, 99, 880, 490, ViewportFocus);
        host->notice = control(window, L"STATIC", L"Choose a character or open a login to begin.", SS_LEFT|SS_ENDELLIPSIS|SS_NOPREFIX, 10, 600, 850, 20);
        host->pinMenu = control(window,L"BUTTON",L"Chat…",BS_PUSHBUTTON|WS_TABSTOP,810,400,80,24,PinActions);
        for (int i = 0; i < 6; ++i) host->pinButtons[i] = control(window,L"BUTTON",L"",BS_OWNERDRAW|WS_TABSTOP,10+i*130,400,125,25,PinBase+i);
        host->chatAccount = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 10, 400, 180, 150, ChatAccount);
        host->chatConversation = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, 200, 400, 400, 200, ChatConversation);
        host->chatHistory = control(window, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 10, 452, 850, 88);
        host->chatCompose = control(window, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER | WS_TABSTOP, 10, 545, 800, 35, ChatCompose);
        SendMessageW(host->chatCompose, EM_SETLIMITTEXT, 1023, 0);
        host->chatSend = control(window, L"BUTTON", L"Send", BS_PUSHBUTTON | WS_TABSTOP, 820, 545, 75, 35, ChatSend);
        host->chatReview = control(window, L"BUTTON", L"Review in viewer", BS_PUSHBUTTON | WS_TABSTOP, 620, 400, 140, 24, ChatReview);
        host->chatNewIM=control(window,L"BUTTON",L"New IM",BS_PUSHBUTTON|WS_TABSTOP,0,0,70,24,ChatNewIM);host->chatFriends=control(window,L"BUTTON",L"Friends",BS_PUSHBUTTON|WS_TABSTOP,0,0,76,24,ChatFriends);
        host->addTooltip(host->chatNewIM,L"Choose a friend or search for a resident as the selected sending character");host->addTooltip(host->chatFriends,L"Show or hide the selected character’s friends sidebar");
        host->chatRead = control(window, L"BUTTON", L"Mark read", BS_PUSHBUTTON | WS_TABSTOP, 770, 400, 90, 24, ChatRead);
        host->chatLatest = control(window,L"BUTTON",L"Latest",BS_PUSHBUTTON|WS_TABSTOP,0,0,60,24,ChatLatest);
        SendMessageW(host->chatHistory,EM_SETLIMITTEXT,2*1024*1024,0);
        host->transcript.attach(host->chatHistory,host->chatLatest);
        host->editor.attach(host->chatCompose,[] { if (host) host->sendChat(); });
        host->addTooltip(host->chatLatest,L"Jump to the latest message and resume following chat");
        host->chatAccent = control(window,L"STATIC",L"",SS_OWNERDRAW,10,429,5,20,ChatAccent);
        host->chatLabel = control(window, L"STATIC", L"Send as: not logged in", SS_LEFT|SS_ENDELLIPSIS|SS_NOPREFIX, 10, 429, 850, 20);
        host->voiceControl = control(window, L"BUTTON", L"Voice follows active character", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 585, 255, 24, VoicePolicy);
        host->muteControl = control(window, L"BUTTON", L"Mute background world/UI/media sound", BS_AUTOCHECKBOX | WS_TABSTOP, 280, 585, 280, 24, BackgroundMute);
        SendMessageW(host->voiceControl, BM_SETCHECK, host->voice ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(host->muteControl, BM_SETCHECK, host->muteBackground ? BST_CHECKED : BST_UNCHECKED, 0);
        host->refreshChat(true);
        host->refreshCharacters(); host->updateDpi(96);
        HMENU system = GetSystemMenu(window, FALSE);
        AppendMenuW(system, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(system, MF_STRING, 0xA100, L"Expanded character controls");
        AppendMenuW(system, MF_STRING, 0xA110, L"Condensed character controls");


        host->recoveryMenu(window); host->tray(true);
        const bool chatDetached = host->chatDetached, controlsDetached = host->controlsDetached;
        host->chatDetached = host->controlsDetached = false;
        if(!host->revealControl && host->chrome==2)host->chrome=1;
        host->setChrome(host->chrome);
        host->addTooltip(host->launchButton,L"Open the next available login; right-click to choose a character");
        host->addTooltip(host->showChatControl,L"Show or hide shared Chat");host->addTooltip(host->viewMenu,L"Open or close character monitors");host->addTooltip(host->settingsMenu,L"Host Preferences");host->addTooltip(host->quickOverflow,L"More quick actions");
        host->addTooltip(host->embedControl, L"Fit the active viewer to this host; uncheck to return to a separate managed window");
        host->addTooltip(host->detachControl, L"Use separate viewer windows while retaining character management");
        host->addTooltip(host->optionsControl, L"Character actions, monitors, host presentation and docking");
        host->addTooltip(host->mainOverflow,L"More character controls");host->addTooltip(host->characterControl,L"Select a character; right-click for its actions");
        host->addTooltip(host->chatReview,L"Review the selected conversation in its native viewer");host->addTooltip(host->chatRead,L"Mark this conversation read");
        host->addTooltip(host->chatSend,L"Send as the account shown above");host->addTooltip(host->pinMenu,L"Pinned conversations");
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
        if(child==host->launchButton){host->sessionMenu(4);return 0;}
        if((id>=QuickBase && id<QuickBase+12) && static_cast<std::size_t>(id-QuickBase)<host->experience.quick.size() && host->experience.quick[static_cast<std::size_t>(id-QuickBase)]==274){host->sessionMenu(5);return 0;}
        if((id>=CardBase && id<CardBase+MaxCharacters) || child==host->characterControl)
        {const int selected=child==host->characterControl?host->selectedSlot(child):id-CardBase;host->lifecycle.observeSelection(selected);SendMessageW(host->characterControl,CB_SETCURSEL,selected,0);host->sessionMenu(1,0,selected);return 0;}
        break;
    }
    case WM_DISPLAYCHANGE: host->refitWindows(); return 0;
    case WM_SETTINGCHANGE:
        host->applyTheme();host->updateDpi(host->dpi);
        for(HWND panel:{host->chatWindow,host->controlsWindow,host->attentionWindow})if(panel)host->panelFont(panel,panel==host->chatWindow?host->chatDpi:panel==host->controlsWindow?host->controlsDpi:host->attentionDpi);
        for(int i=0;i<MonitorViews;++i)if(host->monitors[i].window){host->monitorFont(i);host->layoutMonitorChat(i);}
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
    case WM_MOVE:
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
        if(LOWORD(wparam)==MonitorsMenu){host->monitorsMenu();return 0;}if(LOWORD(wparam)==SettingsActions){host->editPreferences();return 0;}
        if(LOWORD(wparam)==MainOverflow){host->mainOverflowMenu();return 0;}
        if(LOWORD(wparam)==DensityToggle){host->setChrome(host->chrome==1?0:1);return 0;}
        if(LOWORD(wparam)==ControlsDock){host->setPanelDetached(false,!host->controlsDetached);return 0;}
        if(LOWORD(wparam)==ChatDock){host->setPanelDetached(true,!host->chatDetached);return 0;}
        if(LOWORD(wparam)==QuickOverflow){host->quickOverflowMenu();return 0;}
        if(LOWORD(wparam)==274 && reinterpret_cast<HWND>(lparam)==host->attentionButton){host->toggleAttention();return 0;}
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
        case LaunchNext: host->launchNext(); break;
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
            host->toggleChat();break;
        case ChatAccount:
            if (!host->updatingChat)
            {
                std::vector<std::string> ids; for (int i=0;i<MaxCharacters;++i) ids.push_back(std::to_string(i));
                const auto selected = choiceNotification(host->chatAccount,host->accountChoice,HIWORD(wparam),ids,std::to_string(host->chatIndex));
                if (selected)
                { host->saveDraft(true); host->chatIndex = std::stoi(*selected); host->conversation.clear(); host->refreshChat(true,true); }
            }
            break;
        case ChatConversation:
            if (!host->updatingChat)
            {
                const auto selected = choiceNotification(host->chatConversation,host->conversationChoice,HIWORD(wparam),host->conversationIds,host->conversation);
                if (selected && host->slots[host->chatIndex] && host->slots[host->chatIndex]->chat.conversations.count(*selected))
                { host->saveDraft(true); host->conversation = *selected; host->refreshChat(true); }
            }
            break;
        case ChatLatest: host->transcript.tail(); break;
        case ChatCompose: if (HIWORD(wparam) == EN_CHANGE) host->composeChanged(); break;
        case PinActions: host->pinActions(); break;
        case AttentionReview: host->attentionAction(false); break;
        case AttentionDismiss: host->attentionAction(true); break;
        case ChatNewIM: host->openFriendsWindow(host->chatIndex,-1,true);break;
        case ChatFriends: host->toggleFriends();break;
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
        if(!host->saveOptions(false))
        {
            const int choice=MessageBoxW(window,L"Host preferences could not be saved.\n\nYes: retry saving. No: close without saving. Cancel: keep the host open.",L"Preferences not saved",MB_YESNOCANCEL|MB_ICONWARNING);
            if(choice==IDCANCEL || (choice==IDYES && !host->saveOptions(false)))return 0;
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
    std::ifstream chatPreferencesFile(state.profiles/L"host-chat.dat",std::ios::binary);
    if (chatPreferencesFile)
    {
        std::string data(ChatPreferences::MaxBytes+1,'\0'); chatPreferencesFile.read(data.data(),static_cast<std::streamsize>(data.size()));
        data.resize(static_cast<std::size_t>(chatPreferencesFile.gcount()));
        if (!ChatPreferences::decode(data,state.chatPreferences))
        {
            std::ifstream previous(state.profiles/L"host-chat.dat.bak",std::ios::binary);std::string backup(ChatPreferences::MaxBytes+1,'\0');
            previous.read(backup.data(),static_cast<std::streamsize>(backup.size()));backup.resize(static_cast<std::size_t>(previous.gcount()));
            state.lastNotice=ChatPreferences::decode(backup,state.chatPreferences)?L"Recovered earlier chat/sound choices from their backup.":L"Saved chat/sound choices are invalid; using defaults.";
        }
    }
    std::ifstream experienceFile(state.profiles/L"host-experience.dat",std::ios::binary);
    if(experienceFile){std::string data(HostExperience::MaxBytes+1,'\0');experienceFile.read(data.data(),static_cast<std::streamsize>(data.size()));data.resize(static_cast<std::size_t>(experienceFile.gcount()));if(!HostExperience::decode(data,state.experience) || bindingsConflict(state.experience,state.presentation)){state.experience=HostExperience{};state.lastNotice=L"Saved host experience settings are invalid; using defaults.";}}
    else if(!haveHostOptions)state.chrome=1;
    if(!state.chatPreferences.haveCategories)state.chatPreferences.attentionMask=legacyAttentionCategories(state.experience.attentionMask);
    std::ifstream setsFile(state.profiles/L"session-sets.dat",std::ios::binary);
    if(setsFile){std::string data(SessionSetStore::MaxBytes+1,'\0');setsFile.read(data.data(),static_cast<std::streamsize>(data.size()));data.resize(static_cast<std::size_t>(setsFile.gcount()));if(!SessionSetStore::decode(data,state.setStore))state.lastNotice=L"Saved session sets are invalid; no set was opened.";}
    HostPreferences combined;
    const bool haveCombined=state.readPreferences(state.profiles/L"host-preferences.dat",combined);
    if(haveCombined || state.readPreferences(state.profiles/L"host-preferences.dat.bak",combined))
    {
        state.applyPreferences(combined);
        if(haveCombined)state.autosave.committed(combined.encode());
        else state.lastNotice=L"Recovered host preferences from the previous saved copy.";
    }
    else if(GetFileAttributesW((state.profiles/L"host-preferences.dat").c_str())!=INVALID_FILE_ATTRIBUTES)
    {state.saveError=L"Saved host preferences could not be read. Earlier settings loaded; the invalid file will be retained.";}
    upgradeHostToolbar(state.experience);
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
    auto friendsClass=panelClass;friendsClass.lpfnWndProc=friendsProc;friendsClass.lpszClassName=L"FolderstormSessionFriends";
    if(!RegisterClassExW(&friendsClass))return 1;
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
        auto& cycleEditor=state.monitors[CyclingView].editor;
        // Filter before dialog routing as well: old composer Enter must not
        // activate panel buttons after focus has left the unavailable composer.
        if(message.hwnd==cycleEditor.edit && !cycleEditor.allowsInput(message.message))
        {cycleEditor.keys={};continue;}
        if (state.hostShortcut(message)) continue;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE && state.busy())
        { state.skipTransition(); continue; }
        std::array<HWND,4+MonitorViews+MaxCharacters> panels{window,state.chatWindow,state.controlsWindow,state.attentionWindow};
        for (int i = 0; i < MonitorViews; ++i) panels[static_cast<std::size_t>(i)+4] = state.monitors[i].window;
        for(int i=0;i<MaxCharacters;++i)panels[static_cast<std::size_t>(i)+4+MonitorViews]=state.friendsWindows[i].window;
        const bool handled = routePanelDialog(message.hwnd, panels,
            [](HWND panel, HWND target)
            { return IsWindow(panel) && IsWindowVisible(panel) && (panel == target || IsChild(panel, target)); },
            [&message](HWND panel) { return IsDialogMessageW(panel, &message) != FALSE; });
        if (!handled) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    host = nullptr;
    return static_cast<int>(message.wParam);
}
