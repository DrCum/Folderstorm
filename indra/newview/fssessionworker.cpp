/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "fssessionworker.h"

#if LL_WINDOWS
#include "fssessionpipe.h"
#include "fssessionchatmodel.h"
#include "fseventapibridge.h"
#include "fsworkspacecontroller.h"
#include "llagent.h"
#include "llagentdata.h"
#include "llagentui.h"
#include "llchat.h"
#include "llimview.h"
#include "llnotifications.h"
#include "llspeakers.h"
#include "fsnearbychathub.h"
#include "fsdata.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include "llappviewer.h"
#include "llfocusmgr.h"
#include "llfloaterreg.h"
#include "llkeyboard.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"
#include "llviewerinput.h"
#include "llviewerjoystick.h"
#include "llviewernetwork.h"
#include "llviewerwindow.h"
#include "llvoiceclient.h"
#include "llwindow.h"
#include "llwindowwin32.h"
#include "pipeline.h"
#include <cstdlib>
#include <memory>
#include <map>

namespace
{
using namespace fs_session;
struct Worker
{
    Pipe pipe;
    HANDLE profileLease = INVALID_HANDLE_VALUE;
    WorkerId id{};
    DWORD host = 0;
    std::uint64_t generation = 1, sequence = 0, frames = 0, maintenance = 0, promotionStart = 0;
    ULONGLONG lastCommand = 0;
    LLUUID session;
    Mode mode = Mode::Warm;
    std::string loginGrid, loginName;
    FSSessionWorker::LoginGate gate = FSSessionWorker::LoginGate::Wait;
    bool loginRequested = false, readyApplied = false, promoting = false, detached = false, inputGranted = false;
    bool economyTrimmed = false;
    bool hostedStyle = false;
    std::uintptr_t focusLease = 0;
    bool permitVoice = false, muteBackground = true, chatBlocked = false;
    EventBuffer events;
    boost::signals2::scoped_connection chatConnection, notificationConnection;
    Message pendingPromotion;
    HWND window = nullptr, parent = nullptr;
    LONG_PTR style = 0, exStyle = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    struct Override { LLSD effective; bool saved = false; };
    std::map<std::string, Override> controls;
    std::array<bool, KEY_COUNT> heldKeys{};

    ~Worker() { if (profileLease != INVALID_HANDLE_VALUE) CloseHandle(profileLease); }

    bool ready() const { return LLStartUp::getStartupState() == STATE_STARTED && !gDisconnected; }
    bool embedded() const { return parent != nullptr; }
    bool hostForeground() const
    {
        if (!parent || !IsWindow(parent)) return false;
        const HWND root = GetAncestor(parent, GA_ROOT);
        return root && GetForegroundWindow() == root && IsWindowVisible(root) && !IsIconic(root);
    }
    bool sharingAllowed() const
    {
        // Native history can contain pre-restriction names/locations. Never
        // expose that raw cache in the cross-account shell while masked.
        return !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES) && !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC);
    }
    bool voiceAllowed() const { return permitVoice && ready() && inputGranted && mode == Mode::Active && !promoting; }
    void applyVoice()
    {
        if (!LLVoiceClient::instanceExists()) return;
        if (!voiceAllowed())
        {
            LLVoiceClient::instance().tuningStop();
            LLVoiceClient::instance().setUserPTTState(false);
            LLVoiceClient::instance().setMuteMic(true);
        }
        LLVoiceClient::setVoiceEnabled(LLVoiceClient::instance().voiceEnabled(true));
    }
    static Topic topicFor(const LLIMModel::LLIMSession& session)
    {
        return session.isGroupSessionType() ? Topic::Group : session.isAdHocSessionType() ? Topic::Conference : Topic::Private;
    }
    void connectChat()
    {
        if (!chatConnection.connected())
            chatConnection = LLIMModel::instance().addNewMsgCallback([this](const LLSD& data)
            {
                if (detached || !ready() || !sharingAllowed()) return;
                auto* session = LLIMModel::instance().findIMSession(data["session_id"].asUUID());
                const LLUUID sender = data["from_id"].asUUID();
                if (!session || (!sender.isNull() && sender != gAgentID && !RlvActions::canReceiveIM(sender))) return;
                Message message; message.eventType = EventType::Chat; message.topic = topicFor(*session);
                message.conversation = session->mSessionID.asString(); message.title = session->mName;
                message.sender = data["from"].asString(); message.recipient = sender.isNull() ? "" : sender.asString();
                message.text = data["message"].asString(); message.unread = static_cast<std::uint32_t>((std::max)(0, session->mNumUnread));
                events.push(std::move(message));
            });
        if (!notificationConnection.connected())
            if (auto channel = LLNotifications::instance().getChannel("Visible"))
                notificationConnection = channel->connectChanged([this](const LLSD& data)
                {
                    if (!detached && ready() && data["sigtype"].asString() == "add")
                    {
                        // Source-bound attention only. All offer/payment/permission
                        // responses still require their native owning viewer.
                        Message message; message.eventType = EventType::Notice; message.topic = Topic::Notice;
                        message.conversation = "00000000-0000-0000-0000-000000000000"; message.title = "Viewer notifications";
                        message.text = "Review this notification in this character's viewer."; message.unread = 1;
                        events.push(std::move(message));
                    }
                    return false;
                });
    }
    bool imAllowed(const LLIMModel::LLIMSession& session) const
    {
        if (session.isP2PSessionType()) return RlvActions::canSendIM(session.mOtherParticipantID);
        if (session.isGroupSessionType()) return RlvActions::canSendIM(session.mSessionID);
        if (!session.isAdHocSessionType() || !session.mSpeakers) return false;
        LLSpeakerMgr::speaker_list_t speakers; session.mSpeakers->getSpeakerList(&speakers, true);
        for (const auto& speaker : speakers)
            if (speaker->mID != gAgentID && !RlvActions::canSendIM(speaker->mID)) return false;
        return true;
    }
    void chatCommand(const Message& request)
    {
        if (!ready() || request.account != gAgentID.asString() || request.grid != loginGrid)
        { reply(request, "The sending character is unavailable or changed login."); return; }
        if (!sharingAllowed()) { reply(request, "Shared chat is unavailable while names or locations are restricted."); return; }
        Message payload;
        if (request.kind == Kind::Events)
        {
            payload = events.after(request.cursor);
            if (!payload.recipient.empty() && payload.recipient != gAgentID.asString() && !RlvActions::canReceiveIM(LLUUID(payload.recipient)))
            { payload.text.clear(); payload.sender.clear(); payload.eventType = EventType::Gap; }
            auto response = status(request);
            response.event = payload.event; response.cursor = payload.cursor; response.eventType = payload.eventType;
            response.topic = payload.topic; response.conversation = payload.conversation; response.sender = payload.sender;
            response.title = payload.title; response.text = payload.text; response.unread = payload.unread;
            if (!pipe.send(response)) { detach(); pipe.close(); }
            return;
        }
        if (request.kind == Kind::Conversations)
        {
            if (request.cursor < ChatView::MaxConversations - 1)
            {
                auto& sessions = LLIMModel::instance().mId2SessionMap;
                auto it = sessions.begin();
                const auto offset = (std::min)(request.cursor, static_cast<std::uint64_t>(sessions.size()));
                std::advance(it, static_cast<std::ptrdiff_t>(offset));
                if (it != sessions.end())
                {
                    const auto& session = *it->second;
                    payload.eventType = EventType::Conversation; payload.topic = topicFor(session);
                    payload.conversation = session.mSessionID.asString(); payload.title = boundedText(session.mName, 255);
                    payload.unread = static_cast<std::uint32_t>((std::max)(0, session.mNumUnread));
                }
            }
            auto response = status(request); response.eventType = payload.eventType; response.topic = payload.topic;
            response.conversation = payload.conversation; response.title = payload.title; response.unread = payload.unread;
            if (!pipe.send(response)) { detach(); pipe.close(); }
            return;
        }
        if (request.kind == Kind::MarkRead && request.topic == Topic::Notice)
        { reply(request); return; } // Host badge only; never accept a native offer.
        auto* session = request.conversation.empty() ? nullptr : LLIMModel::instance().findIMSession(LLUUID(request.conversation));
        if (!request.conversation.empty() && (!session || topicFor(*session) != request.topic))
        { reply(request, "This conversation is no longer available. Open it in the character's viewer."); return; }
        if (request.kind == Kind::MarkRead)
        {
            if (session) LLIMModel::instance().sendNoUnreadMessages(session->mSessionID);
            reply(request); return;
        }
        if (session && (!session->mSessionInitialized || !session->mTextIMPossible || !imAllowed(*session)))
        { reply(request, "Conversation is not ready or sending is blocked by restrictions."); return; }
        if (request.kind == Kind::Typing)
        {
            if (session && session->mType == IM_NOTHING_SPECIAL &&
                !(FSData::instance().isSupport(session->mOtherParticipantID) && FSData::instance().isAgentFlag(gAgentID, FSData::NO_SUPPORT)))
                LLIMModel::sendTypingState(session->mSessionID, session->mOtherParticipantID, request.unread != 0);
            reply(request); return;
        }
        if (request.text.empty() || request.text.size() > 1023)
        { reply(request, "Use a message of 1 to 1023 UTF-8 bytes."); return; }
        if (!session)
        {
            if (request.topic != Topic::Nearby) { reply(request, "Select a native conversation first."); return; }
            const auto text = utf8str_to_wstring(request.text);
            FSNearbyChat::sendChatFromViewer(text, text, CHAT_TYPE_NORMAL, false, 0);
        }
        else LLIMModel::sendMessage(request.text, session->mSessionID, session->mOtherParticipantID, session->mType);
        // Accepted by native transport, not proof of remote delivery. Never retry.
        reply(request);
    }
    void releaseInput()
    {
        inputGranted = false;
        if (window) RemovePropW(window, InputLeaseProperty);
        if (window) RemovePropW(window, FocusLeaseProperty);
        gViewerInput.releaseHeldInputs();
        gFocusMgr.setMouseCapture(nullptr);
        gAgent.resetControlFlags(); // Preserves Away, Fly and Mouselook.
        if (LLViewerJoystick::instanceExists()) LLViewerJoystick::instance().setNeedsReset();
        if (LLVoiceClient::instanceExists())
        {
            LLVoiceClient::instance().setUserPTTState(false);
            LLVoiceClient::instance().setMuteMic(true);
        }
        applyVoice();
    }
    bool grantInput()
    {
        if (!window || !SetPropW(window, InputLeaseProperty, reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(host)))) return false;
        if (++focusLease == 0) ++focusLease;
        if (!SetPropW(window, FocusLeaseProperty, reinterpret_cast<HANDLE>(focusLease)))
        { RemovePropW(window, InputLeaseProperty); return false; }
        inputGranted = true;
        applyVoice();
        return true;
    }
    bool unembed(bool restore = true)
    {
        // Revoke only the host anchor for standby. Keep client geometry and
        // borderless style while hidden, avoiding two reshape/clamp cycles on
        // every character switch and preserving unsaved floater positions.
        if (!restore) { parent = nullptr; return true; }
        if (!hostedStyle) { parent = nullptr; return true; }
        SetWindowLongPtrW(window, GWL_STYLE, style);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
        SetWindowPlacement(window, &placement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        parent = nullptr;
        hostedStyle = false;
        return true;
    }
    void fitSurface()
    {
        if (!embedded() || mode != Mode::Active) return;
        const HWND root = IsWindow(parent) ? GetAncestor(parent, GA_ROOT) : nullptr;
        if (!root) return;
        const HWND foreground = GetForegroundWindow();
        DWORD owner = 0; GetWindowThreadProcessId(foreground, &owner);
        // Hide with the controller when minimized or another application is
        // foreground. Native owned dialogs keep their worker visible.
        const bool visible = IsWindowVisible(root) && !IsIconic(root) && (foreground == root || owner == GetCurrentProcessId());
        if (!visible) { if (IsWindowVisible(window)) ShowWindow(window, SW_HIDE); return; }
        RECT target{}, current{}; POINT origin{};
        GetClientRect(parent, &target); ClientToScreen(parent, &origin); GetWindowRect(window, &current);
        if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOWNOACTIVATE);
        const bool changed = current.left != origin.x || current.top != origin.y ||
            current.right - current.left != target.right || current.bottom - current.top != target.bottom;
        if (changed || foreground == root)
            SetWindowPos(window, foreground == root ? HWND_TOP : nullptr, origin.x, origin.y,
                (std::max)(1L, target.right), (std::max)(1L, target.bottom),
                SWP_NOACTIVATE | (foreground == root ? 0 : SWP_NOZORDER));
    }
    void focusHostedClient() const
    {
        if (!ready() || detached || !embedded() || mode != Mode::Active ||
            !inputGranted || promoting || !window ||
            !gViewerWindow || !gViewerWindow->getWindow()) return;
        const HWND root = GetAncestor(parent, GA_ROOT);
        DWORD owner = 0;
        const HWND foreground = GetForegroundWindow();
        if (!root || !GetWindowThreadProcessId(root, &owner) || owner != host ||
            (root != foreground && window != foreground) || !IsWindowVisible(root) || IsIconic(root)) return;
        // The viewer owns a separate native window thread. Its existing API
        // posts SetFocus there, delivering normal focus/IME/timer callbacks.
        // Never synthesize keystrokes or force the application's focus flag.
        static_cast<LLWindowWin32*>(gViewerWindow->getWindow())->focusClientGuarded(foreground);
    }
    bool embed(std::uint64_t value)
    {
        HWND target = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
        DWORD owner = 0;
        if (!window || !IsWindow(target) || !GetWindowThreadProcessId(target, &owner) || owner != host || !ready() || mode != Mode::Active ||
            gViewerWindow->getWindow()->getFullscreen()) return false;
        if (parent == target) return true;
        if (embedded() && !unembed()) return false;
        if (!hostedStyle)
        {
            placement.length = sizeof(placement);
            if (!GetWindowPlacement(window, &placement)) return false;
            style = GetWindowLongPtrW(window, GWL_STYLE); exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        }
        // Keep the surface an unowned top-level window in its own process.
        // No foreign parent/owner that can destroy it if the host exits.
        SetWindowLongPtrW(window, GWL_STYLE, (style & ~(WS_OVERLAPPEDWINDOW | WS_CHILD)) | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(window, GWL_EXSTYLE, (exStyle & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);
        SetLastError(0);
        if (!SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        {
            SetWindowLongPtrW(window, GWL_STYLE, style); SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
            SetWindowPlacement(window, &placement);
            return false;
        }
        parent = target;
        hostedStyle = true;
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        fitSurface();
        focusHostedClient();
        return true;
    }
    bool detach()
    {
        const bool previousGrant = inputGranted;
        releaseInput();
        if (!unembed()) { if (previousGrant) grantInput(); return false; }
        promoting = false; mode = Mode::Active;
        detached = true;
        chatConnection.disconnect(); notificationConnection.disconnect(); events.reset();
        if (window) { EnableWindow(window, TRUE); ShowWindow(window, SW_RESTORE); }
        // Restore only our temporary overrides. Saved values were never changed.
        for (const auto& control : controls)
            if (auto value = gSavedSettings.getControl(control.first))
            {
                const LLSD saved = value->getSaveValue();
                value->setValue(saved.asBoolean() == control.second.saved ? control.second.effective : saved, false);
            }
        controls.clear();
        // These backends were masked, rather than overwriting their preference
        // values. Reapply the ordinary choices now that management has ended.
        if (LLVoiceClient::instanceExists()) LLVoiceClient::setVoiceEnabled(LLVoiceClient::instance().voiceEnabled(true));
        if (FSEventAPIBridge::instanceExists()) FSEventAPIBridge::instance().applyEnabled(gSavedSettings.getBOOL("EnableLocalEventAPIBridge"));
        return true;
    }
    Message status(const Message& request, const std::string& error = {}) const
    {
        Message reply;
        reply.kind = Kind::Status; reply.worker = id; reply.sequence = request.sequence; reply.generation = generation;
        reply.mode = mode; reply.pid = GetCurrentProcessId();
        reply.frames = frames; reply.maintenance = maintenance;
        reply.surface = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
        if (window)
        {
            RECT rect{}; GetClientRect(window, &rect);
            reply.width = static_cast<std::uint32_t>((std::max)(0L, rect.right));
            reply.height = static_cast<std::uint32_t>((std::max)(0L, rect.bottom));
        }
        const auto startup = LLStartUp::getStartupState();
        reply.state = gDisconnected ? State::Disconnected : ready() ? State::Ready :
            startup >= STATE_LOGIN_AUTH_INIT ? State::Connecting : State::Login;
        reply.grid = loginGrid; reply.name = loginName;
        if (ready() && !gAgentID.isNull()) reply.account = gAgentID.asString();
        if (loginRequested && gate == FSSessionWorker::LoginGate::Wait) reply.flags |= LoginPending;
        if (embedded()) reply.flags |= Embedded;
        if (promoting) reply.flags |= Promoting;
        if (inputGranted && gFocusMgr.getAppHasFocus()) reply.flags |= ClientFocused;
        if (economyTrimmed) reply.flags |= EconomyTrimmed;
        if (hostedStyle) reply.flags |= HostedStyle;
        if (voiceAllowed()) reply.flags |= VoiceOwner;
        if (!sharingAllowed()) reply.flags |= ChatRestricted;
        if (!error.empty()) { reply.flags |= Error; reply.detail = error; }
        return reply;
    }
    void reply(const Message& request, const std::string& error = {})
    {
        if (!pipe.send(status(request, error))) { detach(); pipe.close(); }
    }
    void command(const Message& request)
    {
        if (request.worker != id || request.kind == Kind::Status || request.sequence <= sequence)
        { detach(); pipe.close(); return; }
        sequence = request.sequence; lastCommand = GetTickCount64();
        if (request.kind != Kind::Poll && request.generation != generation)
        { reply(request, "Session changed; action was not applied."); return; }
        switch (request.kind)
        {
        case Kind::Poll: reply(request); break;
        case Kind::SetMode:
        {
            const HWND popup = window ? GetLastActivePopup(window) : nullptr;
            if ((request.mode == Mode::Active && !ready()) || !readyApplied || gFocusMgr.focusLocked() ||
                (popup && popup != window && IsWindowVisible(popup)) || LLFloaterReg::instanceVisible("preferences"))
            { reply(request, "Close Preferences or the modal dialog, or wait for login before switching."); break; }
            const bool previousGrant = inputGranted;
            releaseInput();
            const Mode previousMode = mode;
            mode = request.mode;
            if (mode != Mode::Active)
            {
                if (!unembed(false))
                {
                    mode = previousMode;
                    if (previousGrant) grantInput();
                    reply(request, "Unable to detach the hosted window; finish in this viewer before switching."); break;
                }
                EnableWindow(window, FALSE); ShowWindow(window, SW_HIDE);
                reply(request);
            }
            else
            {
                // Fit the target before preparing first frames. This also lets
                // rendering recognize host chat/selector focus as app activity.
                if (request.surface && !embed(request.surface))
                {
                    mode = previousMode; reply(request, "Hosted preparation failed. Disable hosting and try the separate window."); break;
                }
                promoting = true; pendingPromotion = request; promotionStart = frames;
                if (gKeyboard)
                    for (std::size_t key = 0; key < heldKeys.size(); ++key)
                    {
                        const auto native = gKeyboard->inverseTranslateKey(static_cast<KEY>(key));
                        heldKeys[key] = native < 256 && (GetAsyncKeyState(static_cast<int>(native)) & 0x8000) != 0;
                    }
                EnableWindow(window, FALSE); ShowWindow(window, SW_RESTORE);
                // Reply/grant input only after an actual normal buffer swap.
            }
            break;
        }
        case Kind::PermitLogin:
        case Kind::DenyLogin:
            if (!loginRequested || request.grid != loginGrid || request.name != loginName || gate != FSSessionWorker::LoginGate::Wait)
            { reply(request, "Login request changed."); break; }
            gate = request.kind == Kind::PermitLogin ? FSSessionWorker::LoginGate::Allow : FSSessionWorker::LoginGate::Deny;
            reply(request); break;
        case Kind::Embed:
            reply(request, embed(request.surface) ? "" : "Window hosting is unavailable; use the separate viewer window."); break;
        case Kind::Unembed: reply(request, unembed() ? "" : "Unable to detach this window. Close the viewer before its controller."); break;
        case Kind::Focus: focusHostedClient(); reply(request); break;
        case Kind::Events:
        case Kind::SendChat:
        case Kind::MarkRead:
        case Kind::Conversations:
        case Kind::Typing: chatCommand(request); break;
        case Kind::AudioPolicy:
            if (request.unread > 3) { reply(request, "Unsupported audio policy."); break; }
            permitVoice = (request.unread & 1) != 0; muteBackground = (request.unread & 2) != 0;
            applyVoice(); reply(request); break;
        case Kind::WorkspaceInfo:
        case Kind::WorkspaceMenu:
        {
            if (!ready() || request.account != gAgentID.asString() || request.grid != loginGrid)
            { reply(request, "Workspace session changed or is not ready."); break; }
            auto& workspaces = FSWorkspaceController::instance();
            if (request.kind == Kind::WorkspaceMenu)
            {
                if (!inputGranted || mode != Mode::Active || promoting || !workspaces.canQuickSwitch())
                { reply(request, "Switch to this character and close Preferences before managing its workspaces."); break; }
                LLFloaterReg::showInstance("workspace_switch", LLSD(), true); reply(request); break;
            }
            auto response = status(request);
            if (workspaces.available() && sharingAllowed())
            {
                response.title = boundedText(workspaces.activeId(), 255);
                response.text = boundedText(workspaces.status(), 3071, true);
                response.unread = 4u | (workspaces.modified() ? 1u : 0u) | (LLFloaterReg::instanceVisible("preferences") ? 2u : 0u);
            }
            if (!pipe.send(response)) { detach(); pipe.close(); }
            break;
        }
        case Kind::Detach:
            reply(request, detach() ? "" : "Unable to detach this window. Close the viewer before its controller."); break;
        case Kind::Quit:
            if (!detach()) { reply(request, "Unable to detach this window before logout."); break; }
            reply(request); LLAppViewer::instance()->userQuit(); break;
        default: detach(); pipe.close(); break;
        }
    }
};
std::unique_ptr<Worker> worker;
std::wstring env(const wchar_t* name)
{
    wchar_t buffer[128]{};
    const DWORD size = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return size && size < std::size(buffer) ? std::wstring(buffer, size) : std::wstring();
}
}

bool FSSessionWorker::initialize()
{
    const auto inherited = env(L"FOLDERSTORM_SESSION_PIPE");
    const auto identity = env(L"FOLDERSTORM_SESSION_ID");
    const auto lease = env(L"FOLDERSTORM_SESSION_LOCK");
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_PIPE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_ID", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_LOCK", nullptr);
    if (inherited.empty() && identity.empty() && lease.empty()) return true;
    if (inherited.empty() || identity.size() != 32 || lease.empty()) return false;
    auto value = std::make_unique<Worker>();
    for (std::size_t i = 0; i < 16; ++i)
    {
        const auto digit = [](wchar_t c) -> int { return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1; };
        const int high = digit(identity[i * 2]), low = digit(identity[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        value->id[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    wchar_t* end = nullptr;
    const auto number = _wcstoui64(inherited.c_str(), &end, 16);
    if (!number || !end || *end || number > (std::numeric_limits<std::uintptr_t>::max)()) return false;
    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(number));
    end = nullptr;
    const auto lockNumber = _wcstoui64(lease.c_str(), &end, 16);
    if (!lockNumber || !end || *end || lockNumber > (std::numeric_limits<std::uintptr_t>::max)()) return false;
    const HANDLE lockHandle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(lockNumber));
    if (GetFileType(lockHandle) != FILE_TYPE_DISK || !SetHandleInformation(lockHandle, HANDLE_FLAG_INHERIT, 0)) return false;
    value->profileLease = lockHandle;
    if (!GetNamedPipeServerProcessId(handle, &value->host) || !value->host || !value->pipe.open(handle)) return false;
    value->lastCommand = GetTickCount64();
    worker = std::move(value);
    return true;
}
bool FSSessionWorker::managed() { return worker && !worker->detached; }
void FSSessionWorker::configure()
{
    if (!managed()) return;
    gNonInteractive = false;
    const std::pair<const char*, bool> changes[] = {
        {"AllowMultipleViewers", true}, {"SLURLPassToOtherInstance", false}, {"AutoLogin", false},
        {"HeadlessClient", false}, {"NonInteractive", false}};
    for (const auto& change : changes)
        if (auto control = gSavedSettings.getControl(change.first))
        {
            worker->controls.emplace(change.first, Worker::Override{control->getValue(), control->getSaveValue().asBoolean()});
            control->setValue(LLSD(change.second), false);
        }
}
bool FSSessionWorker::temporaryControl(const std::string& name)
{
    return managed() && worker->controls.find(name) != worker->controls.end();
}
void FSSessionWorker::tick()
{
    if (!managed()) return;
    auto& value = *worker;
    ++value.maintenance;
    if (gViewerWindow && gViewerWindow->getWindow()) value.window = static_cast<HWND>(gViewerWindow->getWindow()->getPlatformWindow());
    if (value.session != gAgentSessionID)
    {
        const bool wasPromoting = value.promoting;
        const bool wasReady = value.readyApplied;
        value.releaseInput(); value.unembed();
        if (wasReady && value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
        value.session = gAgentSessionID; ++value.generation;
        value.events.reset();
        value.promoting = false; value.mode = Mode::Warm; value.readyApplied = false;
        if (wasPromoting) value.reply(value.pendingPromotion, "Session changed before the character was ready.");
    }
    if (gDisconnected && value.readyApplied && value.mode == Mode::Active)
    {
        value.releaseInput(); value.unembed(); value.mode = Mode::Warm;
        if (value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
        if (value.promoting)
        {
            value.promoting = false;
            value.reply(value.pendingPromotion, "Character disconnected before switching completed.");
        }
    }
    if (gKeyboard)
        for (std::size_t key = 0; key < value.heldKeys.size(); ++key)
            if (value.heldKeys[key])
            {
                const auto native = gKeyboard->inverseTranslateKey(static_cast<KEY>(key));
                if ((GetAsyncKeyState(static_cast<int>(native)) & 0x8000) == 0 && !gKeyboard->getKeyDown(static_cast<KEY>(key))) value.heldKeys[key] = false;
            }
    if (LLStartUp::getStartupState() < STATE_LOGIN_AUTH_INIT)
    {
        value.loginRequested = false; value.gate = LoginGate::Wait;
        value.loginName.clear(); value.loginGrid.clear();
    }
    if (value.ready() && !value.readyApplied)
    {
        value.readyApplied = true;
        value.releaseInput();
        if (value.mode != Mode::Active && value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
    }
    value.fitSurface();
    if (value.embedded() && (!IsWindow(value.parent) || gViewerWindow->getWindow()->getFullscreen())) value.unembed();
    if (value.ready())
    {
        value.connectChat();
        const bool blocked = !value.sharingAllowed();
        if (blocked != value.chatBlocked) { value.events.reset(); value.chatBlocked = blocked; }
    }
    Message request;
    // A pending promotion owns its response slot until first frame/timeout.
    if (!value.promoting && !value.pipe.writing() && value.pipe.receive(request)) value.command(request);
    if (!value.pipe.alive() || GetTickCount64() - value.lastCommand > 15000)
    {
        if (!value.detach()) { value.releaseInput(); value.mode = Mode::Warm; value.promoting = false; }
        value.pipe.close();
    }
}
bool FSSessionWorker::renderAllowed()
{
    return !managed() || !worker->readyApplied || worker->mode == Mode::Active;
}
bool FSSessionWorker::hostForeground()
{
    return managed() && worker->readyApplied && worker->mode == Mode::Active && worker->hostForeground();
}
void FSSessionWorker::prepareDisplay()
{
    if (!worker || !gPipeline.isInit()) return;
    auto& value = *worker;
    if (managed() && value.readyApplied && value.mode == Mode::Economy && !value.promoting)
    {
        // Only disposable screen/shadow targets. Scene objects, textures,
        // shader programs, avatar state and live network work remain intact.
        // Recheck each display: graphics/DPI callbacks may allocate new targets.
        gPipeline.releaseScreenBuffers();
        gPipeline.releaseShadowBuffers();
        gResizeScreenTexture = true;
        gResizeShadowTexture = true;
        value.economyTrimmed = true;
    }
    else if (value.economyTrimmed)
    {
        // The ordinary allocator honors the worker's current graphics choices,
        // including settings changed in standby. Never save a quality downgrade.
        gPipeline.resizeScreenTexture();
        value.economyTrimmed = false;
    }
}
bool FSSessionWorker::inputAllowed()
{
    return !managed() || !worker->readyApplied || (worker->mode == Mode::Active && worker->inputGranted && !worker->promoting);
}
bool FSSessionWorker::voiceAllowed() { return !managed() || worker->voiceAllowed(); }
bool FSSessionWorker::backgroundAudioMuted()
{
    return managed() && worker->readyApplied && worker->mode != Mode::Active && worker->muteBackground;
}
void FSSessionWorker::nearbyMessage(const LLChat& chat)
{
    if (!managed() || !worker->ready() || !worker->sharingAllowed() || chat.mMuted ||
        chat.mChatType == CHAT_TYPE_IM || chat.mChatType == CHAT_TYPE_IM_GROUP ||
        chat.mChatStyle == CHAT_STYLE_HISTORY || chat.mChatStyle == CHAT_STYLE_SERVER_HISTORY) return;
    Message message; message.eventType = EventType::Chat; message.topic = Topic::Nearby;
    message.title = "Nearby chat"; message.sender = chat.mFromName; message.text = chat.mText;
    worker->events.push(std::move(message));
}
bool FSSessionWorker::keyAllowed(unsigned int key)
{
    return inputAllowed() && (!managed() || key >= worker->heldKeys.size() || !worker->heldKeys[key]);
}
void FSSessionWorker::keyReleased(unsigned int key)
{
    if (managed() && key < worker->heldKeys.size()) worker->heldKeys[key] = false;
}
bool FSSessionWorker::textAllowed()
{
    return inputAllowed() && (!managed() || std::none_of(worker->heldKeys.begin(), worker->heldKeys.end(), [](bool held) { return held; }));
}
void FSSessionWorker::focusHostedClient()
{
    if (managed()) worker->focusHostedClient();
}
int FSSessionWorker::backgroundYield(int normal)
{
    // Standby services the main loop frequently, never at a preview frame rate.
    return managed() && worker->ready() && worker->mode != Mode::Active ? (std::max)(1, (std::min)(normal, 40)) : normal;
}
void FSSessionWorker::framePresented()
{
    if (!managed()) return;
    ++worker->frames;
    if (worker->promoting && worker->ready() && worker->frames - worker->promotionStart >= 2)
    {
        worker->promoting = false;
        if (!worker->grantInput())
        {
            worker->mode = Mode::Warm;
            worker->reply(worker->pendingPromotion, "Unable to establish this window's input owner.");
            return;
        }
        EnableWindow(worker->window, TRUE);
        worker->reply(worker->pendingPromotion);
    }
}
FSSessionWorker::LoginGate FSSessionWorker::loginGate(const std::string& grid, const std::string& name)
{
    if (!managed()) return LoginGate::Allow;
    if (grid.empty() || name.empty() || grid.size() >= 128 || name.size() >= 128 || !validUtf8(grid) || !validUtf8(name)) return LoginGate::Deny;
    if (!worker->loginRequested || worker->loginGrid != grid || worker->loginName != name)
    {
        worker->loginGrid = grid; worker->loginName = name;
        worker->loginRequested = true; worker->gate = LoginGate::Wait;
    }
    return worker->gate;
}
void FSSessionWorker::shutdown()
{
    if (worker)
    {
        worker->inputGranted = false;
        if (worker->window) RemovePropW(worker->window, InputLeaseProperty);
        if (worker->window) RemovePropW(worker->window, FocusLeaseProperty);
        worker->pipe.close(); worker->detached = true;
    }
    // Keep the inherited profile lease until process/static teardown, after
    // ordinary viewer cleanup has finished saving account/settings files.
}
#else
bool FSSessionWorker::initialize() { return true; }
bool FSSessionWorker::managed() { return false; }
void FSSessionWorker::configure() {}
bool FSSessionWorker::temporaryControl(const std::string&) { return false; }
void FSSessionWorker::tick() {}
bool FSSessionWorker::renderAllowed() { return true; }
bool FSSessionWorker::hostForeground() { return false; }
void FSSessionWorker::prepareDisplay() {}
bool FSSessionWorker::inputAllowed() { return true; }
bool FSSessionWorker::voiceAllowed() { return true; }
bool FSSessionWorker::backgroundAudioMuted() { return false; }
void FSSessionWorker::nearbyMessage(const LLChat&) {}
bool FSSessionWorker::keyAllowed(unsigned int) { return true; }
void FSSessionWorker::keyReleased(unsigned int) {}
bool FSSessionWorker::textAllowed() { return true; }
void FSSessionWorker::focusHostedClient() {}
int FSSessionWorker::backgroundYield(int normal) { return normal; }
void FSSessionWorker::framePresented() {}
FSSessionWorker::LoginGate FSSessionWorker::loginGate(const std::string&, const std::string&) { return LoginGate::Allow; }
void FSSessionWorker::shutdown() {}
#endif
