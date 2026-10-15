/**
 * @file fssharedposemanager.cpp
 * @brief Owner-authoritative shared posing through the existing LSL bridge.
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "fssharedposemanager.h"
#include "fslslbridge.h"
#include "fsfloaterposer.h"
#include "llagent.h"
#include "llavatarnamecache.h"
#include "llbase64.h"
#include "llcallbacklist.h"
#include "llfloaterreg.h"
#include "llfocusmgr.h"
#include "llmutelist.h"
#include "llnotificationsutil.h"
#include "llsdserialize.h"
#include "lltimer.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include <chrono>
#include <sstream>

namespace
{
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::string uuid() { return LLUUID::generateNewID().asString(); }
LLVOAvatar* avatarFor(const LLUUID& id)
{
    if (id.isNull()) return nullptr;
    for (auto* character : LLCharacter::sInstances)
        if (character->getID() == id) return dynamic_cast<LLVOAvatar*>(character);
    return nullptr;
}
std::string nameFor(const LLUUID& id)
{
    LLAvatarName name;
    return LLAvatarNameCache::get(id, &name) ? name.getCompleteName() : id.asString();
}
LLSD message(const std::string& type, const std::string& session = {})
{
    LLSD result = LLSD::emptyMap();
    result["t"] = type;
    result["s"] = session;
    return result;
}
LLSD capability()
{
    LLSD result = message("capable"); result["v"] = 1;
    return result;
}
LLSD poseData(const FSSharedPose::Pose& pose)
{
    const auto bytes = FSSharedPose::encodePose(pose);
    return LLSD::Binary(bytes.begin(), bytes.end());
}
std::optional<FSSharedPose::Pose> readPose(const LLSD& data)
{
    if (!data.isBinary() || data.size() > 50000) return {};
    const auto& bytes = data.asBinary();
    return FSSharedPose::decodePose(std::string(bytes.begin(), bytes.end()));
}
FSSharedPose::Pose subset(const FSSharedPose::Pose& pose, const FSSharedPose::JointSet& joints)
{
    FSSharedPose::Pose result;
    for (const auto& name : joints) if (pose.count(name)) result.emplace(name, pose.at(name));
    return result;
}
FSSharedPose::Pose difference(const FSSharedPose::Pose& before, const FSSharedPose::Pose& after)
{
    FSSharedPose::Pose result;
    for (const auto& item : after)
        if (!before.count(item.first) || before.at(item.first) != item.second) result.emplace(item);
    return result;
}
struct Applying
{
    bool& value;
    bool previous;
    explicit Applying(bool& flag) : value(flag), previous(flag) { value = true; }
    ~Applying() { value = previous; }
};
}

FSSharedPoseManager::FSSharedPoseManager() { gIdleCallbacks.addFunction(onIdle, this); }
FSSharedPoseManager::~FSSharedPoseManager() { gIdleCallbacks.deleteFunction(onIdle, this); }
void FSSharedPoseManager::onIdle(void* data) { static_cast<FSSharedPoseManager*>(data)->tick(); }

bool FSSharedPoseManager::available() const
{
    return gAgent.getRegion() && gAgentID.notNull() && gAgent.getTeleportState() == LLAgent::TELEPORT_NONE &&
        gSavedSettings.getBOOL("FSSharedPosingEnabled") && FSLSLBridge::instance().canUseBridge() &&
        FSLSLBridge::instance().isSharedPoseReady();
}
bool FSSharedPoseManager::nearby(const LLUUID& id) const
{
    const auto* avatar = avatarFor(id);
    return avatar && !avatar->isDead() && !avatar->isControlAvatar() &&
        avatar->getRegion() == gAgent.getRegion() && !LLMuteList::instance().isMuted(id);
}
bool FSSharedPoseManager::isShared(const LLUUID& id) const
{
    return (id == gAgentID && mOwner.active()) || mRemotes.count(id) || mApprovals.count(id);
}
bool FSSharedPoseManager::canEdit(const LLUUID& id) const
{
    if (!available() || !nearby(id)) return false;
    if (id == gAgentID) return mOwner.active();
    const auto grant = mApprovals.find(id);
    const auto remote = mRemotes.find(id);
    return grant != mApprovals.end() && remote != mRemotes.end() && !remote->second.pose.empty() &&
        grant->second.session == remote->second.session && grant->second.seen + FSSharedPose::PEER_SECONDS > now();
}
bool FSSharedPoseManager::compatible(const LLUUID& id) const
{
    const auto found = mPeers.find(id);
    return nearby(id) && found != mPeers.end() && found->second + 60. > now();
}

void FSSharedPoseManager::bridgeReady()
{
    if (!available()) return;
    if (mReady) stopAll();
    mReady = true;
    mRegion = gAgent.getRegion()->getRegionID();
    mBridge = FSLSLBridge::instance().getAttachedID();
    mPeers.clear();
    FSLSLBridge::instance().viewerToLSL("SharedPoseEnable|1");
    send(LLUUID::null, message("hello"));
    send(LLUUID::null, capability()); mLastBeacon = now();
    mStatus = "Ready";
}

void FSSharedPoseManager::send(const LLUUID& target, const LLSD& data, bool control)
{
    LLSD outgoing = data;
    const auto type = data["t"].asString();
    // Keep one unsent update per stream, merging disjoint joint changes.
    auto pending = std::find_if(mQueue.begin(), mQueue.end(), [&](const Queued& item)
    {
        return !item.started && item.target == target && item.data["t"] == data["t"] &&
            item.data["s"] == data["s"] && item.data["l"] == data["l"] &&
            (type == "update" || type == "snapshot" || type == "edit" || type == "renew" ||
             type == "heartbeat" || type == "permit" || type == "alive");
    });
    if (pending != mQueue.end())
    {
        if (type == "update")
        {
            const auto previous = readPose(pending->data["p"]);
            const auto next = readPose(data["p"]);
            if (previous && next)
            {
                auto merged = *previous;
                for (const auto& item : *next) merged[item.first] = item.second;
                outgoing["p"] = poseData(merged);
                outgoing["b"] = pending->data["b"];
            }
        }
        mQueue.erase(pending);
    }
    std::ostringstream binary;
    LLSDSerialize::toBinary(outgoing, binary);
    const auto bytes = binary.str();
    const auto encoded = LLBase64::encode(reinterpret_cast<const U8*>(bytes.data()), bytes.size());
    const auto packets = FSSharedPose::fragment(uuid(), encoded);
    if (packets.empty()) return;
    if (mQueue.size() >= 64)
    {
        auto discard = std::find_if(mQueue.begin(), mQueue.end(), [](const Queued& item) { return !item.control; });
        if (discard == mQueue.end()) return;
        mQueue.erase(discard);
    }
    Queued entry{target, std::deque<std::string>(packets.begin(), packets.end()), control, false, outgoing};
    if (type == "renew" || type == "heartbeat" || type == "permit" || type == "alive" || type == "stop" || type == "revoke")
        mQueue.push_front(std::move(entry));
    else if (control)
    {
        const auto position = std::find_if(mQueue.begin(), mQueue.end(), [](const Queued& item) { return !item.control; });
        mQueue.insert(position, std::move(entry));
    }
    else mQueue.push_back(std::move(entry));
}

void FSSharedPoseManager::receive(const LLUUID& owner, const LLUUID& object, const std::string& packet)
{
    if (!available() || !mReady || owner == gAgentID || !nearby(owner) || object.isNull()) return;
    const auto encoded = mAssembler.receive(owner.asString(), packet, now());
    if (!encoded || encoded->find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") !=
        std::string::npos) return;
    const auto binary = LLBase64::decodeAsString(*encoded);
    if (binary.empty() || binary.size() > 60000) return;
    std::istringstream stream(binary);
    LLSD data;
    if (LLSDSerialize::fromBinary(data, stream, static_cast<llssize>(binary.size()), 12) == LLSDParser::PARSE_FAILURE ||
        !data.isMap() || !data["t"].isString() || data["t"].asString().size() > 16) return;
    handle(owner, data);
}

void FSSharedPoseManager::shareSelf()
{
    if (!available() || !nearby(gAgentID)) { mStatus = "Shared posing needs the LSL bridge"; return; }
    if (mOwner.active()) return;
    mAnimator.tryPosingAvatar(avatarFor(gAgentID));
    const auto pose = mAnimator.captureSharedPose(avatarFor(gAgentID));
    mOwner.start(gAgentID.asString(), uuid(), pose);
    if (!mOwner.active()) return;
    mPublished.clear();
    mPublishedRevision = 0;
    snapshot(LLUUID::null);
    mPublished = pose;
    mLastHeartbeat = now();
    mStatus = "Sharing your pose";
}

void FSSharedPoseManager::requestPermission(const LLUUID& id)
{
    if (!available() || id == gAgentID || !compatible(id)) { mStatus = "Shared posing is unavailable"; return; }
    const double time = now();
    auto found = mLastRequests.find(id);
    if (found != mLastRequests.end() && found->second + 30. > time) return;
    mLastRequests[id] = time;
    const LLUUID request = LLUUID::generateNewID();
    mOutgoingRequests[id] = {request, time + 30.};
    LLSD data = message("request");
    data["id"] = request;
    send(id, data);
    mStatus = "Permission requested";
}

void FSSharedPoseManager::accept(const LLUUID& editor, const LLUUID& request)
{
    const auto found = mRequests.find(editor);
    if (!available() || !nearby(editor) || found == mRequests.end() || found->second.id != request ||
        found->second.expires <= now()) return;
    mRequests.erase(found);
    shareSelf();
    if (!mOwner.active()) return;
    const LLUUID token = LLUUID::generateNewID();
    mOwner.grant(editor.asString(), token.asString(), now());
    LLSD data = message("grant", mOwner.session());
    data["id"] = request;
    data["g"] = token;
    send(editor, data);
    snapshot(editor);
    LLFloaterReg::showInstance("fs_poser");
    mStatus = nameFor(editor) + " can edit your pose";
}

void FSSharedPoseManager::removeEditor(const LLUUID& editor)
{
    if (!mOwner.active()) return;
    mOwner.revoke(editor.asString());
    send(editor, message("revoke", mOwner.session()));
    mStatus = "Editing permission removed";
}
std::vector<LLUUID> FSSharedPoseManager::editors() const
{
    std::vector<LLUUID> result;
    for (const auto& editor : mOwner.editors()) result.emplace_back(editor);
    return result;
}

void FSSharedPoseManager::apply(const LLUUID& id, const FSSharedPose::Pose& pose)
{
    Applying guard(mApplying);
    mAnimator.applySharedPose(avatarFor(id), pose);
}
void FSSharedPoseManager::stopRemote(const LLUUID& id)
{
    const auto found = mRemotes.find(id);
    if (found != mRemotes.end()) mClosed[id] = {found->second.session, now() + 30.};
    mRemotes.erase(id);
    mApprovals.erase(id);
    mWorks.erase(id);
    mBlockedEdits.erase(id);
    Applying guard(mApplying);
    mAnimator.stopPosingAvatar(avatarFor(id));
}
void FSSharedPoseManager::leave(const LLUUID& id)
{
    if (id == gAgentID) { stopAll(); return; }
    const auto approval = mApprovals.find(id);
    if (approval == mApprovals.end()) return;
    LLSD data = message("leave", approval->second.session.asString());
    data["g"] = approval->second.token;
    send(id, data);
    complete(id, false);
    mApprovals.erase(id);
    mBlockedEdits.erase(id);
    mStatus = "Editing session ended";
}
void FSSharedPoseManager::stopAll()
{
    const bool owned = mOwner.active();
    mQueue.clear();
    if (mOwner.active()) send(LLUUID::null, message("stop", mOwner.session()));
    std::vector<LLUUID> approvals;
    for (const auto& item : mApprovals) approvals.push_back(item.first);
    for (const auto& id : approvals) leave(id);
    mOwner.clear();
    mPublished.clear();
    mRequests.clear();
    mOutgoingRequests.clear();
    mWorks.clear();
    mBlockedEdits.clear();
    mAssembler.clear();
    Applying guard(mApplying);
    if (owned) mAnimator.stopPosingAvatar(avatarFor(gAgentID));
    mStatus = "Shared posing stopped";
}

LLSD FSSharedPoseManager::workMessage(const Work& work, const std::string& type) const
{
    LLSD result = message(type, work.session.asString());
    result["g"] = work.token;
    result["l"] = work.lease;
    return result;
}

bool FSSharedPoseManager::edit(const LLUUID& id, const FSSharedPose::JointSet& joints, std::function<void()> action)
{
    if (mApplying || !isShared(id)) return false;
    if (!canEdit(id) || joints.empty() || mBlockedEdits.count(id)) return true;
    auto& work = FSSharedPose::appendGesture(mWorks[id], joints, std::move(action));
    if (work.avatar.isNull())
    {
        work.avatar = id;
        work.lease = LLUUID::generateNewID();
        if (id == gAgentID) work.session = LLUUID(mOwner.session());
        else { work.session = mApprovals.at(id).session; work.token = mApprovals.at(id).token; }
        mStatus = "Reserving joints";
    }
    execute(work);
    return true;
}

void FSSharedPoseManager::finishEdit(const LLUUID& id)
{
    const auto found = mWorks.find(id);
    if (found != mWorks.end()) FSSharedPose::finishGesture(found->second);
    mBlockedEdits.erase(id);
}

void FSSharedPoseManager::execute(Work& work)
{
    Applying guard(mApplying);
    work.preview();
    work.latest = subset(mAnimator.captureSharedPose(avatarFor(work.avatar)), work.joints);
}

void FSSharedPoseManager::restorePreviews(const LLUUID& id)
{
    const auto found = mWorks.find(id);
    if (found != mWorks.end()) apply(id, FSSharedPose::previewPose(found->second));
}

bool FSSharedPoseManager::grantWork(const LLUUID& id, const FSSharedPose::Pose& pose)
{
    auto found = mWorks.find(id);
    if (found == mWorks.end() || found->second.front().finished) return false;
    auto& work = found->second.front();
    if (work.granted) return true;
    if (subset(pose, work.joints).size() != work.joints.size()) return false;
    apply(id, subset(pose, work.joints));
    work.lastSent = subset(pose, work.joints);
    work.granted = true;
    mStatus = "Editing pose";
    work.renewed = now();
    if (!work.history) execute(work);
    restorePreviews(id);
    return true;
}

void FSSharedPoseManager::complete(const LLUUID& id, bool accepted)
{
    auto found = mWorks.find(id);
    if (found == mWorks.end()) return;
    auto joints = found->second.front().joints;
    if (!accepted)
    {
        mStatus = "Edit cancelled; pose refreshed";
        // A refused drag must also cancel its queued previews and network work.
        for (const auto& work : found->second)
        {
            joints.insert(work.joints.begin(), work.joints.end());
            mQueue.erase(std::remove_if(mQueue.begin(), mQueue.end(), [&](const Queued& packet)
                { return packet.target == id && packet.data["l"].asUUID() == work.lease; }), mQueue.end());
        }
        const auto& work = found->second.front();
        if (id == gAgentID) mOwner.release(gAgentID.asString(), work.lease.asString());
        else if (work.asked) send(id, workMessage(work, "release"));
        mWorks.erase(found);
        mBlockedEdits.insert(id);
    }
    else
    {
        found->second.pop_front();
        if (found->second.empty()) mWorks.erase(found);
    }
    if (id != gAgentID && mRemotes.count(id)) apply(id, subset(mRemotes.at(id).pose, joints));
    else if (id == gAgentID && mOwner.active()) apply(id, subset(mOwner.pose(), joints));
    restorePreviews(id);
}

bool FSSharedPoseManager::canHistory(const LLUUID& id, bool redo) const
{
    if (!canEdit(id)) return false;
    if (id == gAgentID) return !mOwner.historyJoints(gAgentID.asString(), redo).empty();
    return redo ? mApprovals.at(id).redo : mApprovals.at(id).undo;
}
bool FSSharedPoseManager::history(const LLUUID& id, bool redo)
{
    if (!isShared(id)) return false;
    if (!canEdit(id) || mWorks.count(id)) return true;
    Work work;
    work.avatar = id;
    work.lease = LLUUID::generateNewID();
    work.history = redo ? 2 : 1;
    work.inputComplete = true;
    work.asked = true;
    work.requested = now();
    if (id == gAgentID)
    {
        const auto editor = gAgentID.asString();
        work.session = LLUUID(mOwner.session());
        work.joints = mOwner.historyJoints(editor, redo);
        const auto acquired = mOwner.acquire(editor, "", work.lease.asString(), work.joints, now());
        const auto before = mOwner.pose();
        if (!acquired.granted || !mOwner.restore(editor, "", work.lease.asString(), redo, now()))
        {
            mOwner.release(editor, work.lease.asString());
            mStatus = "Undo unavailable: another editor changed these joints";
        }
        else apply(id, difference(before, mOwner.pose()));
    }
    else
    {
        work.session = mApprovals.at(id).session;
        work.token = mApprovals.at(id).token;
        LLSD data = workMessage(work, "history");
        data["redo"] = redo;
        send(id, data);
        mWorks[id].push_back(std::move(work));
    }
    return true;
}

void FSSharedPoseManager::snapshot(const LLUUID& target)
{
    if (!mOwner.active()) return;
    LLSD data = message("snapshot", mOwner.session());
    data["r"] = static_cast<S32>(mOwner.revision());
    data["p"] = poseData(mOwner.pose());
    send(target, data, false);
}
void FSSharedPoseManager::publish()
{
    if (!mOwner.active()) return;
    const auto changes = difference(mPublished, mOwner.pose());
    if (changes.empty()) return;
    LLSD data = message("update", mOwner.session());
    data["b"] = static_cast<S32>(mPublishedRevision);
    data["r"] = static_cast<S32>(mOwner.revision());
    data["p"] = poseData(changes);
    send(LLUUID::null, data, false);
    mPublished = mOwner.pose();
    mPublishedRevision = mOwner.revision();
}

void FSSharedPoseManager::handle(const LLUUID& sender, const LLSD& data)
{
    const std::string type = data["t"].asString();
    const LLUUID session(data["s"].asString());
    const double time = now();
    if (type == "hello") { send(sender, capability()); snapshot(sender); return; }
    if (type == "capable") { if (data["v"].asInteger() == 1) mPeers[sender] = time; return; }
    if (type == "request")
    {
        const LLUUID request = data["id"].asUUID();
        if (request.isNull() || mRequests.count(sender)) return;
        const auto last = mLastIncomingRequests.find(sender);
        if (last != mLastIncomingRequests.end() && last->second + 30. > time) return;
        mLastIncomingRequests[sender] = time;
        mRequests[sender] = {request, time + 30.};
        LLSD args;
        args["NAME"] = nameFor(sender);
        LLNotificationsUtil::add("FSSharedPoseRequest", args, LLSD(), [sender, request](const LLSD& notification, const LLSD& response)
        {
            auto& manager = FSSharedPoseManager::instance();
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0) manager.accept(sender, request);
            else
            {
                const auto pending = manager.mRequests.find(sender);
                if (pending == manager.mRequests.end() || pending->second.id != request) return;
                manager.mRequests.erase(pending);
                LLSD reply = message("decline");
                reply["id"] = request;
                manager.send(sender, reply);
            }
        });
        return;
    }
    if (type == "grant" || type == "decline")
    {
        auto request = mOutgoingRequests.find(sender);
        if (request == mOutgoingRequests.end() || request->second.expires <= time || request->second.id != data["id"].asUUID()) return;
        mOutgoingRequests.erase(request);
        if (type == "decline") { mStatus = "Permission declined"; return; }
        const auto token = data["g"].asUUID();
        if (session.isNull() || token.isNull()) return;
        mApprovals[sender] = {session, token, time, 0.};
        if (mRemotes.count(sender) && mRemotes.at(sender).session != session) stopRemote(sender);
        // A snapshot can arrive before the grant; keep it when its session matches.
        mApprovals[sender] = {session, token, time, 0.};
        mStatus = "Permission granted";
        return;
    }
    if (session.isNull()) return;
    if (type == "snapshot" || type == "update" || type == "alive" || type == "stop" || type == "revoke")
    {
        auto closed = mClosed.find(sender);
        if (closed != mClosed.end() && closed->second.first == session && closed->second.second > time) return;
        if (type == "stop")
        {
            if (mRemotes.count(sender) && mRemotes.at(sender).session == session) stopRemote(sender);
            mClosed[sender] = {session, time + 30.};
            return;
        }
        if (type == "revoke")
        {
            if (mApprovals.count(sender) && mApprovals.at(sender).session == session) { mApprovals.erase(sender); complete(sender, false); }
            mStatus = "Editing permission removed";
            return;
        }
        auto& remote = mRemotes[sender];
        if (remote.session != session)
        {
            if (type != "snapshot" && !remote.session.isNull()) return;
            remote = Remote(); remote.session = session;
        }
        remote.seen = time;
        if (type == "alive")
        {
            if ((remote.pose.empty() || data["r"].asInteger() > static_cast<S32>(remote.revision)) && remote.requestedSnapshot + 2. < time)
            {
                remote.requestedSnapshot = time;
                send(sender, message("hello"));
            }
            return;
        }
        const auto pose = readPose(data["p"]);
        const S32 revision = data["r"].asInteger();
        if (!pose || revision < 0 || static_cast<U32>(revision) < remote.revision) return;
        if (type == "update" && (remote.pose.empty() || data["b"].asInteger() > static_cast<S32>(remote.revision)))
        {
            if (remote.requestedSnapshot + 2. < time) { remote.requestedSnapshot = time; send(sender, message("hello")); }
            return;
        }
        if (type == "snapshot") remote.pose = *pose;
        else for (const auto& item : *pose) remote.pose[item.first] = item.second;
        remote.revision = static_cast<U32>(revision);
        auto display = *pose;
        const auto work = mWorks.find(sender);
        if (work != mWorks.end())
            for (const auto& gesture : work->second)
                for (const auto& joint : gesture.joints) display.erase(joint);
        apply(sender, display);
        return;
    }
    if (type == "locked" || type == "busy" || type == "ack")
    {
        const auto found = mWorks.find(sender);
        if (found == mWorks.end() || found->second.front().session != session || found->second.front().lease != data["l"].asUUID()) return;
        const auto pose = readPose(data["p"]);
        if (type == "busy")
        {
            if (pose && mRemotes.count(sender))
                for (const auto& item : *pose) mRemotes.at(sender).pose[item.first] = item.second;
            complete(sender, false); mStatus = "Joint reserved by " + nameFor(data["by"].asUUID()); return;
        }
        if (type == "locked") { if (!pose || !grantWork(sender, *pose)) complete(sender, false); return; }
        if (mApprovals.count(sender)) { mApprovals.at(sender).undo = data["u"].asBoolean(); mApprovals.at(sender).redo = data["d"].asBoolean(); }
        if (pose && mRemotes.count(sender)) for (const auto& item : *pose) mRemotes.at(sender).pose[item.first] = item.second;
        if (data["f"].asBoolean() || !data["ok"].asBoolean()) complete(sender, data["ok"].asBoolean());
        return;
    }
    if (type == "permit")
    {
        auto approval = mApprovals.find(sender);
        if (approval != mApprovals.end() && approval->second.session == session && approval->second.token == data["g"].asUUID())
            approval->second.seen = time;
        return;
    }
    if (!mOwner.active() || session.asString() != mOwner.session()) return;
    const auto token = data["g"].asUUID().asString();
    const auto editor = sender.asString();
    const auto lease = data["l"].asUUID().asString();
    if (!mOwner.authorized(editor, token, time)) return;
    mOwner.heartbeat(editor, token, time);
    if (type == "heartbeat")
    {
        LLSD reply = message("permit", mOwner.session()); reply["g"] = data["g"];
        send(sender, reply);
        return;
    }
    if (type == "leave") { mOwner.revoke(editor); return; }
    if (type == "renew") { mOwner.renew(editor, token, lease, time); return; }
    if (type == "release") { mOwner.release(editor, lease); return; }
    if (type == "acquire")
    {
        FSSharedPose::JointSet joints;
        if (!data["j"].isArray() || data["j"].size() > static_cast<S32>(FSSharedPose::MAX_JOINTS)) return;
        for (auto it = data["j"].beginArray(); it != data["j"].endArray(); ++it)
        { if (!it->isString()) return; joints.insert(it->asString()); }
        const auto acquired = mOwner.acquire(editor, token, lease, joints, time);
        LLSD reply = message(acquired.granted ? "locked" : "busy", mOwner.session());
        reply["l"] = data["l"];
        reply["p"] = poseData(subset(mOwner.pose(), joints));
        if (!acquired.granted) reply["by"] = LLUUID(acquired.occupiedBy);
        send(sender, reply);
        return;
    }
    if (type == "edit" || type == "history")
    {
        const auto before = mOwner.pose();
        bool accepted = false;
        FSSharedPose::JointSet joints;
        const bool final = type == "history" || data["f"].asBoolean();
        if (type == "history")
        {
            const bool redo = data["redo"].asBoolean();
            joints = mOwner.historyJoints(editor, redo);
            const auto acquired = mOwner.acquire(editor, token, lease, joints, time);
            accepted = acquired.granted && mOwner.restore(editor, token, lease, redo, time);
            if (!accepted) mOwner.release(editor, lease);
        }
        else if (auto changes = readPose(data["p"]))
        {
            if (!data["n"].isInteger() || data["n"].asInteger() <= 0) return;
            for (const auto& item : *changes) joints.insert(item.first);
            accepted = mOwner.apply(editor, token, lease, *changes, final, time, true, static_cast<U32>(data["n"].asInteger()));
        }
        if (accepted) apply(gAgentID, difference(before, mOwner.pose()));
        if (!final && accepted) return;
        LLSD reply = message("ack", mOwner.session());
        reply["l"] = data["l"]; reply["ok"] = accepted; reply["f"] = final;
        reply["u"] = !mOwner.historyJoints(editor, false).empty();
        reply["d"] = !mOwner.historyJoints(editor, true).empty();
        if (!joints.empty()) reply["p"] = poseData(subset(mOwner.pose(), joints));
        send(sender, reply);
    }
}

void FSSharedPoseManager::tick()
{
    const double time = now();
    if (!available())
    {
        if (mReady)
        {
            stopAll();
            std::vector<LLUUID> ids;
            for (const auto& item : mRemotes) ids.push_back(item.first);
            for (const auto& id : ids) stopRemote(id);
            if (FSLSLBridge::instance().canUseBridge())
            {
                for (const auto& entry : mQueue)
                    if (entry.control) for (const auto& packet : entry.packets)
                        FSLSLBridge::instance().viewerToLSL(entry.target.isNull() ? "SharedPoseBroadcast|" + packet :
                            "SharedPoseSend|" + entry.target.asString() + "|" + packet);
            }
            FSLSLBridge::instance().viewerToLSL("SharedPoseEnable|0");
            mQueue.clear(); mReady = false;
        }
        return;
    }
    if (!mReady || mRegion != gAgent.getRegion()->getRegionID() || mBridge != FSLSLBridge::instance().getAttachedID())
        bridgeReady();
    mOwner.expire(time);
    for (auto it = mClosed.begin(); it != mClosed.end();)
        if (it->second.second <= time) it = mClosed.erase(it); else ++it;
    for (auto* requests : {&mLastRequests, &mLastIncomingRequests})
        for (auto it = requests->begin(); it != requests->end();)
            if (it->second + 30. <= time) it = requests->erase(it); else ++it;
    for (auto it = mPeers.begin(); it != mPeers.end();)
        if (!nearby(it->first) || it->second + 60. <= time) it = mPeers.erase(it); else ++it;
    for (auto it = mRequests.begin(); it != mRequests.end();)
        if (it->second.expires <= time) it = mRequests.erase(it); else ++it;
    for (auto it = mOutgoingRequests.begin(); it != mOutgoingRequests.end();)
        if (it->second.expires <= time) { it = mOutgoingRequests.erase(it); mStatus = "No posing permission received"; } else ++it;
    std::vector<LLUUID> expired;
    for (const auto& item : mRemotes)
        if (!nearby(item.first) || item.second.seen + FSSharedPose::PEER_SECONDS <= time) expired.push_back(item.first);
    for (const auto& id : expired) stopRemote(id);
    for (auto it = mApprovals.begin(); it != mApprovals.end();)
    {
        if (!nearby(it->first) || it->second.seen + FSSharedPose::PEER_SECONDS <= time)
        {
            complete(it->first, false); it = mApprovals.erase(it); continue;
        }
        if (it->second.heartbeat + 2. <= time)
        {
            LLSD data = message("heartbeat", it->second.session.asString()); data["g"] = it->second.token;
            send(it->first, data); it->second.heartbeat = time;
        }
        ++it;
    }
    std::vector<std::pair<LLUUID, bool>> done;
    std::string busyStatus;
    if (!gFocusMgr.getMouseCapture())
    {
        for (auto& entry : mWorks) FSSharedPose::finishGesture(entry.second);
        mBlockedEdits.clear();
    }
    for (auto& entry : mWorks)
    {
        auto& work = entry.second.front();
        if (!canEdit(entry.first)) { done.emplace_back(entry.first, false); continue; }
        if (!work.asked)
        {
            work.asked = true; work.requested = time;
            if (entry.first == gAgentID)
            {
                const auto result = mOwner.acquire(gAgentID.asString(), "", work.lease.asString(), work.joints, time);
                if (result.granted)
                {
                    if (!grantWork(entry.first, mOwner.pose())) { done.emplace_back(entry.first, false); continue; }
                }
                else { busyStatus = "Joint reserved by " + nameFor(LLUUID(result.occupiedBy)); done.emplace_back(entry.first, false); }
            }
            else
            {
                LLSD data = workMessage(work, "acquire");
                data["j"] = LLSD::emptyArray(); for (const auto& joint : work.joints) data["j"].append(joint);
                send(entry.first, data);
            }
        }
        if (work.granted && work.renewed + 1. <= time)
        {
            if (entry.first == gAgentID) mOwner.renew(gAgentID.asString(), "", work.lease.asString(), time);
            else send(entry.first, workMessage(work, "renew"));
            work.renewed = time;
        }
        if (work.finished) { if (work.requested + 20. < time) done.emplace_back(entry.first, false); continue; }
        if (!work.granted) { if (work.requested + 20. < time) done.emplace_back(entry.first, false); continue; }
        const bool final = work.inputComplete;
        if (!work.latest.empty() && (final || (work.latest != work.lastSent && work.sent + .2 <= time)))
        {
            if (entry.first == gAgentID)
            {
                if (!mOwner.apply(gAgentID.asString(), "", work.lease.asString(), work.latest, final, time))
                    done.emplace_back(entry.first, false);
                else if (final) done.emplace_back(entry.first, true);
            }
            else
            {
                LLSD data = workMessage(work, "edit"); data["p"] = poseData(work.latest);
                data["f"] = final; data["n"] = static_cast<S32>(++work.sequence);
                send(entry.first, data, final);
                if (final) { work.finished = true; work.requested = time; }
            }
            work.sent = time;
            work.lastSent = work.latest;
        }
    }
    for (const auto& item : done) complete(item.first, item.second);
    if (!busyStatus.empty()) mStatus = busyStatus;
    if (mLastPublish + .2 <= time) { publish(); mLastPublish = time; }
    if (mOwner.active() && mLastHeartbeat + 2. <= time)
    {
        LLSD data = message("alive", mOwner.session()); data["r"] = static_cast<S32>(mOwner.revision());
        send(LLUUID::null, data); mLastHeartbeat = time;
    }
    if (mLastBeacon + 30. <= time) { send(LLUUID::null, capability()); mLastBeacon = time; }
    // One packet per 100 ms bounds bridge work, including large snapshots.
    if (!mQueue.empty() && mLastPacket + .1 <= time)
    {
        auto& entry = mQueue.front();
        const auto packet = entry.packets.front();
        const std::string command = entry.target.isNull() ? "SharedPoseBroadcast|" + packet :
            "SharedPoseSend|" + entry.target.asString() + "|" + packet;
        if (FSLSLBridge::instance().viewerToLSL(command))
        {
            entry.started = true;
            entry.packets.pop_front(); if (entry.packets.empty()) mQueue.pop_front();
        }
        mLastPacket = time;
    }
}
