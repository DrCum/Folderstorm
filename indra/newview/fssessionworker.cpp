/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "fssessionworker.h"
#include "llsd.h"
#include "fsavatarpickersearchlease.h"

#if LL_WINDOWS
#include <algorithm>
#include "fssessionpipe.h"
#include "fssessionchatmodel.h"
#include "fssessioncontacts.h"
#include "llavatarnamecache.h"
#include "llavatarname.h"
#include "llcoros.h"
#include "llcorehttputil.h"
#include "lluri.h"
#include "fssessionlifecycle.h"
#include "fssessionframepipe.h"
#include "fssessiontransition.h"
#include "fssessionusability.h"
#include "fssessionprofileseed.h"
#include "llsdserialize.h"
#include "llxmlnode.h"
#include "lltoolbarview.h"
#include "llxuiparser.h"
#include "llcommandmanager.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "llimage.h"
#include "llgl.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "fseventapibridge.h"
#include "fssessionassistant.h"
#include "fspanelpreferencelocalassistant.h"
#include "llprocess.h"
#include "llsdjson.h"
#include <boost/json.hpp>
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
#include "fssessioninputguard.h"
#include "fssessionaudio.h"
#include "fssessionreadpolicy.h"
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
#include <set>
#include <cmath>

namespace
{
using namespace fs_session;
bool profilePlainFile(const std::filesystem::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
bool profilePlainDirectory(const std::filesystem::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool readProfileFile(const std::filesystem::path& path, std::string& bytes, std::size_t limit = ProfileSettingsLimit)
{
    bytes.clear(); // A rejected read must not leave a queued toolbar payload.
    std::error_code error;
    if (!profilePlainFile(path)) return false;
    const auto size = std::filesystem::file_size(path,error);
    if (error || !size || size > limit) return false;
    std::ifstream input(path,std::ios::binary);
    if (!input) return false;
    bytes.assign(static_cast<std::size_t>(size),'\0'); input.read(bytes.data(),static_cast<std::streamsize>(size));
    const bool complete = input.gcount() == static_cast<std::streamsize>(size) && input.peek() == std::char_traits<char>::eof() && validProfileXmlText(bytes);
    if (!complete) bytes.clear();
    return complete;
}
struct ProfileBackup
{
    std::filesystem::path folder;
    bool create(const std::filesystem::path& destination)
    {
        std::error_code error;
        if (!profilePlainDirectory(destination)) return false;
        const auto backups = destination/L"settings-import-backups";
        std::filesystem::create_directory(backups,error);
        if (error || !profilePlainDirectory(backups)) return false;
        folder = backups/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId()));
        return std::filesystem::create_directory(folder,error) && !error;
    }
    bool save(const std::filesystem::path& file) const
    {
        const DWORD attributes = GetFileAttributesW(file.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND;
        if (!profilePlainFile(file)) return false;
        std::error_code error;
        return std::filesystem::copy_file(file,folder/file.filename(),std::filesystem::copy_options::none,error) && !error;
    }
};
void recordProfileImport(const std::string& detail,bool reset=false)
{
    // Retain a bounded explanation, never control values, conversations or
    // Inventory data. Later account/toolbar phases append to the same report.
    const auto name=gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"settings-import-report.txt");
    const auto file=std::filesystem::path(ll_convert_string_to_wide(name,name.size(),CP_UTF8));
    std::error_code error;const auto size=std::filesystem::file_size(file,error);
    const bool append=!reset && !error && size<16384;
    std::ofstream output(file,std::ios::binary|(append?std::ios::app:std::ios::trunc));
    const auto line=boundedText(detail,4096,true);output.write(line.data(),static_cast<std::streamsize>(line.size()));output.put('\n');
}
bool validImportedControlValue(const std::string& type,const LLSD& value)
{
        const auto number = [](const LLSD& v) { return (v.isReal() || v.isInteger()) && std::isfinite(v.asReal()); };
        if (type == "Boolean") return value.isBoolean() || (value.isInteger() && (value.asInteger() == 0 || value.asInteger() == 1));
        if (type == "S32") return value.isInteger();
        if (type == "U32") return value.isInteger() && value.asInteger() >= 0;
        if (type == "F32") return number(value) && std::abs(value.asReal()) <= (std::numeric_limits<F32>::max)();
        if (type == "String") return value.isString() && value.asString().size() <= 4096 && validUtf8(value.asString(),true);
        if(type!="Vector3" && type!="Vector3D" && type!="Color3" && type!="Color4" && type!="Color4U" && type!="Rect")return false;
        const std::size_t expected = type == "Vector3" || type == "Vector3D" || type == "Color3" ? 3 : 4;
        if (!value.isArray() || value.size() != expected) return false;
        for (auto it = value.beginArray(); it != value.endArray(); ++it)
            if (!number(*it) || ((type == "Rect" || type == "Color4U") && !it->isInteger()) ||
                (type != "Vector3D" && std::abs(it->asReal()) > (std::numeric_limits<F32>::max)()) ||
                (type == "Color4U" && (it->asInteger() < 0 || it->asInteger() > 255))) return false;
        return true;
    }

void fillImportDefaults(LLControlGroup& controls,LLSD& records,std::uint32_t groups,bool account)
{
    struct Defaults final : LLControlGroup::ApplyFunctor
    {
        LLSD& records;std::uint32_t groups;bool account;
        Defaults(LLSD& data,std::uint32_t selected,bool perAccount):records(data),groups(selected),account(perAccount){}
        void apply(const std::string& name,LLControlVariable* control) override
        {
            const auto type=LLControlGroup::typeEnumToString(control->type());
            const bool eligible=account?(accountPreferenceAllowed(name,groups)&&control->isPersisted()&&control->isBackupable()):seedPreference(name,type,control->isPersisted(),control->isBackupable(),true,groups);
            if(eligible && !records.has(name)){records[name]["Type"]=type;records[name]["Value"]=control->getDefault();records[name]["Backup"]=true;}
        }
    } defaults(records,groups,account);
    controls.applyToAll(&defaults);
}
struct Worker : LLIMSessionObserver
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
    std::wstring toolbarSource;
    std::uint32_t copyGroups = CopyLegacy;
    std::string toolbarImport,accountImport;
    std::string settingsNotice;
    mutable std::string lastRecordedImport; // Report deduplication only; status() remains logically const.
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
    bool assistantHold=false;
    std::string assistantProfile;
    LLProcessPtr assistantCheck;Message assistantCheckRequest;LLFrameTimer assistantCheckTimer;std::string assistantCheckOutput,assistantCheckSession;
    bool hostedStyle = false;
    std::uintptr_t focusLease = 0, restartTag = 0;
    bool permitVoice = false, muteBackground = true, hostUI = false, chatBlocked = false, shortcuts = false, shellShortcuts = false;
    EventBuffer events;
    std::map<std::string,std::string> attention;
    boost::signals2::scoped_connection chatConnection, notificationConnection;
    bool sessionObserverConnected=false;
    Message pendingPromotion;
    HWND window = nullptr, parent = nullptr;
    LONG_PTR style = 0, exStyle = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    struct Override { LLSD effective; bool saved = false; };
    std::map<std::string, Override> controls;
    InputQuarantine inputQuarantine;
    std::uint64_t lastInputDiagnostic = 0;

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
    WorkerAlertDecision backgroundAlertDecision(const LLUUID& sender) const
    {
        if(!hostUI)return WorkerAlertDecision::PolicyOff;
        return backgroundHostAlertDecision(ready(), mode != Mode::Active, promoting || demoting, !sharingAllowed(),
            gAgent.isDoNotDisturb(), gSavedSettings.getBOOL("MuteAudio") || gSavedSettings.getBOOL("MuteUI") ||
            gSavedSettings.getF32("AudioLevelUI") <= 0.f, !sender.isNull() && sender != gAgentID &&
            !LLMuteList::instance().isMuted(sender,LLMute::flagTextChat));
    }
    void prepareAlert(Message& message,const LLUUID& sender,bool region=false) const
    {
        const auto decision=region?WorkerAlertDecision::RegionMessage:backgroundAlertDecision(sender);
        message.detail=workerAlertDecisionText(decision);
        if(decision==WorkerAlertDecision::Allowed){message.flags|=AlertEligible;message.eventAt=GetTickCount64();}
    }
    void sessionAdded(const LLUUID&,const std::string&,const LLUUID&,bool) override {}
    void sessionActivated(const LLUUID&,const std::string&,const LLUUID&) override {}
    void sessionVoiceOrIMStarted(const LLUUID&) override {}
    void sessionIDUpdated(const LLUUID&,const LLUUID&) override {}
    void sessionRemoved(const LLUUID& session) override
    {
        if(detached || !ready() || !sharingAllowed())return;
        Message event;event.eventType=EventType::ConversationClosed;event.topic=Topic::Private;
        event.conversation=session.asString();events.push(std::move(event));
    }
    void disconnectSessionObserver()
    {
        if(sessionObserverConnected && LLIMMgr::instanceExists() && gIMMgr)gIMMgr->removeSessionObserver(this);
        sessionObserverConnected=false;
    }
    void connectChat()
    {
        if(!sessionObserverConnected && gIMMgr){gIMMgr->addSessionObserver(this);sessionObserverConnected=true;}
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
                prepareAlert(message,sender,data["is_region_msg"].asBoolean());
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
                    const auto supported = notification ? attentionCategory(notification->getName()) : std::string{};
                    const auto category=supported.empty()?std::string("Viewer notification"):supported;
                    const auto previous=attention.find(key);
                    if(signal=="delete" || !notification || !notification->isActive())
                    {
                        if(previous!=attention.end())
                        {Message item;item.eventType=previous->second=="Viewer notification"?EventType::NoticeRemoved:EventType::AttentionRemoved;item.topic=Topic::Notice;item.conversation=key;events.push(item);attention.erase(previous);}
                    }
                    else if(previous!=attention.end() || attention.size()<AttentionBook::Capacity)
                    {
                        const bool fresh=previous==attention.end();attention[key]=category;
                        Message item;item.topic=Topic::Notice;item.conversation=key;
                        LLUUID sender=notification->getPayload()["from_id"].asUUID();
                        if(sender.isNull())sender=notification->getPayload()["owner_id"].asUUID();
                        if(sender.isNull())sender=notification->getPayload()["task_id"].asUUID();
                        if(supported.empty())
                        {
                            item.eventType=fresh?EventType::Notice:EventType::Conversation;
                            std::string label;
                            for(const auto c:notification->getName()){if(!label.empty() && c>='A' && c<='Z')label+=' ';label+=c;}
                            item.title="Viewer: "+boundedText(label,240);item.text=boundedText(notification->getMessage(),3071,true);
                            item.recipient=sender.isNull()?"":sender.asString();item.unread=1;
                        }
                        else
                        {
                            item.eventType=EventType::Attention;item.title=category;item.text="Review in this character's native viewer.";
                            item.recipient=sender.isNull()?"":sender.asString();
                            if(fresh)prepareAlert(item,sender);
                        }
                        events.push(std::move(item));
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
                { Message item; item.eventType = it->second=="Viewer notification"?EventType::NoticeRemoved:EventType::AttentionRemoved; item.topic = Topic::Notice; item.conversation = it->first; events.push(item); it = attention.erase(it); }
                else ++it;
            }
            payload = events.after(request.cursor);
            // Restrictions may change after the native enqueue callback. Check
            // the owning group as well as its sender immediately before IPC.
            if(!filterChatEvent(payload,gAgentID.asString(),[](const std::string& id)
                { return RlvActions::canReceiveIM(LLUUID(id)); }))payload.detail=workerAlertDecisionText(WorkerAlertDecision::Restricted);
            if(payload.flags&AlertEligible)
            {
                const auto decision=backgroundAlertDecision(LLUUID(payload.recipient));
                if(decision!=WorkerAlertDecision::Allowed){payload.flags&=~AlertEligible;payload.eventAt=0;payload.detail=workerAlertDecisionText(decision);}
            }
            if (payload.eventType == EventType::Chat && payload.topic != Topic::Nearby && payload.topic != Topic::Notice)
                if (const auto* native = LLIMModel::instance().findIMSession(LLUUID(payload.conversation)))
                    payload.unread = static_cast<std::uint32_t>((std::max)(0,native->mNumUnread));
            auto response = status(request);
            response.event = payload.event; response.cursor = payload.cursor; response.eventType = payload.eventType;
            response.topic = payload.topic; response.conversation = payload.conversation; response.sender = payload.sender;
            response.title = payload.title; response.text = payload.text; response.unread = payload.unread;
            response.recipient = payload.recipient; response.flags |= payload.flags & (AlertEligible|ConversationRestricted); response.eventAt = payload.eventAt;
            if(payload.detail.rfind("Alert:",0)==0)response.detail=payload.detail;
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
        if(request.kind==Kind::CloseChat)
        {
            if(request.topic==Topic::Nearby){reply(request,"Nearby chat remains available.");return;}
            auto response=status(request);response.cursor=events.latest();
            if(!pipe.send(response)){detach();pipe.close();}return;
        }
        if (request.kind == Kind::MarkRead && request.topic == Topic::Notice)
        { reply(request); return; } // Host badge only; never accept a native offer.
        auto* session = request.conversation.empty() ? nullptr : LLIMModel::instance().findIMSession(LLUUID(request.conversation));
        if (!request.conversation.empty() && (!session || topicFor(*session) != request.topic))
        { reply(request, "This conversation is no longer available. Open it in the character's viewer."); return; }
        if (request.kind == Kind::MarkRead)
        {
            if ((request.flags & AutomaticRead) && !readFenceCurrent(request,events.latest()))
            {
                auto response = status(request); response.flags |= ReadDeferred;
                if (!pipe.send(response)) { detach(); pipe.close(); }
                return;
            }
            if (session && ((session->isP2PSessionType() && !RlvActions::canReceiveIM(session->mOtherParticipantID)) ||
                (session->isGroupSessionType() && !RlvActions::canReceiveIM(session->mSessionID))))
            { reply(request,"Conversation is blocked by receive restrictions."); return; }
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
    Message assistantStatus(const Message& request) const
    {
        auto& bridge=FSEventAPIBridge::instance();const auto snapshot=bridge.getStatusSnapshot();
        auto response=status(request);
        response.width=(snapshot.enabled?AssistantEnabled:0u)|(snapshot.ready?AssistantReady:0u)|(assistantHold?AssistantPaused:0u)|
            (FSPanelPreferenceLocalAssistant::sessionScoped()?AssistantSessionOnly:0u);
        response.height=snapshot.enabled?1u:0u;response.unread=static_cast<std::uint32_t>(snapshot.pendingApprovals);
        response.cursor=static_cast<std::uint64_t>(snapshot.policyGeneration);response.event=snapshot.approvalRevision;
        response.text=snapshot.ready?bridge.sessionReference():std::string();response.detail=boundedText(snapshot.error,118);
        response.title=snapshot.pendingApprovalSummary.empty()?"Local Assistant":"Local Assistant: "+snapshot.pendingApprovalSummary;
        response.sender="Read: "+FSEventAPIBridge::getPermissionLevel("read")+"; Edit: "+FSEventAPIBridge::getPermissionLevel("edit")+"; Wear: "+FSEventAPIBridge::getPermissionLevel("wear")+"; Create: "+FSEventAPIBridge::getPermissionLevel("create");
        return response;
    }
    void pollAssistantCheck()
    {
        if(!assistantCheck)return;
        auto& output=assistantCheck->getReadPipe(LLProcess::STDOUT);auto& errors=assistantCheck->getReadPipe(LLProcess::STDERR);
        const bool stale=!ready() || assistantCheckRequest.generation!=generation || assistantCheckRequest.account!=gAgentID.asString() ||
            assistantCheckRequest.grid!=loginGrid || assistantCheckSession!=FSEventAPIBridge::instance().sessionReference();
        if(stale || output.size()+assistantCheckOutput.size()>8192 || errors.size()>8192 || assistantCheckTimer.getElapsedTimeF32()>5.f)
        {reply(assistantCheckRequest,stale?"Character or assistant connection changed during check":"Connection check timed out or returned invalid output");assistantCheck.reset();return;}
        assistantCheckOutput+=output.read(output.size());errors.read(errors.size());
        if(assistantCheck->isRunning())return;
        bool passed=false;std::string stage;
        try
        {
            const auto data=LlsdFromJson(boost::json::parse(assistantCheckOutput));stage=data["stage"].asString();
            passed=data["ok"].asBoolean() && data["pid"].asInteger()==LLApp::getPid() &&
                assistantCheck->getStatus().mState==LLProcess::EXITED && assistantCheck->getStatus().mData==0;
        }
        catch(const std::exception&){}
        auto response=assistantStatus(assistantCheckRequest);
        response.text=passed?"Connection verified for this exact character login.":stage=="discovery"?"Discovery unavailable. Enable Local Assistant and check its registry.":
            stage=="session_changed"?"Assistant login changed. Check again.":stage=="authorization"?"Local Assistant access was refused.":
            stage=="timeout"?"Connection check timed out.":"Local Assistant connection check failed.";
        pipe.send(response);assistantCheck.reset();
    }
    void releaseInput()
    {
        inputGranted = false;
        inputQuarantine.reset();
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
        detached = true;assistantHold=false;contactSearch.reset();friendSnapshot.clear();friendToken=0;
        toolbarImport.clear(); accountImport.clear(); toolbarSource.clear(); settingsSource.clear();
        previewCap = {}; previewRate = 0;
        chatConnection.disconnect(); notificationConnection.disconnect(); disconnectSessionObserver(); events.clear();
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
        if (!settingsNotice.empty() && error.empty())
        {
            if(settingsNotice!=lastRecordedImport){recordProfileImport(settingsNotice);lastRecordedImport=settingsNotice;}
            reply.detail = boundedText(settingsNotice,119);
        }
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
    struct SearchResult
    {
        ChatKey source;
        LLUUID login;
        std::uint64_t token=0;
        bool pending=true;
        std::string error;
        std::vector<Contact> rows;
    };
    std::shared_ptr<SearchResult> contactSearch;
    std::weak_ptr<SearchResult> runningContactSearch; // At most one bounded HTTP request, including discarded sessions.
    std::vector<Contact> friendSnapshot;
    std::uint64_t friendToken=0;
    bool contactAllowed(const LLUUID& resident) const
    {
        return resident.notNull() && resident!=gAgentID && RlvActions::canStartIM(resident) &&
            RlvActions::canSendIM(resident) && RlvActions::canReceiveIM(resident) && FSData::instance().canSendToIMContact(IM_NOTHING_SPECIAL,gAgentID,resident);
    }
    Contact contactFor(const LLUUID& resident) const
    {
        LLAvatarName name;Contact c;c.id=resident.asString();
        if(LLAvatarNameCache::get(resident,&name)){c.display=boundedText(name.getDisplayName(true),255);c.username=boundedText(name.getUserName(),127);}
        else {c.display=c.id;c.username="Name loading";}
        c.online=LLAvatarTracker::instance().isBuddyOnline(resident);c.allowed=contactAllowed(resident);return c;
    }
    void contactCommand(const Message& request)
    {
        if(!ready() || !sharingAllowed() || request.account!=gAgentID.asString() || request.grid!=loginGrid || request.width>1 || !request.event)
        {reply(request,"Contacts are unavailable or their character/session changed.");return;}
        const ChatKey identity{id,generation,gAgentID.asString(),loginGrid,{}};
        auto response=status(request);response.width=request.width;response.height=0;response.event=request.event;
        if(request.kind==Kind::ContactSearch)
        {
            if(request.text.size()<2 || request.text.size()>127 || !validUtf8(request.text))
            {reply(request,"Enter 2 to 127 UTF-8 bytes of a resident name.");return;}
            if(const auto running=runningContactSearch.lock();running && running->pending){reply(request,"A resident search is still running. Try again shortly.");return;}
            auto* region=gAgent.getRegion();auto url=region?region->getCapability("AvatarPickerSearch"):std::string{};
            if(url.empty()){reply(request,"Resident search is unavailable on this region. Friends remain available.");return;}
            const auto searchLease=fs_assistant_social::AvatarPickerSearchLease::acquire();
            if(!searchLease){reply(request,"A resident search is still running. Try again shortly.");return;}
            if(!url.empty() && url.back()!='/')url+='/';
            auto query=request.text;std::replace(query.begin(),query.end(),'.',' ');url+="?page_size=100&names="+LLURI::escape(query);
            auto result=std::make_shared<SearchResult>();result->source=identity;result->login=gAgent.getSessionID();result->token=request.event;contactSearch=result;runningContactSearch=result;
            LLCoros::instance().launch("FolderstormContactSearch",[result,url,searchLease]()
            {
                (void)searchLease;
                auto adapter=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("FolderstormContactSearch",LLCore::HttpRequest::DEFAULT_POLICY_ID);
                auto http=std::make_shared<LLCore::HttpRequest>();auto options=std::make_shared<LLCore::HttpOptions>();options->setTimeout(20);
                const LLSD data=adapter->getAndSuspend(http,url,options);
                const auto httpStatus=LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(data[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
                // This state is session-only. Replaced requests own separate state;
                // never keep a Worker pointer alive through an HTTP yield.
                if(result->login!=gAgent.getSessionID() || result->source.account!=gAgentID.asString() || !FSSessionWorker::managed())
                {result->error="Search session ended.";result->pending=false;return;}
                if(!httpStatus || !data["agents"].isArray()){result->error="Resident search failed. Try again.";result->pending=false;return;}
                for(auto it=data["agents"].beginArray();it!=data["agents"].endArray() && result->rows.size()<MaxSearchContacts;++it)
                {
                    const auto resident=(*it)["id"].asUUID();if(resident.isNull() || resident==gAgentID)continue;
                    Contact c{resident.asString(),boundedText((*it)["display_name"].asString(),255),boundedText((*it)["username"].asString(),127),false,false};
                    if(c.display.empty())c.display=c.username;
                    if(c.valid() && std::none_of(result->rows.begin(),result->rows.end(),[&](const Contact& old){return old.id==c.id;}))result->rows.push_back(std::move(c));
                }
                result->pending=false;
            });
            response.height=1;response.text="FSC1\n";
        }
        else if(request.kind==Kind::OpenIM)
        {
            const LLUUID resident(request.recipient);
            const Contact* chosen=nullptr;
            if(request.width==1 && contactSearch && contactSearch->token==request.event && contactSearch->source.owns(request) && !contactSearch->pending && contactSearch->error.empty())
                for(const auto& c:contactSearch->rows)if(c.id==request.recipient){chosen=&c;break;}
            const bool friendNow=LLAvatarTracker::instance().isBuddy(resident);
            if(!contactAllowed(resident) || (request.width==0?!friendNow:!chosen))
            {reply(request,"This resident is unavailable or starting an IM is blocked by restrictions.");return;}
            const auto name=chosen?chosen->display:contactFor(resident).display;
            const auto sessionID=gIMMgr->addSession(name,IM_NOTHING_SPECIAL,resident);
            auto* im=LLIMModel::instance().findIMSession(sessionID);
            if(!im){reply(request,"The native conversation could not be opened.");return;}
            response.event=0;response.cursor=events.latest();response.eventType=EventType::Conversation;response.topic=Topic::Private;
            response.conversation=sessionID.asString();response.recipient=resident.asString();response.title=boundedText(im->mName,255);
            response.unread=static_cast<std::uint32_t>((std::max)(0,im->mNumUnread));
        }
        else
        {
            std::vector<Contact>* rows=&friendSnapshot;
            if(request.width==0)
            {
                if(!request.cursor)
                {
                    friendToken=request.event;friendSnapshot.clear();LLAvatarTracker::buddy_map_t buddies;LLAvatarTracker::instance().copyBuddyList(buddies);
                    for(bool online:{true,false})for(const auto& buddy:buddies){if(friendSnapshot.size()>=MaxContacts)break;if(buddy.first!=gAgentID && LLAvatarTracker::instance().isBuddyOnline(buddy.first)==online)friendSnapshot.push_back(contactFor(buddy.first));}
                    friendSnapshot=sortedContacts(std::move(friendSnapshot));
                }
                if(friendToken!=request.event){reply(request,"Friend refresh expired; refresh again.");return;}
            }
            else
            {
                if(!contactSearch || contactSearch->token!=request.event || !contactSearch->source.owns(request) || contactSearch->login!=gAgent.getSessionID())
                {reply(request,"Search changed or expired. Search again.");return;}
                if(!contactSearch->error.empty()){reply(request,contactSearch->error);return;}
                if(contactSearch->pending){response.height=1;response.text="FSC1\n";if(!pipe.send(response)){detach();pipe.close();}return;}
                rows=&contactSearch->rows;
                for(auto& c:*rows){const LLUUID resident(c.id);c.allowed=contactAllowed(resident);c.online=LLAvatarTracker::instance().isBuddyOnline(resident);}
            }
            for(auto& c:*rows)c.allowed=contactAllowed(LLUUID(c.id));
            if(request.cursor>rows->size()){reply(request,"Invalid contact page.");return;}
            std::size_t next=0;response.text=contactPage(*rows,static_cast<std::size_t>(request.cursor),next);
            response.cursor=next;response.unread=static_cast<std::uint32_t>(rows->size());
        }
        if(!pipe.send(response)){detach();pipe.close();}
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
        case Kind::AssistantPause:
        {
            // Runtime transport ownership belongs to this authenticated worker,
            // including its login screen. No account data/action is accessed.
            if(request.width>1){reply(request,"Invalid assistant pause choice.");break;}
            assistantHold=request.width!=0;
            auto& bridge=FSEventAPIBridge::instance();
            bridge.applyEnabled(!assistantHold && gSavedSettings.getBOOL("EnableLocalEventAPIBridge"));
            pipe.send(assistantStatus(request));break;
        }
        case Kind::AssistantInfo:
        case Kind::AssistantConfigure:
        case Kind::AssistantReview:
        {
            if((request.kind==Kind::AssistantInfo && request.unread>2) || !ready() || request.account!=gAgentID.asString() || request.grid!=loginGrid)
            {reply(request,"Local Assistant login changed or is unavailable.");break;}
            auto& bridge=FSEventAPIBridge::instance();
            if(request.kind==Kind::AssistantConfigure)
            {
                auto control=gSavedSettings.getControl("EnableLocalEventAPIBridge");
                if(request.width>1 || !control || FSPanelPreferenceLocalAssistant::sessionScoped() || LLFloaterReg::instanceVisible("preferences") ||
                    request.cursor!=static_cast<std::uint64_t>(FSEventAPIBridge::getPolicyGeneration()) || request.height!=(control->getValue().asBoolean()?1u:0u))
                {reply(request,"Local Assistant settings changed or have an open/session-only draft. Use native Preferences.");break;}
                control->setValue(LLSD(request.width!=0),true);
                const auto file=gSavedSettings.getString("ClientSettingsFile");
                // saveToFile returns a control count: a valid empty/default-only
                // file also returns zero. Verify the persisted enable choice.
                if(!file.empty())gSavedSettings.saveToFile(file,true);
                std::string bytes;LLSD persisted;bool saved=false;
                if(!file.empty() && readProfileFile(std::filesystem::path(ll_convert_string_to_wide(file,file.size(),CP_UTF8)),bytes))
                {
                    std::istringstream input(bytes);
                    if(LLSDSerialize::fromXML(persisted,input,false)>0 && (persisted.isMap() || persisted.isUndefined()))
                    {
                        const LLSD stored=persisted.has("EnableLocalEventAPIBridge")?persisted["EnableLocalEventAPIBridge"]["Value"]:control->getDefault();
                        saved=stored.asBoolean()==control->getSaveValue().asBoolean();
                    }
                }
                if(!saved){reply(request,"Local Assistant choice applied but could not be saved. Check native Preferences.");break;}
            }
            else if(request.kind==Kind::AssistantReview)
            {
                if(request.unread>1 || !FSSessionWorker::assistantActiveAllowed())
                {reply(request,"Activate this character and close conflicting dialogs before reviewing.");break;}
                SetForegroundWindow(window);
                if(request.unread)LLFloaterReg::showInstance("preferences",llsd::map("tab","local_assistant"),true);
                else bridge.reviewPending();
            }
            if(request.kind==Kind::AssistantInfo && request.unread==0 && !request.title.empty())assistantProfile=boundedText(request.title,128);
            bridge.refreshSession();
            if(request.kind==Kind::AssistantInfo && request.unread==2)
            {
                if(assistantCheck){reply(request,"A connection check is already running.");break;}
                LLProcess::Params params;params.executable=FSPanelPreferenceLocalAssistant::installedSidecar();
                params.args.add("--diagnose").add("--viewer-pid").add(std::to_string(LLApp::getPid()))
                    .add("--discovery").add(bridge.discoveryDirectory()).add("--viewer-session").add(bridge.sessionReference());
                params.files.add(LLProcess::FileParam("pipe")).add(LLProcess::FileParam("pipe")).add(LLProcess::FileParam("pipe"));
                params.attached=true;params.autokill=true;params.desc="Hosted Local Assistant connection check";
                assistantCheckRequest=request;assistantCheckOutput.clear();assistantCheckSession=bridge.sessionReference();assistantCheckTimer.reset();
                assistantCheck=LLProcess::create(params);
                if(!assistantCheck)reply(request,"Unable to start the matching MCP sidecar.");
                break;
            }
            auto response=assistantStatus(request);
            if(request.kind==Kind::AssistantInfo && request.unread==1)
            {
                std::string error;response.text=FSPanelPreferenceLocalAssistant::hostConfiguration(request.text,error);
                if(!error.empty() || response.text.size()>3071){reply(request,error.empty()?"MCP configuration is too large":error);break;}
            }
            pipe.send(response);break;
        }
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
                inputQuarantine.capture([](unsigned int key) { return (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0; });
                LL_DEBUGS("SessionInput") << "Promotion quarantine: " << inputQuarantine.size()
                    << " physical keys; generation " << generation << LL_ENDL;
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
        case Kind::Contacts:
        case Kind::ContactSearch:
        case Kind::OpenIM: contactCommand(request); break;
        case Kind::CloseChat:
        case Kind::Events:
        case Kind::SendChat:
        case Kind::MarkRead:
        case Kind::Conversations:
        case Kind::Typing: chatCommand(request); break;
        case Kind::ShortcutPolicy:
            if (!ready() || request.unread > 3 || request.account != gAgentID.asString() || request.grid != loginGrid)
            { reply(request,"Shortcut policy is invalid or its account changed."); break; }
            shortcuts = (request.unread&1)!=0; shellShortcuts = (request.unread&2)!=0; reply(request); break;
        case Kind::AudioPolicy:
            if (request.unread > 7) { reply(request, "Unsupported audio policy."); break; }
            permitVoice = (request.unread & 1) != 0; muteBackground = (request.unread & 2) != 0;
            hostUI = (request.unread & AudioHostUI) != 0;
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
            if (request.unread > 3 || request.account != status(request).account || request.grid != loginGrid)
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
    const auto assistantPause=env(L"FOLDERSTORM_SESSION_ASSISTANT_PAUSE");
    const auto settingsSource = env(L"FOLDERSTORM_SESSION_SETTINGS_SOURCE",32768);
    const auto profileSource = env(L"FOLDERSTORM_SESSION_PROFILE_SOURCE",32768);
    const auto toolbarSource = env(L"FOLDERSTORM_SESSION_TOOLBARS_SOURCE",32768);
    const auto copyGroups = env(L"FOLDERSTORM_SESSION_COPY_GROUPS",16);
    const bool explicitCopy = GetEnvironmentVariableW(L"FOLDERSTORM_SESSION_COPY_GROUPS",nullptr,0) != 0;
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_PIPE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_ID", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_LOCK", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_FRAMES", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_FRAME_MUTEX", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_ASSISTANT_PAUSE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_SETTINGS_SOURCE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_PROFILE_SOURCE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_TOOLBARS_SOURCE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_COPY_GROUPS", nullptr);
    if (inherited.empty() && identity.empty() && lease.empty() && frames.empty() && frameMutex.empty() && settingsSource.empty() && profileSource.empty() && toolbarSource.empty() && copyGroups.empty() && !explicitCopy && assistantPause.empty()) return true;
    if (inherited.empty() || identity.size() != 32 || lease.empty() || frames.empty() || frameMutex.empty()) return false;
    if ((explicitCopy && (copyGroups.empty() || profileSource.empty() || !settingsSource.empty())) ||
        (!profileSource.empty() && !explicitCopy) ||
        (!settingsSource.empty() && !std::filesystem::path(settingsSource).is_absolute()) ||
        (!profileSource.empty() && !std::filesystem::path(profileSource).is_absolute()) ||
        (!toolbarSource.empty() && !std::filesystem::path(toolbarSource).is_absolute())) return false;
    if(!assistantPause.empty() && assistantPause!=L"0" && assistantPause!=L"1")return false;
    auto value = std::make_unique<Worker>();value->assistantHold=assistantPause==L"1";
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
    value->settingsSource = explicitCopy ? profileSource : settingsSource;
    value->toolbarSource = toolbarSource;
    if (explicitCopy && !parseProfileCopyGroups(copyGroups,value->copyGroups)) return false;
    if ((!toolbarSource.empty() && !(value->copyGroups & (CopyToolbars|CopyAccountUI|CopyAvatar|CopyMovement))) ||
        ((value->copyGroups & (CopyToolbars|CopyAccountUI)) && toolbarSource.empty())) return false;
    value->lastCommand = GetTickCount64();
    worker = std::move(value);
    return true;
}
bool FSSessionWorker::managed() { return worker && !worker->detached; }
LLSD FSSessionWorker::assistantIdentity()
{
    if(!worker)return LLSD::emptyMap();
    if(worker->detached)return llsd::map("profile",worker->assistantProfile);
    LLUUID workerId;
    std::copy(worker->id.begin(), worker->id.end(), workerId.mData);
    return llsd::map("worker",workerId.asString(),"generation",std::to_string(worker->generation),
        "profile",worker->assistantProfile,"name",worker->loginName,"grid",worker->loginGrid);
}
bool FSSessionWorker::assistantPaused() {return managed() && worker->assistantHold;}
bool FSSessionWorker::assistantActiveAllowed()
{
    if(!managed())return true;
    const HWND popup=worker->window?GetLastActivePopup(worker->window):nullptr;
    return !(popup && popup!=worker->window && IsWindowVisible(popup)) && worker->ready() && worker->inputGranted && worker->mode==Mode::Active && !worker->promoting && !worker->demoting &&
        !worker->transition.active() && !gTeleportDisplay && !gFocusMgr.focusLocked() && !LLFloaterReg::instanceVisible("preferences");
}
void FSSessionWorker::importProfileSettings()
{
    if (!managed()) return;
    if(!worker->settingsSource.empty() || !worker->toolbarSource.empty())recordProfileImport("Profile copy: selected preferences use source values or this viewer version's defaults. Account UI and toolbar phases wait for a successful login. No credentials, contents or live restrictions are imported.",true);
    if(!worker->toolbarSource.empty() && (worker->copyGroups&(CopyAccountUI|CopyAvatar|CopyMovement)))
    {
        if(!readProfileFile(std::filesystem::path(worker->toolbarSource)/L"settings_per_account.xml",worker->accountImport))
            worker->settingsNotice="Settings import: account UI source missing or invalid; destination retained. ";
    }
    if (worker->copyGroups & CopyToolbars)
    {
        // Snapshot the selected source before login. The destination account is
        // deliberately unknown here; no account files are written until login.
        if (!readProfileFile(std::filesystem::path(worker->toolbarSource)/L"toolbars.xml",worker->toolbarImport,64u*1024u))
            worker->settingsNotice = "Settings import: toolbar source unavailable or invalid; destination kept. ";
        else worker->settingsNotice = "Settings import: toolbar copy queued for the first successful login. ";
        worker->toolbarSource.clear();
        worker->copyGroups &= ~CopyToolbars;
    }
    worker->toolbarSource.clear();
    if (worker->settingsSource.empty() || !(worker->copyGroups & (CopyGlobalPreferences|CopyColors|CopyBindings|CopyQuickPreferences))) return;
    const auto folder = std::filesystem::path(worker->settingsSource);
    worker->settingsSource.clear(); // One deliberate attempt; never retry at a new login.
    const auto targetName = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"settings.xml");
    const auto target = std::filesystem::path(ll_convert_string_to_wide(targetName,targetName.size(),CP_UTF8));
    const auto destination = target.parent_path();
    std::string bytes;
    const bool copyPreferences = (worker->copyGroups & CopyGlobalPreferences) != 0;
    if (copyPreferences && !readProfileFile(folder/L"settings.xml",bytes))
    { worker->settingsNotice += "Settings import: source unavailable, too large or not a plain settings file; preferences unchanged."; return; }
    std::error_code error;
    if (copyPreferences && std::filesystem::equivalent(folder/L"settings.xml",target,error))
    { worker->settingsNotice += "Settings import: source is this profile; preferences unchanged."; return; }
    LLSD data; std::istringstream input(bytes);
    if (copyPreferences && (LLSDSerialize::fromXML(data,input,false) <= 0 || !data.isMap() || data.size() > ProfileSettingsEntries))
    { worker->settingsNotice += "Settings import: invalid or oversized preferences; profile unchanged."; return; }

    if(copyPreferences)
    {
        // Main-viewer settings omit values equal to their session preset. Read
        // only registered, packaged presets so Phoenix/Modern defaults (for
        // example WASD) survive a copy into a Firestorm-default profile.
        const auto preset=data["SessionSettingsFile"]["Value"].asString();
        if(knownProfilePreset(preset))
        {
            const auto name=gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,preset);
            const auto file=std::filesystem::path(ll_convert_string_to_wide(name,name.size(),CP_UTF8));
            std::string text;LLSD defaults;
            if(readProfileFile(file,text))
            {
                std::istringstream sourcePreset(text);
                if(LLSDSerialize::fromXML(defaults,sourcePreset,false)>0 && defaults.isMap() && defaults.size()<=ProfileSettingsEntries)
                    for(auto it=defaults.beginMap();it!=defaults.endMap();++it)if(!data.has(it->first))data[it->first]=it->second;
            }
        }
        fillImportDefaults(gSavedSettings,data,worker->copyGroups,false);
    }
    if(data.size()>ProfileSettingsEntries){worker->settingsNotice="Settings import: too many preferences after resolving defaults; destination retained.";recordProfileImport(worker->settingsNotice);return;}
    LLSD changes; std::size_t skipped = 0;std::vector<std::string> skippedDetails;
    if (!copyPreferences) data = LLSD::emptyMap();
    for (auto it = data.beginMap(); it != data.endMap(); ++it)
    {
        auto control = gSavedSettings.getControl(it->first); const auto& record = it->second;
        const auto type = control ? LLControlGroup::typeEnumToString(control->type()) : std::string{};
        if (!control || !record.isMap() || record["Type"].asString() != type || !record.has("Value") ||
            !seedPreference(it->first,type,control->isPersisted(),control->isBackupable(),!record.has("Backup") || record["Backup"].asBoolean(),worker->copyGroups) ||
            !validImportedControlValue(type,record["Value"])) { ++skipped;if(skippedDetails.size()<40)skippedDetails.push_back(boundedText(it->first,128)+": unselected, isolated, unsupported or invalid");continue; }
        changes[it->first] = record["Value"];
    }
    ProfileBackup backup;
    if (!backup.create(destination) || (changes.size() && !backup.save(target)))
    { worker->settingsNotice += "Settings import: unable to back up existing preferences; profile unchanged."; return; }
    if (changes.size())
    {
        LLSD previous, effective;
        for (auto it = changes.beginMap(); it != changes.endMap(); ++it)
        {
            auto control = gSavedSettings.getControl(it->first);
            previous[it->first] = control->getSaveValue(); effective[it->first] = control->getValue(); control->setValue(it->second,true);
        }
        const auto temporary = backup.folder/L"settings-import.tmp";
        gSavedSettings.saveToFile(ll_convert_wide_to_string(temporary.wstring()),true);
        LLSD saved;
        bool complete = readProfileFile(temporary,bytes);
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
            worker->settingsNotice += "Settings import: preference write failed; original settings retained."; return;
        }
    }
    unsigned int copied = 0;
    for (const auto* name : seedUiFiles())
    {
        if (!copyUiFile(worker->copyGroups,name)) continue;
        const auto source = folder/name;
        if (!std::filesystem::exists(source,error)) { error.clear(); ++skipped; continue; }
        LLXMLNodePtr node;
        if (!readProfileFile(source,bytes) || !LLXMLNode::parseBuffer(bytes.data(),static_cast<U64>(bytes.size()),node) || !backup.save(destination/name))
        { ++skipped; continue; }
        const auto temp = backup.folder/(std::string(name)+".import-tmp");
        std::ofstream out(temp,std::ios::binary|std::ios::trunc); out.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); out.close();
        const auto file = destination/name;
        if (!out || !MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        { std::filesystem::remove(temp,error); ++skipped; continue; }
        ++copied;
    }
    worker->settingsNotice += "Settings import: "+std::to_string(changes.size())+" preferences, "+std::to_string(copied)+
        " UI files, "+std::to_string(skipped)+" unselected/isolated/unsupported/invalid; previous files backed up.";
    for(const auto& skippedControl:skippedDetails)recordProfileImport("Skipped "+skippedControl);
    recordProfileImport(worker->settingsNotice);
    LL_INFOS("SessionWorker") << worker->settingsNotice << LL_ENDL;
}
void FSSessionWorker::importProfileAccountSettings()
{
    if(!worker || worker->accountImport.empty())return;
    if(!managed()){worker->accountImport.clear();return;}
    if(gAgent.getID().isNull() || gAgent.getSessionID().isNull() || gDisconnected)return;
    const auto bytes=std::move(worker->accountImport);worker->accountImport.clear();
    const auto report=[&](const std::string& text){worker->settingsNotice="Settings import: "+text;recordProfileImport(worker->settingsNotice);LL_INFOS("SessionWorker")<<worker->settingsNotice<<LL_ENDL;};
    LLSD records;std::istringstream input(bytes);
    if(LLSDSerialize::fromXML(records,input,false)<=0 || !records.isMap() || records.size()>ProfileSettingsEntries){report("account preferences invalid; destination retained.");return;}
    fillImportDefaults(gSavedPerAccountSettings,records,worker->copyGroups,true);
    if(records.size()>ProfileSettingsEntries){report("too many account preferences after resolving defaults; destination retained.");return;}
    LLSD changes;std::size_t skipped=0;
    for(auto it=records.beginMap();it!=records.endMap();++it)
    {
        auto control=gSavedPerAccountSettings.getControl(it->first);const auto& record=it->second;
        const auto type=control?LLControlGroup::typeEnumToString(control->type()):std::string{};
        if(!control || !accountPreferenceAllowed(it->first,worker->copyGroups) || !control->isPersisted() || !control->isBackupable() ||
           !record.isMap() || record["Type"].asString()!=type || !record.has("Value") || (record.has("Backup")&&!record["Backup"].asBoolean()) ||
           !validImportedControlValue(type,record["Value"])){++skipped;continue;}
        changes[it->first]=record["Value"];
    }
    if(!changes.size()){report("no eligible account UI preferences; destination retained.");return;}
    const auto targetName=gSavedSettings.getString("PerAccountSettingsFile");
    const auto target=std::filesystem::path(ll_convert_string_to_wide(targetName,targetName.size(),CP_UTF8));
    if(targetName.empty()){report("destination account settings unavailable; original choices retained.");return;}
    ProfileBackup backup;if(!backup.create(target.parent_path()) || !backup.save(target)){report("account backup failed; original choices retained.");return;}
    LLSD previous,effective;
    for(auto it=changes.beginMap();it!=changes.endMap();++it){auto control=gSavedPerAccountSettings.getControl(it->first);previous[it->first]=control->getSaveValue();effective[it->first]=control->getValue();control->setValue(it->second,true);}
    const auto temporary=backup.folder/L"account-settings-import.tmp";
    gSavedPerAccountSettings.saveToFile(ll_convert_wide_to_string(temporary.wstring()),true);
    LLSD saved;std::string written;bool complete=readProfileFile(temporary,written);
    if(complete){std::istringstream check(written);complete=LLSDSerialize::fromXML(saved,check,false)>0 && saved.isMap();}
    for(auto it=changes.beginMap();complete && it!=changes.endMap();++it){auto control=gSavedPerAccountSettings.getControl(it->first);if(control->shouldSave(true))complete=saved.has(it->first)&&saved[it->first]["Value"]==control->getSaveValue();}
    if(!complete || !MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        for(auto it=previous.beginMap();it!=previous.endMap();++it){auto control=gSavedPerAccountSettings.getControl(it->first);control->setValue(it->second,true);control->setValue(effective[it->first],false);}
        std::error_code error;std::filesystem::remove(temporary,error);report("account write failed; original choices restored.");return;
    }
    report(std::to_string(changes.size())+" account UI/LookAt preferences copied; "+std::to_string(skipped)+" unselected or unsupported; prior file backed up.");
}
bool FSSessionWorker::importProfileToolbars()
{
    if (!worker || worker->toolbarImport.empty()) return false;
    if (!managed()) { worker->toolbarImport.clear(); return false; }
    if (gAgent.getID().isNull() || gAgent.getSessionID().isNull() || gDisconnected) return false;
    const auto bytes = std::move(worker->toolbarImport);
    worker->toolbarImport.clear(); // Never apply to another account/session or retry on a later login.
    const auto refused = [](const std::string& reason)
    {
        worker->settingsNotice = "Settings import: toolbar copy skipped ("+reason+"); original layout kept.";
        return false;
    };
    LLXMLNodePtr root;
    if (!LLXMLNode::parseBuffer(bytes.data(),static_cast<U64>(bytes.size()),root) || !root->hasName("toolbars"))
        return refused("invalid toolbar XML");
    std::set<std::string> bars;
    for (auto child = root->getFirstChild(); child.notNull(); child = child->getNextSibling())
    {
        const std::string name = child->getName()->mString;
        if ((name != "left_toolbar" && name != "right_toolbar" && name != "bottom_toolbar") || !bars.insert(name).second)
            return refused("unsupported or duplicate toolbar");
    }
    if (bars.empty()) return refused("no toolbar definitions");
    LLToolBarView::ToolbarSet saved, clean;
    LLXUIParser parser;
    parser.readXUI(root,saved,"profile toolbar import");
    if (!saved.validateBlock()) return refused("invalid toolbar settings");
    std::set<std::string> commands;
    unsigned int skipped = 0;
    const auto copyBar = [&](const LLToolBarView::Toolbar& source, LLToolBarView::Toolbar& destination)
    {
        if (source.commands.size() > 128) return false;
        destination.button_display_mode = source.button_display_mode();
        if (source.button_alignment.isProvided()) destination.button_alignment = source.button_alignment();
        if (source.button_layout_style.isProvided()) destination.button_layout_style = source.button_layout_style();
        for (const LLCommandId::Params& command : source.commands)
        {
            const std::string name = command.name();
            if (name.empty() || name.size() > 128 || !commands.insert(name).second) return false;
            if (LLCommandManager::instance().getCommand(name)) destination.commands.add(command);
            else ++skipped;
        }
        return true;
    };
    if ((saved.left_toolbar.isProvided() && !copyBar(saved.left_toolbar,clean.left_toolbar)) ||
        (saved.right_toolbar.isProvided() && !copyBar(saved.right_toolbar,clean.right_toolbar)) ||
        (saved.bottom_toolbar.isProvided() && !copyBar(saved.bottom_toolbar,clean.bottom_toolbar)))
        return refused("oversized or duplicate command list");
    if (!clean.validateBlock()) return refused("invalid filtered toolbar settings");
    LLXMLNodePtr output = new LLXMLNode("toolbars",false);
    parser.writeXUI(output,clean);
    std::ostringstream serialized;
    output->writeToOstream(serialized);
    const auto document = serialized.str();
    if (document.empty() || document.size() > 64u*1024u) return refused("oversized toolbar file");
    const auto targetName = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT,"toolbars.xml");
    const auto target = std::filesystem::path(ll_convert_string_to_wide(targetName,targetName.size(),CP_UTF8));
    ProfileBackup backup;
    if (!backup.create(target.parent_path()) || !backup.save(target)) return refused("backup unavailable");
    const auto temporary = backup.folder/L"toolbars-import.tmp";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out.write(document.data(),static_cast<std::streamsize>(document.size())); out.close();
    if (!out || !MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        std::error_code error; std::filesystem::remove(temporary,error);
        return refused("write failed");
    }
    // A destination's first-run reset must not overwrite this explicit choice.
    gSavedSettings.setBOOL("ResetToolbarSettings",false);
    worker->settingsNotice = "Settings import: toolbar layout copied; "+std::to_string(skipped)+" unavailable commands skipped; previous file backed up.";
    recordProfileImport(worker->settingsNotice);
    LL_INFOS("SessionWorker") << worker->settingsNotice << LL_ENDL;
    return true;
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
    value.pollAssistantCheck();
    if (gViewerWindow && gViewerWindow->getWindow()) value.window = static_cast<HWND>(gViewerWindow->getWindow()->getPlatformWindow());
    if (value.session != gAgentSessionID)
    {
        const bool wasPromoting = value.promoting, wasDemoting = value.demoting;
        const bool wasReady = value.readyApplied;
        value.releaseInput(); value.unembed();
        if (wasReady && value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
        value.session = gAgentSessionID; ++value.generation;
        value.contactSearch.reset();value.friendSnapshot.clear();value.friendToken=0;
        value.events.resetSession(); value.attention.clear(); value.shortcuts = value.shellShortcuts = false;
        value.previewCap = {}; value.previewRate = 0; value.previewError.clear();
        value.preview.publish(value.status(Message{}), nullptr, 0);
        value.transition.cancel(); value.transitionNotice.clear(); value.demoting = false;
        value.promoting = false; value.mode = Mode::Warm; value.readyApplied = false; value.recoveryShown = false;
        if (wasPromoting) value.reply(value.pendingPromotion, "Session changed before the character was ready.");
        else if (wasDemoting) value.reply(value.pendingDemotion, "Session changed before switching completed.");
    }
    if (gDisconnected && !value.recoveryShown)
    {
        value.recoveryShown = true;value.contactSearch.reset();value.friendSnapshot.clear();value.friendToken=0;
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
    const auto quarantined = value.inputQuarantine.size();
    value.inputQuarantine.refresh([](unsigned int key) { return (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0; },
        [](unsigned int native)
        {
            if (!gKeyboard) return;
            KEY mapped = 0;
            // Win32 normal and extended key translation both use this map.
            if (gKeyboard->translateKey(static_cast<U16>(native),&mapped))
            {
                // A fresh key on the other keypad/modifier side may share the
                // same logical key. Clear stale state only after every actual
                // physical origin for that logical key is up.
                bool physical = false;
                for (unsigned int key = 7; key < 256 && !physical; ++key)
                {
                    KEY translated = 0;
                    const bool matches = gKeyboard->translateKey(static_cast<U16>(key),&translated) && translated == mapped;
                    physical = matches && (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
                }
                if (!physical) gKeyboard->clearKeyState(mapped);
            }
        });
    if (quarantined != value.inputQuarantine.size())
        LL_DEBUGS("SessionInput") << "Physical release; remaining " << value.inputQuarantine.size() << LL_ENDL;
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
        if (blocked != value.chatBlocked) { value.events.clear(); value.attention.clear(); value.contactSearch.reset(); value.friendSnapshot.clear(); value.friendToken=0; value.chatBlocked = blocked; }
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
bool FSSessionWorker::nativeChatReadAllowed()
{
    if(!managed())return true;
    return fs_session::nativeChatReadAllowed(true,worker->ready(),worker->inputGranted && worker->mode==Mode::Active,
        gFocusMgr.getAppHasFocus(),worker->window && IsWindowVisible(worker->window) && !IsIconic(worker->window),worker->promoting || worker->demoting);
}
bool FSSessionWorker::backgroundAudioMuted()
{
    return managed() && worker->readyApplied && (worker->mode != Mode::Active || worker->promoting || worker->demoting) && worker->muteBackground;
}
bool FSSessionWorker::backgroundUIAudioMuted()
{
    return managed() && worker->pipe.alive() && worker->readyApplied && worker->hostUI &&
        (worker->mode != Mode::Active || worker->promoting || worker->demoting);
}
bool FSSessionWorker::requestCharacterSwitch(unsigned int command)
{
    if (!managed() || !worker->ready() || (command<=6 ? !worker->shortcuts : !worker->shellShortcuts) || !worker->inputGranted || worker->mode != Mode::Active ||
        worker->promoting || worker->demoting || !worker->sharingAllowed() || command > 9 || !gFocusMgr.getAppHasFocus() ||
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
    if (!inputAllowed()) return false;
    if (!managed()) return true;
    // Scan callbacks have no current native event. Match every forward key
    // mapping instead of using the last message or the lossy inverse map
    // (regular digits and keypad digits can share a viewer key).
    if (!gKeyboard) return false;
    return worker->inputQuarantine.allowsLogical(key,[](unsigned int native,unsigned int logical)
    {
        KEY translated = 0;
        return gKeyboard->translateKey(static_cast<U16>(native),&translated) && translated == logical;
    });
}
void FSSessionWorker::keyReleased(unsigned int)
{
    if (managed() && gViewerWindow)
        worker->inputQuarantine.released(static_cast<unsigned int>(gViewerWindow->getWindow()->getNativeKeyData()["virtual_key"].asInteger()));
}
bool FSSessionWorker::textAllowed()
{
    if (!inputAllowed()) return false;
    if (!managed()) return true;
    // WM_CHAR records its own scan-derived origin, rather than the preceding
    // key-down. Unknown-origin IME text is still protected by input ownership.
    const auto native = static_cast<unsigned int>(gViewerWindow->getWindow()->getNativeKeyData()["virtual_key"].asInteger());
    const bool allowed = worker->inputQuarantine.allows(native);
    if (!allowed && GetTickCount64() - worker->lastInputDiagnostic >= 2000)
    {
        worker->lastInputDiagnostic = GetTickCount64();
        LL_DEBUGS("SessionInput") << "Held-origin text suppressed; generation " << worker->generation
            << "; quarantined keys " << worker->inputQuarantine.size() << LL_ENDL;
    }
    return allowed;
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
        worker->contactSearch.reset();worker->friendSnapshot.clear();
        worker->disconnectSessionObserver();worker->chatConnection.disconnect();worker->notificationConnection.disconnect();
        worker->pipe.close(); worker->detached = true;
    }
    // Keep the inherited profile lease until process/static teardown, after
    // ordinary viewer cleanup has finished saving account/settings files.
}
#else
bool FSSessionWorker::initialize() { return true; }
bool FSSessionWorker::managed() { return false; }
LLSD FSSessionWorker::assistantIdentity() {return LLSD::emptyMap();}
bool FSSessionWorker::assistantPaused() {return false;}
bool FSSessionWorker::assistantActiveAllowed() {return true;}
bool FSSessionWorker::nativeChatReadAllowed() { return true; }
void FSSessionWorker::configure() {}
void FSSessionWorker::importProfileSettings() {}
void FSSessionWorker::importProfileAccountSettings() {}
bool FSSessionWorker::importProfileToolbars() { return false; }
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
bool FSSessionWorker::backgroundUIAudioMuted() { return false; }
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
