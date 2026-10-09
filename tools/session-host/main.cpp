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
#include "fssessionchatmodel.h"
#include "fssessionhostoptions.h"
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
constexpr int ChatAccount = 109, ChatConversation = 110, ChatCompose = 111, ChatSend = 112, ChatReview = 113;
constexpr int VoicePolicy = 114, BackgroundMute = 115, ChatRead = 116;
constexpr int ActiveCharacter = 117, ShowChat = 118;
constexpr int SessionOptions = 119;
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
    ChatView chat;
    Message pendingSend, pendingTyping;
    Message pendingAction;
    Message closingAction;
    bool hasPendingAction = false;
    std::string workspace;
    bool workspaceModified = false, preferencesOpen = false;
    ULONGLONG workspaceAt = 0;
    bool hasPendingSend = false, hasPendingTyping = false;
    std::uint32_t audioPolicy = 2, catalogIndex = 0;
    ULONGLONG eventsAt = 0, catalogAt = 0, typingAt = 0;
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
    HWND characterControl = nullptr, showChatControl = nullptr, detachControl = nullptr;
    HWND optionsControl = nullptr;
    Handle optionsLease;
    bool logoutOnClose = false;
    bool showChat = true;
    UINT dpi = 96;
    HFONT font = nullptr;
    HWND chatAccount = nullptr, chatConversation = nullptr, chatHistory = nullptr, chatCompose = nullptr;
    HWND chatSend = nullptr, chatReview = nullptr, chatRead = nullptr, voiceControl = nullptr, muteControl = nullptr, chatLabel = nullptr;
    int chatIndex = 0;
    std::string conversation;
    std::vector<std::string> conversationIds;
    bool updatingChat = false, voice = false, muteBackground = true;
    Mode standby[2]{Mode::Warm, Mode::Warm};
    std::unique_ptr<Slot> slots[2];
    Handoff handoff;
    int active = -1;
    bool embedding = false, detaching = false, closing = false, selectFirst = true, focusRequested = false;
    std::filesystem::path viewer, profiles;
    ~Host() { if (font) DeleteObject(font); }

    int scaled(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    void updateDpi(UINT value)
    {
        dpi = (std::max)(96u, value);
        HFONT replacement = CreateFontW(-scaled(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (replacement)
        {
            EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL
            { SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(data), TRUE); return TRUE; }, reinterpret_cast<LPARAM>(replacement));
            if (font) DeleteObject(font);
            font = replacement;
        }
    }
    void refreshCharacters()
    {
        const auto selected = SendMessageW(characterControl, CB_GETCURSEL, 0, 0);
        for (int i = 0; i < 2; ++i)
        {
            std::wstring text = std::to_wstring(i + 1) + L": ";
            text += slots[i] && !slots[i]->snapshot.name.empty() ? wide(slots[i]->snapshot.name) : L"not logged in";
            if (i == active) text += L" · active";
            if (SendMessageW(characterControl, CB_GETCOUNT, 0, 0) > i) SendMessageW(characterControl, CB_DELETESTRING, static_cast<WPARAM>(i), 0);
            SendMessageW(characterControl, CB_INSERTSTRING, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(characterControl, CB_SETCURSEL, static_cast<WPARAM>(active >= 0 ? active : selected >= 0 ? selected : 0), 0);
        EnableWindow(characterControl, !busy());
    }

    void message(const std::wstring& text) { SetWindowTextW(notice, text.c_str()); }
    bool send(int index, Kind kind, Mode mode = Mode::Warm, std::uint64_t surface = 0, const Message* bound = nullptr)
    {
        auto& slot = *slots[index];
        if (slot.waiting || !slot.pipe.alive() || slot.pipe.writing()) return false;
        Message request;
        if (bound)
        {
            if (!slot.chat.identity.owns(*bound) || !slot.chat.identity.owns(slot.snapshot) || (slot.snapshot.state != State::Ready && kind != Kind::Quit))
            { message(L"The selected sending session changed. Text was not sent."); return false; }
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
        if (kind == Kind::AudioPolicy) request.unread = (voice ? 1u : 0u) | (muteBackground ? 2u : 0u);
        if ((kind == Kind::Focus || (kind == Kind::SetMode && mode == Mode::Active)) && GetForegroundWindow() == window)
            AllowSetForegroundWindow(slot.pid);
        if (kind == Kind::PermitLogin || kind == Kind::DenyLogin) { request.grid = slot.snapshot.grid; request.name = slot.snapshot.name; }
        if (!slot.pipe.send(request)) return false;
        slot.request = request; slot.waiting = true; slot.sentAt = GetTickCount64();
        return true;
    }
    HostOptions options() const
    {
        HostOptions result; result.standby[0] = standby[0]; result.standby[1] = standby[1];
        result.voice = voice; result.muteBackground = muteBackground; result.hosted = embedding; result.chat = showChat;
        return result;
    }
    void saveOptions()
    {
        const auto temporary = profiles / L"host-options.tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto data = options().encode(); output.write(data.data(), static_cast<std::streamsize>(data.size())); output.close();
        if (!output || !MoveFileExW(temporary.c_str(), (profiles / L"host-options.txt").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        { message(L"Host choices could not be saved. Current session choices remain active."); return; }
        message(L"Host choices saved. Character credentials, chats and workspace contents are not in this file.");
    }
    void sessionMenu()
    {
        const int index = SendMessageW(characterControl, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
        auto* slot = slots[index].get();
        Message action;
        if (slot)
        {
            action.worker = slot->chat.identity.worker; action.generation = slot->chat.identity.generation;
            action.account = slot->chat.identity.account; action.grid = slot->chat.identity.grid;
        }
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        const std::wstring name = slot && !slot->snapshot.name.empty() ? wide(slot->snapshot.name) : index == 0 ? L"Character 1" : L"Character 2";
        AppendMenuW(menu, MF_STRING | (index == active && !busy() ? 0 : MF_GRAYED), 201, (L"Manage workspaces: " + name).c_str());
        AppendMenuW(menu, MF_STRING | (slot && slot->pipe.alive() && !slot->detached && !busy() ? 0 : MF_GRAYED), 202, (L"Close character: " + name + L"…").c_str());
        AppendMenuW(menu, MF_STRING, 203, L"Open selected profile folder");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (voice ? MF_CHECKED : 0), 204, L"Voice follows active character");
        AppendMenuW(menu, MF_STRING | (muteBackground ? MF_CHECKED : 0), 205, L"Mute background sound/media");
        AppendMenuW(menu, MF_STRING, 206, L"Save host choices");
        POINT point{}; GetCursorPos(&point);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (choice == 201 || choice == 202)
        {
            // Timer replies may have changed login while the popup was open.
            if (!slots[index] || slots[index].get() != slot || !slot->chat.identity.owns(action) || slot->hasPendingAction)
            { message(L"Character changed or has a pending action. Select it again."); return; }
            action.kind = choice == 201 ? Kind::WorkspaceMenu : Kind::Quit;
            slot->pendingAction = action; slot->hasPendingAction = true;
            message(choice == 201 ? L"Opening this character's workspace controls…" : L"Returning this character to its native window for the normal logout confirmation…");
        }
        else if (choice == 203)
        {
            const auto path = profiles / (index == 0 ? L"Character1" : L"Character2");
            ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        else if (choice == 204) { voice = !voice; SendMessageW(voiceControl, BM_SETCHECK, voice ? BST_CHECKED : BST_UNCHECKED, 0); }
        else if (choice == 205) { muteBackground = !muteBackground; SendMessageW(muteControl, BM_SETCHECK, muteBackground ? BST_CHECKED : BST_UNCHECKED, 0); }
        else if (choice == 206) saveOptions();
    }
    std::string editText() const
    {
        const int length = (std::min)(1023, GetWindowTextLengthW(chatCompose));
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(chatCompose, value.data(), length + 1);
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
        if (found != slot.chat.conversations.end()) found->second.draft = editText();
        if (stopTyping && !conversation.empty())
        {
            slot.pendingTyping = boundChat(Kind::Typing); slot.pendingTyping.unread = 0;
            slot.hasPendingTyping = true;
        }
    }
    void refreshChat(bool loadDraft = false)
    {
        if (!chatAccount) return;
        updatingChat = true;
        for (int i = 0; i < 2; ++i)
        {
            const std::wstring name = slots[i] && !slots[i]->snapshot.name.empty() ? wide(slots[i]->snapshot.name) : i == 0 ? L"Character 1" : L"Character 2";
            if (SendMessageW(chatAccount, CB_GETCOUNT, 0, 0) > i) SendMessageW(chatAccount, CB_DELETESTRING, static_cast<WPARAM>(i), 0);
            SendMessageW(chatAccount, CB_INSERTSTRING, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(chatAccount, CB_SETCURSEL, static_cast<WPARAM>(chatIndex), 0);
        auto* slot = slots[chatIndex].get();
        const bool ready = slot && !slot->detached && slot->pipe.alive() && slot->snapshot.state == State::Ready;
        const bool restricted = slot && (slot->snapshot.flags & ChatRestricted);
        std::wstring label = L"Send as: " + (slot && !slot->snapshot.name.empty() ? wide(slot->snapshot.name) : L"not logged in");
        if (!ready) label += L" · unavailable";
        else if (restricted) label += L" · shared chat restricted; use native viewer";
        else if (slot->chat.gap) label += L" · history gap; native history has more detail";
        SetWindowTextW(chatLabel, label.c_str());
        if (loadDraft)
        {
            SendMessageW(chatConversation, CB_RESETCONTENT, 0, 0); conversationIds.clear();
            if (slot) for (const auto& entry : slot->chat.conversations)
            {
                conversationIds.push_back(entry.first);
                std::wstring name = wide(entry.second.title);
                if (entry.second.unread) name += L" · " + std::to_wstring(entry.second.unread) + L" unread";
                SendMessageW(chatConversation, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            }
            const auto found = std::find(conversationIds.begin(), conversationIds.end(), conversation);
            const auto selection = found == conversationIds.end() ? 0 : static_cast<int>(found - conversationIds.begin());
            SendMessageW(chatConversation, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
            if (!conversationIds.empty()) conversation = conversationIds[static_cast<std::size_t>(selection)];
            const auto draft = slot ? slot->chat.conversations.find(conversation) : std::map<std::string, Conversation>::iterator{};
            const std::string wanted = slot && draft != slot->chat.conversations.end() ? draft->second.draft : "";
            if (editText() != wanted) SetWindowTextW(chatCompose, wide(wanted).c_str());
        }
        std::wstring history;
        if (slot && !restricted) for (const auto& line : slot->chat.lines)
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
        EnableWindow(chatCompose, ready && !restricted && !sending && !isNotice);
        EnableWindow(chatSend, ready && !restricted && !sending && !isNotice);
        EnableWindow(chatReview, ready); EnableWindow(chatRead, ready && !restricted);
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
    }
    void sendChat()
    {
        if (!slots[chatIndex]) return;
        auto& slot = *slots[chatIndex];
        if (slot.hasPendingSend || (slot.waiting && slot.request.kind == Kind::SendChat)) return;
        Message request = boundChat(Kind::SendChat); request.text = editText();
        if (request.text.empty() || request.text.size() > 1023 || !validUtf8(request.text, true))
        { message(L"Use a message of 1 to 1023 UTF-8 bytes."); return; }
        slot.pendingSend = std::move(request); slot.hasPendingSend = true;
        message(L"Sending through the selected character's native chat…"); refreshChat();
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
        const bool changedSession = !slot.chat.identity.owns(response);
        slot.chat.bind(response);
        if (changedSession)
        {
            slot.hasPendingSend = slot.hasPendingTyping = false; slot.catalogIndex = 0; slot.catalogAt = slot.eventsAt = 0;
            slot.hasPendingAction = false; slot.workspace.clear(); slot.workspaceModified = slot.preferencesOpen = false; slot.workspaceAt = 0;
            if (chatIndex == index) { conversation.clear(); refreshChat(true); }
        }
        slot.snapshot = response;
        if (request.kind == Kind::WorkspaceInfo && !(response.flags & Error))
        {
            slot.workspace = response.title; slot.workspaceModified = (response.unread & 1) != 0;
            slot.preferencesOpen = (response.unread & 2) != 0; slot.workspaceAt = GetTickCount64();
        }
        if (response.flags & ChatRestricted)
        {
            slot.chat.lines.clear(); slot.chat.conversations.clear(); slot.chat.cursor = 0;
            slot.chat.conversations.emplace("", Conversation{"", "Nearby chat", "", Topic::Nearby, 0});
            if (chatIndex == index) { conversation.clear(); refreshChat(true); }
        }
        if (!(response.flags & Error) && (request.kind == Kind::Events || request.kind == Kind::Conversations))
        {
            slot.chat.accept(response);
            if (request.kind == Kind::Events) slot.eventsAt = GetTickCount64();
            else if (response.eventType == EventType::None) { slot.catalogIndex = 0; slot.catalogAt = GetTickCount64(); }
            else ++slot.catalogIndex;
            if (index == chatIndex)
            {
                saveDraft(false);
                const bool catalogChanged = response.eventType == EventType::Conversation || response.eventType == EventType::Chat || response.eventType == EventType::Notice;
                refreshChat(catalogChanged);
            }
        }
        if (request.kind == Kind::AudioPolicy && !(response.flags & Error)) slot.audioPolicy = request.unread;
        if (request.kind == Kind::MarkRead && !(response.flags & Error))
        {
            const auto found = slot.chat.conversations.find(request.conversation);
            if (found != slot.chat.conversations.end()) found->second.unread = 0;
        }
        if (request.kind == Kind::SendChat)
        {
            if (response.flags & Error) message(L"Send as " + wide(request.name.empty() ? slot.snapshot.name : request.name) + L": " + wide(response.detail));
            else
            {
                const auto found = slot.chat.conversations.find(request.conversation);
                if (found != slot.chat.conversations.end() && found->second.draft == request.text) found->second.draft.clear();
                if (index == chatIndex && request.conversation == conversation) { updatingChat = true; SetWindowTextW(chatCompose, L""); updatingChat = false; }
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
            if (request.kind == Kind::Detach || request.kind == Kind::Quit) { detaching = closing = logoutOnClose = false; }
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
        if (slot.snapshot.flags & VoiceOwner) text << L" · voice owner";
        if (!slot.workspace.empty()) text << L" · workspace " << wide(slot.workspace) << (slot.workspaceModified ? L" *" : L"");
        if (slot.preferencesOpen) text << L" · Preferences open";
        std::uint64_t unread = 0; for (const auto& entry : slot.chat.conversations) unread += entry.second.unread;
        if (unread) text << L" · " << unread << L" unread";
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
                if (slot.hasPendingSend || (slot.waiting && slot.request.kind == Kind::SendChat))
                    message(L"Chat connection lost: delivery is uncertain. Text was not retried; check the owning character's native chat before sending again.");
                slot.hasPendingSend = slot.hasPendingTyping = false;
                slot.hasPendingAction = false;
                slot.waiting = false;
                if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index) failHandoff(index, L"Character connection lost. Other connected characters can still be selected.");
                if (active == index) active = -1;
            }
            if (slot.waiting && GetTickCount64() - slot.sentAt > (slot.snapshot.state == State::Ready ? 12000ULL : 60000ULL))
            {
                slot.pipe.close(); slot.waiting = false; slot.detached = true;
                if (slot.request.kind == Kind::SendChat)
                    message(L"Chat send timed out: delivery is uncertain and will not be retried. Check the native conversation.");
                if (handoff.step() != Handoff::Step::Idle && handoff.worker() == index) failHandoff(index, L"The viewer did not respond; returning it to an ordinary window.");
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
                    const int other = 1 - index;
                    const bool collision = slots[other] && slots[other]->running() && loginCollision(slot.snapshot, slots[other]->snapshot, slots[other]->reservation);
                    if (send(index, collision ? Kind::DenyLogin : Kind::PermitLogin))
                    {
                        if (!collision) slot.reservation = slot.snapshot.name;
                        else message(L"That character is already reserved. Use a different character in the second viewer.");
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
                    (GetFocus() == window || GetFocus() == viewport || GetFocus() == nullptr ||
                        GetFocus() == characterControl || GetFocus() == embedControl))
                {
                    if (send(index, Kind::Focus)) focusRequested = false;
                }
                else if (!busy() && index != active && slot.snapshot.state == State::Ready && slot.snapshot.mode != standby[index])
                    send(index, Kind::SetMode, standby[index]);
                else if (slot.snapshot.state == State::Ready && slot.audioPolicy != ((voice ? 1u : 0u) | (muteBackground ? 2u : 0u)))
                    send(index, Kind::AudioPolicy);
                else if (slot.hasPendingAction)
                {
                    const auto action = slot.pendingAction; slot.hasPendingAction = false;
                    send(index, action.kind, Mode::Warm, 0, &action);
                }
                else if (slot.hasPendingSend)
                {
                    const auto pending = slot.pendingSend; slot.hasPendingSend = false;
                    if (!send(index, Kind::SendChat, Mode::Warm, 0, &pending))
                        message(L"Send was not acknowledged. Text remains in its original draft and was not retried.");
                }
                else if (slot.hasPendingTyping && (slot.pendingTyping.unread == 0 || GetTickCount64() - slot.typingAt >= 1000))
                {
                    const auto pending = slot.pendingTyping; slot.hasPendingTyping = false;
                    if (send(index, Kind::Typing, Mode::Warm, 0, &pending)) slot.typingAt = GetTickCount64();
                }
                else if (slot.snapshot.state == State::Ready && !(slot.snapshot.flags & ChatRestricted) && GetTickCount64() - slot.eventsAt >= 100)
                    send(index, Kind::Events);
                else if (slot.snapshot.state == State::Ready && !(slot.snapshot.flags & ChatRestricted) && GetTickCount64() - slot.catalogAt >= 20000)
                    send(index, Kind::Conversations);
                else if (slot.snapshot.state == State::Ready && GetTickCount64() - slot.workspaceAt >= 2000)
                    send(index, Kind::WorkspaceInfo);
                else if (GetTickCount64() - slot.pollAt >= 1000) send(index, Kind::Poll);
            }
            describe(index);
            EnableWindow(launchButton[index], !slot.running() && !busy());
            EnableWindow(switchButton[index], slot.running() && slot.pipe.alive() && !slot.detached && slot.snapshot.state == State::Ready && !busy());
        }
        refreshChat();
        refreshCharacters();
        if (!busy() && active < 0 && selectFirst)
            for (int i = 0; i < 2; ++i) if (slots[i] && slots[i]->pipe.alive() && !slots[i]->detached && slots[i]->snapshot.state == State::Ready) { switchTo(i); selectFirst = false; break; }
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
        handoff.cancel(); detaching = true; closing = close; logoutOnClose = logout; focusRequested = false;
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
    void layout()
    {
        RECT rect{}; GetClientRect(window, &rect);
        const int width = static_cast<int>(rect.right);
        const int height = static_cast<int>(rect.bottom);
        auto move = [this](HWND child, int x, int y, int w, int h) { MoveWindow(child, scaled(x), scaled(y), scaled(w), scaled(h), TRUE); };
        move(characterControl, 10, 10, 170, 200);
        move(launchButton[0], 190, 10, 45, 28); move(launchButton[1], 240, 10, 45, 28);
        move(embedControl, 295, 10, 180, 28); move(detachControl, 485, 10, 110, 28);
        move(showChatControl, 605, 10, 90, 28); move(optionsControl, 705, 10, 80, 28);
        for (int i = 0; i < 2; ++i)
        {
            MoveWindow(status[i], scaled(10), scaled(48 + i * 24), width - scaled(215), scaled(20), TRUE);
            MoveWindow(standbyControl[i], width - scaled(200), scaled(46 + i * 24), scaled(190), scaled(130), TRUE);
        }
        const int chatTop = showChat ? (std::max)(scaled(220), height - scaled(240)) : height - scaled(26);
        MoveWindow(viewport, 0, scaled(99), width, (std::max)(1, chatTop - scaled(105)), TRUE);
        MoveWindow(chatAccount, scaled(10), chatTop, scaled(180), scaled(150), TRUE);
        MoveWindow(chatConversation, scaled(200), chatTop, width - scaled(460), scaled(200), TRUE);
        MoveWindow(chatReview, width - scaled(250), chatTop, scaled(140), scaled(24), TRUE);
        MoveWindow(chatRead, width - scaled(100), chatTop, scaled(90), scaled(24), TRUE);
        MoveWindow(chatLabel, scaled(10), chatTop + scaled(29), width - scaled(20), scaled(20), TRUE);
        MoveWindow(chatHistory, scaled(10), chatTop + scaled(52), width - scaled(20), scaled(88), TRUE);
        MoveWindow(chatCompose, scaled(10), chatTop + scaled(145), width - scaled(105), scaled(35), TRUE);
        MoveWindow(chatSend, width - scaled(85), chatTop + scaled(145), scaled(75), scaled(35), TRUE);
        MoveWindow(voiceControl, scaled(10), chatTop + scaled(185), scaled(255), scaled(24), TRUE);
        MoveWindow(muteControl, scaled(280), chatTop + scaled(185), scaled(280), scaled(24), TRUE);
        for (HWND child : {chatAccount, chatConversation, chatReview, chatRead, chatLabel, chatHistory, chatCompose, chatSend, voiceControl, muteControl})
            ShowWindow(child, showChat ? SW_SHOWNOACTIVATE : SW_HIDE);
        MoveWindow(notice, scaled(10), height - scaled(23), width - scaled(20), scaled(20), TRUE);
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
        host->launchButton[0] = control(window, L"BUTTON", L"+ 1", BS_PUSHBUTTON | WS_TABSTOP, 10, 10, 80, 28, Launch1);
        host->switchButton[0] = control(window, L"BUTTON", L"Switch to 1", BS_PUSHBUTTON, 95, 10, 90, 28, Switch1);
        host->launchButton[1] = control(window, L"BUTTON", L"+ 2", BS_PUSHBUTTON | WS_TABSTOP, 195, 10, 80, 28, Launch2);
        host->switchButton[1] = control(window, L"BUTTON", L"Switch to 2", BS_PUSHBUTTON, 280, 10, 90, 28, Switch2);
        ShowWindow(host->switchButton[0], SW_HIDE); ShowWindow(host->switchButton[1], SW_HIDE);
        host->characterControl = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 10, 10, 170, 200, ActiveCharacter);
        host->showChatControl = control(window, L"BUTTON", L"Chat panel", BS_AUTOCHECKBOX | WS_TABSTOP, 695, 10, 90, 28, ShowChat);
        SendMessageW(host->showChatControl, BM_SETCHECK, host->showChat ? BST_CHECKED : BST_UNCHECKED, 0);
        host->embedControl = control(window, L"BUTTON", L"Host active viewer", BS_AUTOCHECKBOX | WS_TABSTOP, 383, 10, 235, 28, HostSurface);
        host->detachControl = control(window, L"BUTTON", L"Separate windows", BS_PUSHBUTTON | WS_TABSTOP, 630, 10, 125, 28, DetachAll);
        host->optionsControl = control(window, L"BUTTON", L"Session…", BS_PUSHBUTTON | WS_TABSTOP, 705, 10, 80, 28, SessionOptions);
        SendMessageW(host->embedControl, BM_SETCHECK, host->embedding ? BST_CHECKED : BST_UNCHECKED, 0);
        host->status[0] = control(window, L"STATIC", L"Character 1: not launched", SS_LEFT, 10, 48, 850, 20);
        host->status[1] = control(window, L"STATIC", L"Character 2: not launched", SS_LEFT, 10, 70, 850, 20);
        for (int i = 0; i < 2; ++i)
        {
            host->standbyControl[i] = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 760, 46 + i * 24, 190, 130, i == 0 ? Standby1 : Standby2);
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Background: Warm"));
            SendMessageW(host->standbyControl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Economy (experimental)"));
            SendMessageW(host->standbyControl[i], CB_SETCURSEL, host->standby[i] == Mode::Economy ? 1 : 0, 0);
        }
        host->viewport = control(window, L"STATIC", L"", SS_BLACKRECT | WS_CLIPCHILDREN, 0, 99, 880, 490);
        host->notice = control(window, L"STATIC", L"Prototype: warm standby, separate profiles. Voice and MCP are off.", SS_LEFT, 10, 600, 850, 20);
        host->chatAccount = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 10, 400, 180, 150, ChatAccount);
        host->chatConversation = control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 200, 400, 400, 200, ChatConversation);
        host->chatHistory = control(window, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 10, 452, 850, 88);
        host->chatCompose = control(window, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER | WS_TABSTOP, 10, 545, 800, 35, ChatCompose);
        SendMessageW(host->chatCompose, EM_SETLIMITTEXT, 1023, 0);
        host->chatSend = control(window, L"BUTTON", L"Send", BS_PUSHBUTTON | WS_TABSTOP, 820, 545, 75, 35, ChatSend);
        host->chatReview = control(window, L"BUTTON", L"Review in viewer", BS_PUSHBUTTON | WS_TABSTOP, 620, 400, 140, 24, ChatReview);
        host->chatRead = control(window, L"BUTTON", L"Mark read", BS_PUSHBUTTON | WS_TABSTOP, 770, 400, 90, 24, ChatRead);
        host->chatLabel = control(window, L"STATIC", L"Send as: not logged in", SS_LEFT, 10, 429, 850, 20);
        host->voiceControl = control(window, L"BUTTON", L"Voice follows active character", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 585, 255, 24, VoicePolicy);
        host->muteControl = control(window, L"BUTTON", L"Mute background world/UI/media sound", BS_AUTOCHECKBOX | WS_TABSTOP, 280, 585, 280, 24, BackgroundMute);
        SendMessageW(host->voiceControl, BM_SETCHECK, host->voice ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(host->muteControl, BM_SETCHECK, host->muteBackground ? BST_CHECKED : BST_UNCHECKED, 0);
        host->refreshChat(true);
        host->refreshCharacters(); host->updateDpi(96); host->layout();
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
    case WM_DPICHANGED:
    {
        host->updateDpi(LOWORD(wparam));
        const auto* next = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, next->left, next->top, next->right - next->left, next->bottom - next->top, SWP_NOZORDER | SWP_NOACTIVATE);
        host->layout(); return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {host->scaled(805), host->scaled(host->showChat ? 540 : 350)}; return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case Launch1: host->launch(0); break;
        case Launch2: host->launch(1); break;
        case Switch1: host->switchTo(0); break;
        case Switch2: host->switchTo(1); break;
        case SessionOptions: host->sessionMenu(); break;
        case ActiveCharacter:
            if (HIWORD(wparam) == CBN_SELCHANGE)
                host->switchTo(SendMessageW(host->characterControl, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0);
            break;
        case ShowChat:
            host->saveDraft(true); host->showChat = SendMessageW(host->showChatControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            host->layout(); break;
        case ChatAccount:
            if (!host->updatingChat && HIWORD(wparam) == CBN_SELCHANGE)
            {
                host->saveDraft(true); host->chatIndex = SendMessageW(host->chatAccount, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
                host->conversation.clear(); host->refreshChat(true);
            }
            break;
        case ChatConversation:
            if (!host->updatingChat && HIWORD(wparam) == CBN_SELCHANGE)
            {
                const auto selected = SendMessageW(host->chatConversation, CB_GETCURSEL, 0, 0);
                host->saveDraft(true);
                if (selected >= 0 && static_cast<std::size_t>(selected) < host->conversationIds.size()) host->conversation = host->conversationIds[static_cast<std::size_t>(selected)];
                host->refreshChat(true);
            }
            break;
        case ChatCompose: if (HIWORD(wparam) == EN_CHANGE) host->composeChanged(); break;
        case ChatSend: host->saveDraft(true); host->sendChat(); break;
        case ChatReview: host->switchTo(host->chatIndex); break;
        case ChatRead:
            if (host->slots[host->chatIndex])
            {
                const auto request = host->boundChat(Kind::MarkRead);
                if (!host->send(host->chatIndex, Kind::MarkRead, Mode::Warm, 0, &request)) host->message(L"Viewer is busy; try Mark read again.");
            }
            break;
        case VoicePolicy: host->voice = SendMessageW(host->voiceControl, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
        case BackgroundMute: host->muteBackground = SendMessageW(host->muteControl, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
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
            host->message(host->embedding ? L"Active viewer fitted to this host. Use Separate windows for fullscreen or recovery." : L"Using separate viewer windows; chosen standby modes remain enabled.");
            break;
        case DetachAll: host->detach(false); break;
        }
        return 0;
    case WM_CLOSE:
        if (host->closing)
        {
            if (host->logoutOnClose && MessageBoxW(window, L"Close the host and leave remaining separate viewers logged in?",
                L"Folderstorm character sessions", MB_YESNO | MB_ICONQUESTION) == IDYES) host->logoutOnClose = false;
            return 0;
        }
        switch (MessageBoxW(window, L"Yes: log out the characters and close this host.\nNo: close this host and keep characters logged in in separate windows.\nCancel: keep working.",
            L"Folderstorm character sessions", MB_YESNOCANCEL | MB_ICONQUESTION))
        {
        case IDYES: host->detach(true, true); break;
        case IDNO: host->detach(true); break;
        }
        return 0;
    case WM_DESTROY: KillTimer(window, Timer); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    // Resolve at runtime for older Windows SDK/runtime compatibility.
    using DpiContext = BOOL(WINAPI*)(HANDLE);
    const auto dpiContext = reinterpret_cast<DpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpiContext) dpiContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); // PER_MONITOR_AWARE_V2
    else SetProcessDPIAware();
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
    std::ifstream savedOptions(state.profiles / L"host-options.txt", std::ios::binary);
    if (savedOptions)
    {
        char data[513]{}; savedOptions.read(data, sizeof(data)); HostOptions choices;
        if (HostOptions::decode(std::string(data, static_cast<std::size_t>(savedOptions.gcount())), choices))
        {
            state.standby[0] = choices.standby[0]; state.standby[1] = choices.standby[1];
            state.voice = choices.voice; state.muteBackground = choices.muteBackground; state.embedding = choices.hosted; state.showChat = choices.chat;
        }
    }
    host = &state;
    WNDCLASSW klass{}; klass.lpfnWndProc = windowProc; klass.hInstance = instance;
    klass.lpszClassName = L"FolderstormSessionPrototype"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&klass)) return 1;
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"Folderstorm character sessions",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 980, 700, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    using WindowDpi = UINT(WINAPI*)(HWND);
    const auto windowDpi = reinterpret_cast<WindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (windowDpi) { state.updateDpi(windowDpi(window)); state.layout(); }
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    host = nullptr;
    return static_cast<int>(message.wParam);
}
