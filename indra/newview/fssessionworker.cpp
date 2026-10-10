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
#include "fssessionlifecycle.h"
#include "fssessionframepipe.h"
#include "fssessiontransition.h"
#include "fssessionusability.h"
#include "fssessionprofileseed.h"
#include "llsdserialize.h"
#include "llxmlnode.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "llimage.h"
#include "llgl.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "fseventapibridge.h"
#include "fsworkspacecontroller.h"
#include "fsworkspacecontextadapter.h"
#include "llagent.h"
#include "llagentdata.h"
#include "llagentui.h"
#include "llchat.h"
#include "llimview.h"
#include "llnotifications.h"
#include "llchannelmanager.h"
#include "llscreenchannel.h"
#include "llscriptfloater.h"
#include "llspeakers.h"
#include "fsnearbychathub.h"
#include "fsdata.h"
#include "fsfloaterim.h"
#include "llmutelist.h"
#include "llcallingcard.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include "llappviewer.h"
#include "llfocusmgr.h"
#include "lluictrl.h"
#include <imm.h>
#include "llfloaterreg.h"
#include "llkeyboard.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
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
#include <cmath>

namespace
{
using namespace fs_session;
struct Worker
{
    Pipe pipe;
    FrameLane preview;
    PreviewSize previewCap{};
    std::uint32_t previewRate = 0;
    std::uint64_t previewSequence = 0;
    ULONGLONG previewAt = 0;
    bool previewPass = false;
    std::string previewError;
    std::wstring settingsSource;
    std::string settingsNotice;
    TransitionClock transition;
    unsigned int transitionStyle = 0, transitionHeight = 64;
    bool demoting = false, escapeHeld = false;
    Message pendingDemotion;
    LLUUID transitionRegion;
    std::string transitionNotice;
    LLCamera cameraBefore;
    bool cameraOverridden = false;
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
    bool economyTrimmed = false, recoveryShown = false;
    bool hostedStyle = false;
    std::uintptr_t focusLease = 0, restartTag = 0;
    bool permitVoice = false, muteBackground = true, chatBlocked = false, shortcuts = false;
    EventBuffer events;
    std::map<std::string,std::string> attention;
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
    bool appForeground() const
    {
        DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == host || pid == GetCurrentProcessId();
    }
    bool cameraAllowed() const
    {
        const HWND popup = window ? GetLastActivePopup(window) : nullptr;
        return ready() && gViewerWindow && window && IsWindowVisible(window) && !IsIconic(window) && appForeground() &&
            !gTeleportDisplay && gAgent.getTeleportState() == LLAgent::TELEPORT_NONE && gAgent.getRegion() &&
            sharingAllowed() && FSWorkspaceContext::cameraAllowed() && !gFocusMgr.focusLocked() &&
            (!popup || popup == window || !IsWindowVisible(popup)) &&
            !LLFloaterReg::instanceVisible("preferences") &&
            (!LLViewerJoystick::instanceExists() || !LLViewerJoystick::instance().getOverrideCamera());
    }
    void startTransition(const Message& request)
    {
        transition.cancel(); transitionNotice.clear();
        if (!request.unread) return;
        BOOL animation = TRUE;
        const bool reduced = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) && !animation;
        // A hidden standby target is made visible before calling this helper.
        if (reduced || !cameraAllowed() || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
        { transitionNotice = "Bird's-eye transition skipped: camera, restrictions, focus or reduced-motion setting."; return; }
        transitionRegion = gAgent.getRegion()->getRegionID();
        transitionStyle = request.unread; transitionHeight = request.height;
        escapeHeld = false; transition.begin(GetTickCount64(), generation,request.width);
    }
    void finishDemotion()
    {
        transition.cancel(); demoting = false;
        if (!unembed(false))
        {
            EnableWindow(window, TRUE); grantInput();
            reply(pendingDemotion, "Unable to detach the hosted window; finish in this viewer before switching."); return;
        }
        mode = pendingDemotion.mode;
        EnableWindow(window, FALSE); ShowWindow(window, SW_HIDE);
        reply(pendingDemotion);
    }
    bool hostForeground() const
    {
        if (!parent || !IsWindow(parent)) return false;
        const HWND root = GetAncestor(parent, GA_ROOT);
        const HWND foreground = GetForegroundWindow();
        return root && (foreground == root || GetAncestor(foreground, GA_ROOTOWNER) == root) && IsWindowVisible(root) && !IsIconic(root);
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
    bool backgroundAlertAllowed(const LLUUID& sender) const
    {
        return ready() && mode != Mode::Active && !promoting && !demoting && muteBackground && sharingAllowed() &&
            !gAgent.isDoNotDisturb() && !gSavedSettings.getBOOL("MuteAudio") && !gSavedSettings.getBOOL("MuteUI") &&
            gSavedSettings.getF32("AudioLevelUI") > 0.f && !sender.isNull() && sender != gAgentID &&
            !LLMuteList::instance().isMuted(sender,LLMute::flagTextChat);
    }
    void connectChat()
    {
        if (!chatConnection.connected())
            chatConnection = LLIMModel::instance().addNewMsgCallback([this](const LLSD& data)
            {
                if (detached || !ready() || !sharingAllowed()) return;
                auto* session = LLIMModel::instance().findIMSession(data["session_id"].asUUID());
                const LLUUID sender = data["from_id"].asUUID();
                if (!session || (session->isGroupSessionType() && !RlvActions::canReceiveIM(session->mSessionID)) || (!sender.isNull() && sender != gAgentID && !RlvActions::canReceiveIM(sender))) return;
                Message message; message.eventType = EventType::Chat; message.topic = topicFor(*session);
                message.conversation = session->mSessionID.asString(); message.title = session->mName;
                message.sender = data["from"].asString(); message.recipient = sender.isNull() ? "" : sender.asString();
                message.text = data["message"].asString(); message.unread = static_cast<std::uint32_t>((std::max)(0, session->mNumUnread));
                const std::string sound = session->isGroupSessionType() ? "PlaySoundGroupChatIM" : session->isAdHocSessionType() ? "PlaySoundConferenceIM" :
                    LLAvatarTracker::instance().isBuddy(sender) ? "PlaySoundFriendIM" : "PlaySoundNonFriendIM";
                if (backgroundAlertAllowed(sender) && gSavedSettings.getBOOL(sound) && !data["is_region_msg"].asBoolean())
                { message.flags |= AlertEligible; message.eventAt = GetTickCount64(); }
                events.push(std::move(message));
            });
        if (!notificationConnection.connected())
            if (auto channel = LLNotifications::instance().getChannel("Visible"))
                notificationConnection = channel->connectChanged([this](const LLSD& data)
                {
                    if (detached || !ready() || !sharingAllowed()) return false;
                    const LLUUID id = data["id"].asUUID(); if (id.isNull()) return false;
                    const auto key = id.asString(); const auto signal = data["sigtype"].asString();
                    const auto notification = LLNotifications::instance().find(id);
                    const auto category = notification ? attentionCategory(notification->getName()) : std::string{};
                    if (signal == "delete" || !notification || !notification->isActive())
                    {
                        if (attention.erase(key))
                        { Message item; item.eventType = EventType::AttentionRemoved; item.topic = Topic::Notice; item.conversation = key; events.push(item); }
                    }
                    else if (!category.empty() && attention.size() < AttentionBook::Capacity)
                    {
                        attention[key] = category;
                        Message item; item.eventType = EventType::Attention; item.topic = Topic::Notice; item.conversation = key;
                        item.title = category; item.text = "Review in this character's native viewer.";
                        LLUUID sender = notification->getPayload()["from_id"].asUUID();
                        if (sender.isNull()) sender = notification->getPayload()["owner_id"].asUUID();
                        if (sender.isNull()) sender = notification->getPayload()["task_id"].asUUID();
                        if (signal == "add" && backgroundAlertAllowed(sender)) { item.flags |= AlertEligible; item.eventAt = GetTickCount64(); }
                        events.push(item);
                    }
                    else if (signal == "add")
                    {
                        Message item; item.eventType = EventType::Notice; item.topic = Topic::Notice;
                        item.conversation = "00000000-0000-0000-0000-000000000000"; item.title = "Viewer notifications";
                        item.text = "Review this notification in this character's viewer."; item.unread = 1; events.push(item);
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
            for (auto it = attention.begin(); it != attention.end(); )
            {
                const auto notification = LLNotifications::instance().find(LLUUID(it->first));
                if (!notification || !notification->isActive())
                { Message item; item.eventType = EventType::AttentionRemoved; item.topic = Topic::Notice; item.conversation = it->first; events.push(item); it = attention.erase(it); }
                else ++it;
            }
            payload = events.after(request.cursor);
            if (payload.topic != Topic::Nearby && payload.topic != Topic::Notice &&
                !payload.recipient.empty() && payload.recipient != gAgentID.asString() && !RlvActions::canReceiveIM(LLUUID(payload.recipient)))
            { payload.text.clear(); payload.sender.clear(); payload.eventType = EventType::Gap; payload.flags |= ConversationRestricted; }
            auto response = status(request);
            response.event = payload.event; response.cursor = payload.cursor; response.eventType = payload.eventType;
            response.topic = payload.topic; response.conversation = payload.conversation; response.sender = payload.sender;
            response.title = payload.title; response.text = payload.text; response.unread = payload.unread;
            response.recipient = payload.recipient; response.flags |= payload.flags & (AlertEligible|ConversationRestricted); response.eventAt = payload.eventAt;
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
                    if (session.isP2PSessionType()) payload.recipient = session.mOtherParticipantID.asString();
                    else if (session.isGroupSessionType()) payload.recipient = session.mSessionID.asString();
                    if (!payload.recipient.empty() && !RlvActions::canReceiveIM(LLUUID(payload.recipient)))
                    { payload.title = "Restricted conversation"; payload.recipient.clear(); payload.unread = 0; payload.flags |= ConversationRestricted; }
                }
            }
            auto response = status(request); response.eventType = payload.eventType; response.topic = payload.topic;
            response.conversation = payload.conversation; response.title = payload.title; response.unread = payload.unread; response.recipient = payload.recipient; response.flags |= payload.flags & ConversationRestricted;
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
        if (session && !FSData::instance().canSendToIMContact(session->mType, gAgentID, session->mOtherParticipantID))
        { reply(request, "This account is not permitted to contact this support user."); return; }
        if (request.kind == Kind::Typing)
        {
            if (session && session->mType == IM_NOTHING_SPECIAL)
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
        // Retain the view behind other applications. Stack immediately above
        // the host, below its popups and anything already above the host.
        if (!IsWindowVisible(root) || IsIconic(root))
        { if (IsWindowVisible(window)) ShowWindow(window, SW_HIDE); return; }
        RECT target{}, current{}; POINT origin{};
        GetClientRect(parent, &target); ClientToScreen(parent, &origin); GetWindowRect(window, &current);
        if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOWNOACTIVATE);
        const bool changed = current.left != origin.x || current.top != origin.y ||
            current.right - current.left != target.right || current.bottom - current.top != target.bottom;
        HWND above = GetWindow(root, GW_HWNDPREV);
        const bool restack = above != window;
        if (above && (GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST)) above = HWND_TOP;
        if (changed || restack)
            SetWindowPos(window, above ? above : HWND_TOP, origin.x, origin.y,
                (std::max)(1L, target.right), (std::max)(1L, target.bottom),
                SWP_NOACTIVATE | (restack ? 0 : SWP_NOZORDER));
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
        static_cast<LLWindowWin32*>(gViewerWindow->getWindow())->focusClientGuarded(foreground, root);
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
        return true;
    }
    bool detach()
    {
        const bool previousGrant = inputGranted;
        releaseInput();
        if (!unembed()) { if (previousGrant) grantInput(); return false; }
        transition.cancel(); demoting = false; promoting = false; mode = Mode::Active;
        detached = true;
        previewCap = {}; previewRate = 0;
        chatConnection.disconnect(); notificationConnection.disconnect(); events.clear();
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
        if ((ready() || gDisconnected) && !gAgentID.isNull()) reply.account = gAgentID.asString();
        if (loginRequested && gate == FSSessionWorker::LoginGate::Wait) reply.flags |= LoginPending;
        if (embedded()) reply.flags |= Embedded;
        if (promoting || demoting) reply.flags |= Promoting;
        if (transition.active()) reply.flags |= Transitioning;
        if (inputGranted && gFocusMgr.getAppHasFocus()) reply.flags |= ClientFocused;
        if (economyTrimmed) reply.flags |= EconomyTrimmed;
        if (hostedStyle) reply.flags |= HostedStyle;
        if (voiceAllowed()) reply.flags |= VoiceOwner;
        if (!sharingAllowed()) reply.flags |= ChatRestricted;
        if (!settingsNotice.empty() && error.empty()) reply.detail = boundedText(settingsNotice,119);
        if (!previewError.empty())
        {
            reply.flags |= PreviewUnavailable;
            if (error.empty()) reply.detail = boundedText(previewError, 119);
        }
        if (!transitionNotice.empty() && error.empty()) reply.detail = transitionNotice;
        if (!error.empty()) { reply.flags |= Error; reply.detail = error; }
        return reply;
    }
    void reply(const Message& request, const std::string& error = {})
    {
        const auto response = status(request,error);
        if (!pipe.send(response)) { detach(); pipe.close(); }
        else if (response.detail.rfind("Settings import:",0) == 0) settingsNotice.clear();
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
            if (request.unread > 2 || (request.unread && (request.width < 250 || request.width > 2000 || request.height < 16 || request.height > 96)) || request.account != gAgentID.asString() || request.grid != loginGrid)
            { reply(request, "Invalid transition policy or changed account."); break; }
            const HWND popup = window ? GetLastActivePopup(window) : nullptr;
            if ((request.mode == Mode::Active && !ready()) || !readyApplied || gFocusMgr.focusLocked() ||
                (popup && popup != window && IsWindowVisible(popup)) || LLFloaterReg::instanceVisible("preferences"))
            { reply(request, "Close Preferences or the modal dialog, or wait for login before switching."); break; }
            const bool previousGrant = inputGranted;
            releaseInput();
            const Mode previousMode = mode;
            if (request.mode != Mode::Active && previousMode == Mode::Active)
            {
                startTransition(request);
                if (transition.active())
                {
                    pendingDemotion = request; demoting = true;
                    EnableWindow(window, FALSE); break;
                }
            }
            else { transition.cancel(); transitionNotice.clear(); }
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
                EnableWindow(window, FALSE); ShowWindow(window, request.surface ? SW_SHOWNOACTIVATE : SW_RESTORE);
                startTransition(request);
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
        case Kind::Unembed:
            if (promoting || demoting || gFocusMgr.focusLocked() || LLFloaterReg::instanceVisible("preferences"))
                reply(request, "Finish the switch or close the modal/Preferences window before disabling hosting.");
            else reply(request, unembed() ? "" : "Unable to restore the separate window.");
            break;
        case Kind::Focus: focusHostedClient(); reply(request); break;
        case Kind::Events:
        case Kind::SendChat:
        case Kind::MarkRead:
        case Kind::Conversations:
        case Kind::Typing: chatCommand(request); break;
        case Kind::ShortcutPolicy:
            if (!ready() || request.unread > 1 || request.account != gAgentID.asString() || request.grid != loginGrid)
            { reply(request,"Shortcut policy is invalid or its account changed."); break; }
            shortcuts = request.unread != 0; reply(request); break;
        case Kind::AudioPolicy:
            if (request.unread > 3) { reply(request, "Unsupported audio policy."); break; }
            permitVoice = (request.unread & 1) != 0; muteBackground = (request.unread & 2) != 0;
            applyVoice(); reply(request); break;
        case Kind::ReviewChat:
        {
            if (!ready() || !inputGranted || mode != Mode::Active || request.account != gAgentID.asString() || request.grid != loginGrid || !sharingAllowed())
            { reply(request,"The owning character is unavailable or restricted."); break; }
            auto* session = request.conversation.empty() ? nullptr : LLIMModel::instance().findIMSession(LLUUID(request.conversation));
            if (request.topic != Topic::Nearby && request.topic != Topic::Notice && (!session || topicFor(*session) != request.topic ||
                (session->isP2PSessionType() && !RlvActions::canReceiveIM(session->mOtherParticipantID)) ||
                (session->isGroupSessionType() && !RlvActions::canReceiveIM(session->mSessionID))))
            { reply(request,"The native conversation ended or is restricted."); break; }
            if (!appForeground()) { reply(request,"Return to Folderstorm to review this conversation."); break; }
            SetForegroundWindow(window);
            if (request.topic == Topic::Nearby) LLFloaterReg::showInstance("fs_nearby_chat",LLSD(),true);
            else if (request.topic == Topic::Notice) LLFloaterReg::showInstance("notification_well_window",LLSD(),true);
            else FSFloaterIM::show(session->mSessionID);
            reply(request); break;
        }
        case Kind::ReviewAttention:
        {
            if (!ready() || !inputGranted || mode != Mode::Active || request.account != gAgentID.asString() || request.grid != loginGrid || !sharingAllowed())
            { reply(request,"Switch to this character before reviewing its notification."); break; }
            const auto notification = LLNotifications::instance().find(LLUUID(request.conversation));
            if (!notification || !notification->isActive() || attentionCategory(notification->getName()).empty())
            { reply(request,"Notification expired or is no longer supported. Check native notifications."); break; }
            // Open only: the native notification UI still owns all response buttons.
            if (!appForeground()) { reply(request,"Return to Folderstorm to review this notification."); break; }
            SetForegroundWindow(window);
            const LLUUID notificationId(request.conversation);
            auto* channel = dynamic_cast<LLNotificationsUI::LLScreenChannel*>(LLNotificationsUI::LLChannelManager::getInstance()->findChannelByID(LLNotificationsUI::NOTIFICATION_CHANNEL_UUID));
            if (channel && channel->getToastByNotificationID(notificationId)) LLFloaterReg::showInstance("inspect_toast",LLSD(notificationId),true);
            else
            {
                LLScriptFloaterManager::instance().setFloaterVisible(notificationId,true);
                LLFloaterReg::showInstance("notification_well_window",LLSD(),true);
            }
            reply(request); break;
        }
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
        case Kind::MonitorPolicy:
            if (!ready() || request.account != gAgentID.asString() || request.grid != loginGrid ||
                !previewPolicy(request.width, request.height, request.unread))
            { reply(request, "Invalid monitor size/rate or changed session."); break; }
            previewCap = {request.width, request.height}; previewRate = request.unread; previewAt = 0; previewError.clear();
            preview.publish(status(request), nullptr, 0); reply(request); break;
        case Kind::Detach:
            reply(request, detach() ? "" : "Unable to detach this window. Close the viewer before its controller."); break;
        case Kind::Quit:
            if (request.unread > 1 || request.account != status(request).account || request.grid != loginGrid)
            { reply(request, "Close target changed; the new session was not closed."); break; }
            if (request.unread == 1)
            {
                restartTag = static_cast<std::uintptr_t>(request.sequence); if (!restartTag) restartTag = 1;
                RemovePropW(window, L"FolderstormRestartCancelled");
            }
            if (!detach()) { reply(request, "Unable to detach this window before logout."); break; }
            reply(request); LLAppViewer::instance()->userQuit(); break;
        default: detach(); pipe.close(); break;
        }
    }
};
std::unique_ptr<Worker> worker;
std::wstring env(const wchar_t* name, std::size_t limit = 128)
{
    std::vector<wchar_t> buffer(limit,0);
    const DWORD size = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    return size && size < buffer.size() ? std::wstring(buffer.data(), size) : std::wstring();
}
}

bool FSSessionWorker::initialize()
{
    const auto inherited = env(L"FOLDERSTORM_SESSION_PIPE");
    const auto identity = env(L"FOLDERSTORM_SESSION_ID");
    const auto lease = env(L"FOLDERSTORM_SESSION_LOCK");
    const auto frames = env(L"FOLDERSTORM_SESSION_FRAMES");
    const auto frameMutex = env(L"FOLDERSTORM_SESSION_FRAME_MUTEX");
    const auto settingsSource = env(L"FOLDERSTORM_SESSION_SETTINGS_SOURCE",32768);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_PIPE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_ID", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_LOCK", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_FRAMES", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_FRAME_MUTEX", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_SETTINGS_SOURCE", nullptr);
    if (inherited.empty() && identity.empty() && lease.empty() && frames.empty() && frameMutex.empty() && settingsSource.empty()) return true;
    if (inherited.empty() || identity.size() != 32 || lease.empty() || frames.empty() || frameMutex.empty()) return false;
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
    const auto frameHandle = [](const std::wstring& text) -> HANDLE
    {
        wchar_t* tail = nullptr; const auto number = _wcstoui64(text.c_str(), &tail, 16);
        return number && tail && !*tail && number <= (std::numeric_limits<std::uintptr_t>::max)() ?
            reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(number)) : INVALID_HANDLE_VALUE;
    };
    if (!value->preview.open(frameHandle(frames), frameHandle(frameMutex)) || !value->preview.bootstrap(value->id, value->host)) return false;
    value->settingsSource = settingsSource;
    value->lastCommand = GetTickCount64();
    worker = std::move(value);
    return true;
}
bool FSSessionWorker::managed() { return worker && !worker->detached; }
void FSSessionWorker::importProfileSettings()
{
    if (!managed() || worker->settingsSource.empty()) return;
    const auto folder = std::filesystem::path(worker->settingsSource);
    worker->settingsSource.clear(); // One deliberate attempt; never retry at a new login.
    const auto targetName = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"settings.xml");
    const auto target = std::filesystem::path(ll_convert_string_to_wide(targetName,targetName.size(),CP_UTF8));
    const auto destination = target.parent_path();
    const auto plainFile = [](const std::filesystem::path& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    };
    const auto read = [&plainFile](const std::filesystem::path& path,std::string& bytes)
    {
        std::error_code error; if (!plainFile(path)) return false;
        const auto size = std::filesystem::file_size(path,error);
        if (error || !size || size > ProfileSettingsLimit) return false;
        std::ifstream input(path,std::ios::binary); if (!input) return false;
        bytes.assign(static_cast<std::size_t>(size),'\0'); input.read(bytes.data(),static_cast<std::streamsize>(size));
        return input.gcount() == static_cast<std::streamsize>(size) && input.peek() == std::char_traits<char>::eof() &&
            bytes.find("<!DOCTYPE") == std::string::npos && bytes.find("<!ENTITY") == std::string::npos;
    };
    std::string bytes;
    if (!read(folder/L"settings.xml",bytes))
    { worker->settingsNotice = "Settings import: source unavailable, too large or not a plain settings file; preferences unchanged."; return; }
    std::error_code error;
    if (std::filesystem::equivalent(folder/L"settings.xml",target,error))
    { worker->settingsNotice = "Settings import: source is this profile; preferences unchanged."; return; }
    LLSD data; std::istringstream input(bytes);
    if (LLSDSerialize::fromXML(data,input,false) <= 0 || !data.isMap() || data.size() > ProfileSettingsEntries)
    { worker->settingsNotice = "Settings import: invalid or oversized preferences; profile unchanged."; return; }
    const auto validValue = [](const std::string& type,const LLSD& value)
    {
        const auto number = [](const LLSD& v) { return (v.isReal() || v.isInteger()) && std::isfinite(v.asReal()); };
        if (type == "Boolean") return value.isBoolean() || (value.isInteger() && (value.asInteger() == 0 || value.asInteger() == 1));
        if (type == "S32") return value.isInteger();
        if (type == "U32") return value.isInteger() && value.asInteger() >= 0;
        if (type == "F32") return number(value) && std::abs(value.asReal()) <= (std::numeric_limits<F32>::max)();
        if (type == "String") return value.isString() && value.asString().size() <= 4096 && validUtf8(value.asString(),true);
        const std::size_t expected = type == "Vector3" || type == "Vector3D" || type == "Color3" ? 3 : 4;
        if (!value.isArray() || value.size() != expected) return false;
        for (auto it = value.beginArray(); it != value.endArray(); ++it)
            if (!number(*it) || ((type == "Rect" || type == "Color4U") && !it->isInteger()) ||
                (type != "Vector3D" && std::abs(it->asReal()) > (std::numeric_limits<F32>::max)()) ||
                (type == "Color4U" && (it->asInteger() < 0 || it->asInteger() > 255))) return false;
        return true;
    };
    LLSD changes; std::size_t skipped = 0;
    for (auto it = data.beginMap(); it != data.endMap(); ++it)
    {
        auto control = gSavedSettings.getControl(it->first); const auto& record = it->second;
        const auto type = control ? LLControlGroup::typeEnumToString(control->type()) : std::string{};
        if (!control || !record.isMap() || record["Type"].asString() != type || !record.has("Value") ||
            !seedPreference(it->first,type,control->isPersisted(),control->isBackupable(),!record.has("Backup") || record["Backup"].asBoolean()) ||
            !validValue(type,record["Value"])) { ++skipped; continue; }
        changes[it->first] = record["Value"];
    }
    const auto plainDirectory = [](const std::filesystem::path& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    };
    const auto backups = destination/L"settings-import-backups";
    error.clear();
    if (!plainDirectory(destination))
    { worker->settingsNotice = "Settings import: profile settings directory unavailable or redirected; unchanged."; return; }
    std::filesystem::create_directory(backups,error);
    if (error || !plainDirectory(backups))
    { worker->settingsNotice = "Settings import: unable to create preference backup; profile unchanged."; return; }
    const auto backup = backups/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId()));
    // An exclusively new directory holds both originals and staging files; an
    // old or redirected temporary file must never be mistaken for a fresh save.
    if (!std::filesystem::create_directory(backup,error) || error)
    { worker->settingsNotice = "Settings import: unable to create unique preference backup; profile unchanged."; return; }
    const auto backupFile = [&](const std::filesystem::path& file)
    {
        const DWORD attributes = GetFileAttributesW(file.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
        if (!plainFile(file)) return false;
        std::error_code failure;
        return std::filesystem::copy_file(file,backup/file.filename(),std::filesystem::copy_options::none,failure) && !failure;
    };
    if (!backupFile(target))
    { worker->settingsNotice = "Settings import: unable to back up existing preferences; profile unchanged."; return; }
    LLSD previous, effective;
    for (auto it = changes.beginMap(); it != changes.endMap(); ++it)
    {
        auto control = gSavedSettings.getControl(it->first);
        previous[it->first] = control->getSaveValue(); effective[it->first] = control->getValue(); control->setValue(it->second,true);
    }
    const auto temporary = backup/L"settings-import.tmp";
    gSavedSettings.saveToFile(ll_convert_wide_to_string(temporary.wstring()),true);
    LLSD saved;
    bool complete = read(temporary,bytes);
    if (complete)
    {
        std::istringstream written(bytes);
        complete = LLSDSerialize::fromXML(saved,written,false) > 0 && saved.isMap();
    }
    for (auto it = changes.beginMap(); complete && it != changes.endMap(); ++it)
    {
        auto control = gSavedSettings.getControl(it->first);
        if (control->shouldSave(true))
            complete = saved.has(it->first) && saved[it->first]["Value"] == control->getSaveValue();
    }
    if (!complete || !MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        for (auto it = previous.beginMap(); it != previous.endMap(); ++it)
        { auto control = gSavedSettings.getControl(it->first); control->setValue(it->second,true); control->setValue(effective[it->first],false); }
        std::filesystem::remove(temporary,error);
        worker->settingsNotice = "Settings import: preference write failed; original settings retained."; return;
    }
    unsigned int copied = 0;
    for (const auto* name : seedUiFiles())
    {
        const auto source = folder/name;
        if (!std::filesystem::exists(source,error)) { error.clear(); continue; }
        LLXMLNodePtr node;
        if (!read(source,bytes) || !LLXMLNode::parseBuffer(bytes.data(),static_cast<U64>(bytes.size()),node) || !backupFile(destination/name))
        { ++skipped; continue; }
        const auto temp = backup/(std::string(name)+".import-tmp");
        std::ofstream out(temp,std::ios::binary|std::ios::trunc); out.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); out.close();
        const auto file = destination/name;
        if (!out || !MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        { std::filesystem::remove(temp,error); ++skipped; continue; }
        if (std::string(name) == "ignorable_dialogs.xml") gWarningSettings.loadFromFile(ll_convert_wide_to_string(file.wstring()));
        ++copied;
    }
    worker->settingsNotice = "Settings import: "+std::to_string(changes.size())+" preferences, "+std::to_string(copied)+
        " UI files, "+std::to_string(skipped)+" skipped; previous files backed up.";
    LL_INFOS("SessionWorker") << worker->settingsNotice << LL_ENDL;
}
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
        const bool wasPromoting = value.promoting, wasDemoting = value.demoting;
        const bool wasReady = value.readyApplied;
        value.releaseInput(); value.unembed();
        if (wasReady && value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
        value.session = gAgentSessionID; ++value.generation;
        value.events.resetSession(); value.attention.clear(); value.shortcuts = false;
        value.previewCap = {}; value.previewRate = 0; value.previewError.clear();
        value.preview.publish(value.status(Message{}), nullptr, 0);
        value.transition.cancel(); value.transitionNotice.clear(); value.demoting = false;
        value.promoting = false; value.mode = Mode::Warm; value.readyApplied = false; value.recoveryShown = false;
        if (wasPromoting) value.reply(value.pendingPromotion, "Session changed before the character was ready.");
        else if (wasDemoting) value.reply(value.pendingDemotion, "Session changed before switching completed.");
    }
    if (gDisconnected && !value.recoveryShown)
    {
        value.recoveryShown = true;
        value.transition.cancel();
        value.releaseInput(); value.unembed(); value.mode = Mode::Warm;
        if (value.window) { EnableWindow(value.window, TRUE); ShowWindow(value.window, SW_SHOWNOACTIVATE); }
        if (value.promoting)
        {
            value.promoting = false;
            value.reply(value.pendingPromotion, "Character disconnected before switching completed.");
        }
        else if (value.demoting)
        { value.demoting = false; value.reply(value.pendingDemotion, "Character disconnected before switching completed."); }
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
    if (value.transition.active())
    {
        const bool escape = value.appForeground() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000);
        const bool allowed = value.cameraAllowed() && gAgent.getRegion()->getRegionID() == value.transitionRegion && !(escape && !value.escapeHeld);
        value.escapeHeld = escape;
        if (value.transition.finished(GetTickCount64(), value.generation, allowed))
        {
            if (!allowed) value.transitionNotice = "Bird's-eye transition skipped; switching instantly.";
            value.transition.cancel();
        }
    }
    if (value.demoting && !value.transition.active()) value.finishDemotion();
    if (value.embedded() && (!IsWindow(value.parent) || gViewerWindow->getWindow()->getFullscreen())) value.unembed();
    if (value.ready())
    {
        value.connectChat();
        const bool blocked = !value.sharingAllowed();
        if (blocked != value.chatBlocked) { value.events.clear(); value.attention.clear(); value.chatBlocked = blocked; }
        if (blocked && value.previewRate)
        {
            value.previewError = "Preview unavailable while names or locations are restricted.";
            value.preview.publish(value.status(Message{}), nullptr, 0);
        }
    }
    Message request;
    // Visual-only cancellation has no reply and names the exact pending mode
    // request. It cannot acquire input or cancel a later switch.
    if (!value.pipe.writing() && value.pipe.receive(request))
    {
        if (request.kind == Kind::CancelTransition)
        {
            if (request.sequence > value.sequence && request.generation == value.generation &&
                ((value.promoting && transitionCancelMatches(request, value.pendingPromotion)) ||
                    (value.demoting && transitionCancelMatches(request, value.pendingDemotion))))
            {
                value.sequence = request.sequence; value.lastCommand = GetTickCount64();
                value.transition.cancel(); value.transitionNotice = "Bird's-eye transition skipped; switching instantly.";
            }
        }
        else if (!value.promoting && !value.demoting) value.command(request);
        else { value.detach(); value.pipe.close(); } // Single-response contract violated.
    }
    if (!value.pipe.alive() || GetTickCount64() - value.lastCommand > 15000)
    {
        if (!value.detach()) { value.releaseInput(); value.mode = Mode::Warm; value.promoting = value.demoting = false; value.transition.cancel(); }
        value.pipe.close();
    }
}
bool FSSessionWorker::renderAllowed()
{
    return !managed() || fs_session::sessionRenderAllowed(gDisconnected, worker->readyApplied, worker->mode);
}
bool FSSessionWorker::hostForeground()
{
    return managed() && worker->readyApplied && worker->mode == Mode::Active &&
        (worker->hostForeground() || (worker->transition.active() && worker->appForeground()));
}
void FSSessionWorker::prepareDisplay()
{
    if (!worker || !gPipeline.isInit()) return;
    auto& value = *worker;
    if (value.previewPass) return;
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
bool FSSessionWorker::monitorRendering() { return managed() && worker->previewPass; }
void FSSessionWorker::renderMonitor()
{
    if (!managed()) return;
    auto& value = *worker;
    if (!value.previewRate || !value.ready() || value.mode == Mode::Active || value.promoting || gTeleportDisplay || !value.sharingAllowed()) return;
    const auto now = GetTickCount64();
    if (value.previewAt && now - value.previewAt < 2000u / value.previewRate) return;
    value.previewAt = now;
    const auto size = fittedPreview(static_cast<std::uint32_t>((std::max)(0, gViewerWindow->getWorldViewWidthRaw())),
        static_cast<std::uint32_t>((std::max)(0, gViewerWindow->getWorldViewHeightRaw())), value.previewCap);
    if (!previewSize(size) || !gPipeline.isInit()) return;
    LLPointer<LLImageRaw> image = new LLImageRaw;
    value.previewPass = true;
    struct Pass { Worker& worker; ~Pass() { worker.previewPass = false; } } pass{value};
    const bool success = gViewerWindow->rawSnapshot(image, static_cast<S32>(size.width), static_cast<S32>(size.height),
        false, false, false, false, false, false, false, LLSnapshotModel::SNAPSHOT_TYPE_COLOR, 640, true, true, value.mode == Mode::Warm);
    if (!success || image->getComponents() != 3 || !previewSize({static_cast<std::uint32_t>(image->getWidth()), static_cast<std::uint32_t>(image->getHeight())}))
    {
        value.previewError = "Low-resolution preview target unavailable in this graphics mode.";
        value.preview.publish(value.status(Message{}), nullptr, 0); return;
    }
    value.previewError.clear();
    const auto width = static_cast<std::uint32_t>(image->getWidth()), height = static_cast<std::uint32_t>(image->getHeight());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto source = (static_cast<std::size_t>(height - 1 - y) * width + x) * 3;
            const auto target = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[target] = image->getData()[source + 2]; pixels[target + 1] = image->getData()[source + 1];
            pixels[target + 2] = image->getData()[source]; pixels[target + 3] = 255;
        }
    Message frame = value.status(Message{}); frame.width = width; frame.height = height;
    frame.flags |= PreviewFrame; frame.event = ++value.previewSequence; frame.cursor = now;
    value.preview.publish(frame, pixels.data(), pixels.size());
}
void FSSessionWorker::beginCameraFrame()
{
    if (!managed() || !worker->transition.active() || worker->transitionStyle != 1 || worker->previewPass || !worker->cameraAllowed()) return;
    auto& value = *worker;
    auto& camera = LLViewerCamera::instance();
    const LLVector3 avatar = gAgent.getPositionAgent();
    const F32 cap = llmin(96.f, camera.getFar() * 0.65f);
    const F32 distance = (camera.getOrigin() - avatar).length();
    if (!camera.isFinite() || !avatar.isFinite() || !std::isfinite(cap) || cap < 16.f || distance > cap)
    { value.transition.cancel(); value.transitionNotice = "Bird's-eye transition skipped: long-range or unavailable camera."; return; }
    const F32 height = llmin(cap, llmax(static_cast<F32>(value.transitionHeight), distance * 1.25f));
    LLVector3 horizontal = camera.getAtAxis(); horizontal.mV[VZ] = 0.f;
    if (horizontal.normalize() < 0.001f) horizontal.set(1.f, 0.f, 0.f);
    const LLVector3 bird_origin = avatar + LLVector3(0.f, 0.f, height) - horizontal * (height * 0.12f);
    LLCoordFrame bird(bird_origin, avatar + LLVector3(0.f, 0.f, 1.f) - bird_origin);
    const F32 t = value.transition.fraction(GetTickCount64());
    const F32 weight = value.demoting ? t : 1.f - t;
    value.cameraBefore = camera; value.cameraOverridden = true;
    camera.setOrigin(camera.getOrigin() + (bird_origin - camera.getOrigin()) * weight);
    camera.setAxes(slerp(weight, value.cameraBefore.getQuaternion(), bird.getQuaternion()));
}
void FSSessionWorker::drawTransition()
{
    if (!managed() || !worker->transition.active() || worker->transitionStyle != 2 || worker->previewPass || !worker->cameraAllowed() || !gViewerWindow) return;
    const F32 fraction = worker->transition.fraction(GetTickCount64());
    const F32 alpha = worker->demoting ? fraction : 1.f-fraction;
    const LLRect rect = gViewerWindow->getWorldViewRectRaw();
    LLGLSUIDefault state;
    gViewerWindow->setup2DRender();
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gUIProgram.bind();
    gl_rect_2d(rect,LLColor4(0.f,0.f,0.f,alpha));
    gGL.flush(); gGL.color4f(1.f,1.f,1.f,1.f); gUIProgram.unbind();
}
void FSSessionWorker::endCameraFrame()
{
    if (!worker || !worker->cameraOverridden) return;
    auto& value = *worker;
    // Restore native pose/frustum before snapshot/reflection/idle work. Agent
    // camera controls and simulator camera messages are never changed here.
    static_cast<LLCamera&>(LLViewerCamera::instance()) = value.cameraBefore;
    value.cameraOverridden = false;
    gViewerWindow->setup3DRender();
}
bool FSSessionWorker::inputAllowed()
{
    // Disconnected native close/recovery UI has no live simulator input owner.
    return !managed() || gDisconnected || !worker->readyApplied || (worker->mode == Mode::Active && worker->inputGranted && !worker->promoting && !worker->demoting);
}
bool FSSessionWorker::voiceAllowed() { return !managed() || worker->voiceAllowed(); }
bool FSSessionWorker::backgroundAudioMuted()
{
    return managed() && worker->readyApplied && (worker->mode != Mode::Active || worker->promoting || worker->demoting) && worker->muteBackground;
}
bool FSSessionWorker::requestCharacterSwitch(unsigned int command)
{
    if (!managed() || !worker->ready() || !worker->shortcuts || !worker->inputGranted || worker->mode != Mode::Active ||
        worker->promoting || worker->demoting || !worker->sharingAllowed() || command > 6 || !gFocusMgr.getAppHasFocus() ||
        gFocusMgr.focusLocked() || LLFloaterReg::instanceVisible("preferences")) return false;
    auto* focus = dynamic_cast<LLUICtrl*>(gFocusMgr.getKeyboardFocus());
    if (focus && focus->acceptsTextInput()) return false;
    DWORD foreground = 0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if (foreground != GetCurrentProcessId()) return false;
    const HIMC context = ImmGetContext(worker->window);
    const bool composing = context && ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0) > 0;
    if (context) ImmReleaseContext(worker->window,context);
    if (composing) return false;
    Message event; event.eventType = EventType::SwitchIntent; event.unread = command; event.eventAt = GetTickCount64();
    worker->events.push(event); return true;
}
void FSSessionWorker::nearbyMessage(const LLChat& chat)
{
    if (!managed() || !worker->ready() || !worker->sharingAllowed() || chat.mMuted ||
        chat.mChatType == CHAT_TYPE_IM || chat.mChatType == CHAT_TYPE_IM_GROUP ||
        chat.mChatStyle == CHAT_STYLE_HISTORY || chat.mChatStyle == CHAT_STYLE_SERVER_HISTORY) return;
    Message message; message.eventType = EventType::Chat; message.topic = Topic::Nearby;
    message.title = "Nearby chat"; message.sender = chat.mFromName; message.text = chat.mText;
    message.recipient = chat.mFromID.isNull() ? "" : chat.mFromID.asString(); // Sender UUID; exclude own echoes from unread.
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
    if (!managed() || worker->previewPass) return;
    ++worker->frames;
    if (worker->promoting && !worker->transition.active() && worker->ready() && worker->frames - worker->promotionStart >= 2)
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
void FSSessionWorker::quitCancelled()
{
    if (worker && worker->window && worker->restartTag)
        SetPropW(worker->window, L"FolderstormRestartCancelled", reinterpret_cast<HANDLE>(worker->restartTag));
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
void FSSessionWorker::importProfileSettings() {}
bool FSSessionWorker::temporaryControl(const std::string&) { return false; }
void FSSessionWorker::tick() {}
bool FSSessionWorker::renderAllowed() { return true; }
bool FSSessionWorker::hostForeground() { return false; }
void FSSessionWorker::prepareDisplay() {}
bool FSSessionWorker::monitorRendering() { return false; }
void FSSessionWorker::renderMonitor() {}
void FSSessionWorker::beginCameraFrame() {}
void FSSessionWorker::drawTransition() {}
void FSSessionWorker::endCameraFrame() {}
bool FSSessionWorker::inputAllowed() { return true; }
bool FSSessionWorker::voiceAllowed() { return true; }
bool FSSessionWorker::backgroundAudioMuted() { return false; }
bool FSSessionWorker::requestCharacterSwitch(unsigned int) { return false; }
void FSSessionWorker::nearbyMessage(const LLChat&) {}
bool FSSessionWorker::keyAllowed(unsigned int) { return true; }
void FSSessionWorker::keyReleased(unsigned int) {}
bool FSSessionWorker::textAllowed() { return true; }
void FSSessionWorker::focusHostedClient() {}
int FSSessionWorker::backgroundYield(int normal) { return normal; }
void FSSessionWorker::framePresented() {}
FSSessionWorker::LoginGate FSSessionWorker::loginGate(const std::string&, const std::string&) { return LoginGate::Allow; }
void FSSessionWorker::quitCancelled() {}
void FSSessionWorker::shutdown() {}
#endif
