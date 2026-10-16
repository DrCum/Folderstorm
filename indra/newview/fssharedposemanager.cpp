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
#include "fssharedposepolicy.h"
#include "fslslbridge.h"
#include "fssessionworker.h"
#include "llviewernetwork.h"
#include "llapp.h"
#include "llstartup.h"
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
    LLSD result = message("capable"); result["v"] = 1;result["typed_rotations"]=1;
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

FSSharedPoseManager::FSSharedPoseManager()
{
 gIdleCallbacks.addFunction(onIdle,this);
 mAssistantRegionConnection=gAgent.addRegionChangedCallback([this]()
 {
  mAssistantRegionGeneration.advance();
  invalidateNativeSharing("native_region_changed");
 });
 const auto disable=[this](LLControlVariable*,const LLSD& value,const LLSD&)
 {
  if(!value.asBoolean())invalidateNativeSharing("native_sharing_disabled");
 };
 if(LLControlVariable* control=gSavedSettings.getControl("FSSharedPosingEnabled"))mSharingConnection=control->getSignal()->connect(disable);
 if(LLControlVariable* control=gSavedSettings.getControl("UseLSLBridge"))mBridgeUseConnection=control->getSignal()->connect(disable);
}
FSSharedPoseManager::~FSSharedPoseManager()
{mAssistantRegionConnection.disconnect();mSharingConnection.disconnect();mBridgeUseConnection.disconnect();gIdleCallbacks.deleteFunction(onIdle,this);}
void FSSharedPoseManager::invalidateNativeSharing(const std::string& reason)
{
 // Native control disable and region transitions revoke consent even when
 // this viewer has no assistant work and publication was started manually.
 // Run synchronously so returning to the same IDs cannot revive old consent.
 for(const auto& entry:mAssistantWorks)if(entry.second->task && entry.second->task->guard())entry.second->task->guard()->cancel(reason);
 endAssistantPublishing(reason);
 mOwner.clear();mPublicGeneration=LLUUID::generateNewID();mPublished.clear();mPublishedRevision=0;
 mApprovals.clear();mRequests.clear();mOutgoingRequests.clear();mWorks.clear();mDeferred.clear();
 // Release received remote overlays before their ownership records disappear.
 // stopRemote excludes self and preserves pre-existing/changed native motions.
 std::vector<LLUUID> remotes;for(const auto& entry:mRemotes)remotes.push_back(entry.first);
 for(const auto& target:remotes)stopRemote(target);
 mQueue.clear();mClosed.clear();mPeers.clear();mTypedPeers.clear();mAssembler.clear();
 // Preserve the owner's and manually owned native Poser state. Setting mReady
 // false also prevents the legacy unavailable branch from calling stopAll.
 mReady=false;mRegion.setNull();mBridge.setNull();mStatus="Shared posing stopped";
}
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
    mPublicGeneration=LLUUID::generateNewID();mTypedPeers.clear();
    mRegion = gAgent.getRegion()->getRegionID();
    mBridge = FSLSLBridge::instance().getAttachedID();
    mPeers.clear();
    FSLSLBridge::instance().viewerToLSL("SharedPoseEnable|1");
    send(LLUUID::null, message("hello"));
    send(LLUUID::null, capability()); mLastBeacon = now();
    mStatus = "Ready";
}

void FSSharedPoseManager::send(const LLUUID& target, const LLSD& data, bool control,const std::shared_ptr<AssistantWork>& assistant)
{
    LLSD outgoing = data;
    const auto provenance=assistant?assistant:((mAssistantPublishing && data["s"].asString()==mAssistantPublishing->session.asString())?mAssistantPublishing:std::shared_ptr<AssistantWork>());
    const auto type = data["t"].asString();
    // Keep one unsent update per stream, merging disjoint joint changes.
    auto pending = std::find_if(mQueue.begin(), mQueue.end(), [&](const Queued& item)
    {
        return !item.started && item.assistant==provenance && item.target == target && item.data["t"] == data["t"] &&
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
                std::map<std::string,U32> previousRevisions,nextRevisions;
                for(const auto& joint:*previous)
                {const auto& revision=pending->data["jr"][joint.first];if(revision.isInteger() && revision.asInteger()>=0)previousRevisions[joint.first]=static_cast<U32>(revision.asInteger());}
                for(const auto& joint:*next)
                {const auto& revision=data["jr"][joint.first];if(revision.isInteger() && revision.asInteger()>=0)nextRevisions[joint.first]=static_cast<U32>(revision.asInteger());}
                const auto merged=fs_pose::coalescePoseUpdate(*previous,previousRevisions,*next,nextRevisions);
                outgoing["p"] = poseData(merged.pose);outgoing["jr"]=LLSD::emptyMap();
                for(const auto& joint:merged.revisions)outgoing["jr"][joint.first]=static_cast<S32>(joint.second);
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
    Queued entry{target, std::deque<std::string>(packets.begin(), packets.end()), control, false, outgoing,provenance};
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

void FSSharedPoseManager::shareSelf(){ensureSharedSelf(true);}
void FSSharedPoseManager::ensureSharedSelf(bool nativeTakeover)
{
    if (!available() || !nearby(gAgentID)) { mStatus = "Shared posing needs the LSL bridge"; return; }
    if (mOwner.active())
    {
        if(nativeTakeover && mAssistantPublishing)
        {
            // An explicit native Share action takes over the same native
            // publication, preserving its session, grants, leases and Poser.
            // Old MCP cleanup can no longer own or stop that publication.
            const auto previous=mAssistantPublishing;mAssistantPublishing.reset();mPublicGeneration=LLUUID::generateNewID();
            mQueue.erase(std::remove_if(mQueue.begin(),mQueue.end(),[&](const Queued& queued){return queued.assistant==previous;}),mQueue.end());
            finishAssistant(previous,"native_publication_takeover","The native owner took over publishing");snapshot(LLUUID::null);
        }
        return;
    }
    mAnimator.tryPosingAvatar(avatarFor(gAgentID));
    const auto pose = mAnimator.captureSharedPose(avatarFor(gAgentID));
    mOwner.start(gAgentID.asString(), uuid(), pose);mPublicGeneration=LLUUID::generateNewID();
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
    ensureSharedSelf(false);
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
    Applying guard(mApplying);auto* avatar=avatarFor(id);
    const auto remote=mRemotes.find(id);const bool track=id!=gAgentID && remote!=mRemotes.end();
    const auto beforeGeneration=track?mAnimator.nativePoseGeneration(avatar):LLUUID::null;
    const auto before=track?mAnimator.captureSharedPose(avatar):FSSharedPose::Pose();
    mAnimator.applySharedPose(avatar,pose);
    if(track)
    {
        const auto after=mAnimator.nativePoseGeneration(avatar);
        remote->second.overlay.observe(beforeGeneration.isNull()?std::string():beforeGeneration.asString(),before,
            after.isNull()?std::string():after.asString(),mAnimator.captureSharedPose(avatar));
    }
}
void FSSharedPoseManager::stopRemote(const LLUUID& id)
{
    const auto found = mRemotes.find(id);
    if (found != mRemotes.end())
    {
        mClosed[id] = {found->second.session, now() + 30.};
        mAnimator.stopReceivedPose(avatarFor(id),found->second.overlay);
        mRemotes.erase(found);
    }
    mApprovals.erase(id);mWorks.erase(id);
}
void FSSharedPoseManager::leave(const LLUUID& id)
{
    if (id == gAgentID) { stopAll(); return; }
    const auto approval = mApprovals.find(id);
    if (approval == mApprovals.end()) return;
    LLSD data = message("leave", approval->second.session.asString());
    data["g"] = approval->second.token;
    send(id, data);
    mApprovals.erase(id);
    mWorks.erase(id);
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
    mOwner.clear();mPublicGeneration=LLUUID::generateNewID();
    mPublished.clear();
    mRequests.clear();
    mOutgoingRequests.clear();
    mWorks.clear();
    mDeferred.clear();
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
    if (!canEdit(id) || joints.empty()) return true;
    auto found = mWorks.find(id);
    if (found != mWorks.end() && found->second.asked &&
        (found->second.finished || !std::includes(found->second.joints.begin(), found->second.joints.end(), joints.begin(), joints.end())))
    {
        if (mDeferred.size() < 64) mDeferred.push_back({id, joints, std::move(action)});
        found->second.forceFinish = true;
        return true;
    }
    auto& work = mWorks[id];
    if (work.avatar.isNull())
    {
        work.avatar = id;
        work.lease = LLUUID::generateNewID();
        if (id == gAgentID) work.session = LLUUID(mOwner.session());
        else { work.session = mApprovals.at(id).session; work.token = mApprovals.at(id).token; }
        mStatus = "Reserving joints";
    }
    if (!work.asked) work.joints.insert(joints.begin(), joints.end());
    if (work.actions.size() < 256) work.actions.push_back(std::move(action));
    if (work.granted) execute(work);
    return true;
}

void FSSharedPoseManager::execute(Work& work)
{
    Applying guard(mApplying);
    const auto remote=mRemotes.find(work.avatar);if(remote!=mRemotes.end())remote->second.overlay.detach();
    auto actions = std::move(work.actions);
    work.actions.clear();
    for (const auto& action : actions) action();
    work.latest = subset(mAnimator.captureSharedPose(avatarFor(work.avatar)), work.joints);
}

void FSSharedPoseManager::grantWork(const LLUUID& id, const FSSharedPose::Pose& pose)
{
    auto found = mWorks.find(id);
    if (found == mWorks.end() || found->second.finished) return;
    auto& work = found->second;
    if (subset(pose, work.joints).size() != work.joints.size()) { complete(id, false); return; }
    apply(id, subset(pose, work.joints));
    work.lastSent = subset(pose, work.joints);
    work.granted = true;
    mStatus = "Editing pose";
    work.renewed = now();
    if (!work.history) execute(work);
}

void FSSharedPoseManager::complete(const LLUUID& id, bool accepted)
{
    auto found = mWorks.find(id);
    if (found == mWorks.end()) return;
    if (!accepted) mStatus = "Edit cancelled; pose refreshed";
    if (id != gAgentID && mRemotes.count(id)) apply(id, subset(mRemotes.at(id).pose, found->second.joints));
    else if (id == gAgentID && mOwner.active()) apply(id, subset(mOwner.pose(), found->second.joints));
    mWorks.erase(found);
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
        mWorks.emplace(id, std::move(work));
    }
    return true;
}

void FSSharedPoseManager::snapshot(const LLUUID& target)
{
    if (!mOwner.active()) return;
    LLSD data = message("snapshot", mOwner.session());
    data["r"] = static_cast<S32>(mOwner.revision());
    data["p"] = poseData(mOwner.pose());data["jr"]=LLSD::emptyMap();
    for(const auto& joint:mOwner.pose())data["jr"][joint.first]=static_cast<S32>(mOwner.jointRevision(joint.first));
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
    data["p"] = poseData(changes);data["jr"]=LLSD::emptyMap();
    for(const auto& joint:changes)data["jr"][joint.first]=static_cast<S32>(mOwner.jointRevision(joint.first));
    send(LLUUID::null, data, false);
    mPublished = mOwner.pose();
    mPublishedRevision = mOwner.revision();
}

void FSSharedPoseManager::handle(const LLUUID& sender, const LLSD& data)
{
    const std::string type = data["t"].asString();
    const LLUUID session(data["s"].asString());
    const double time = now();
    if(handleAssistant(sender,data))return;
    if (type == "hello") { send(sender, capability()); snapshot(sender); return; }
    if (type == "capable") { if (data["v"].asInteger() == 1){mPeers[sender] = time;mTypedPeers[sender]=data["typed_rotations"].asInteger()==1;} return; }
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
            mAnimator.stopReceivedPose(avatarFor(sender),remote.overlay);
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
        for(const auto& item:*pose)
        {
            if(data["jr"][item.first].isInteger() && data["jr"][item.first].asInteger()>=0)
                remote.jointRevisions[item.first]=static_cast<U32>(data["jr"][item.first].asInteger());
            else if(!remote.pose.count(item.first) || remote.pose.at(item.first)!=item.second)remote.jointRevisions[item.first]=revision;
        }
        if (type == "snapshot") remote.pose = *pose;
        else for (const auto& item : *pose) remote.pose[item.first] = item.second;
        remote.revision = static_cast<U32>(revision);
        auto display = *pose;
        const auto work = mWorks.find(sender);
        if (work != mWorks.end() && work->second.granted)
            for (const auto& joint : work->second.joints) display.erase(joint);
        apply(sender, display);
        return;
    }
    if (type == "locked" || type == "busy" || type == "ack")
    {
        const auto found = mWorks.find(sender);
        if (found == mWorks.end() || found->second.session != session || found->second.lease != data["l"].asUUID()) return;
        if (type == "busy") { complete(sender, false); mStatus = "Joint reserved by " + nameFor(data["by"].asUUID()); return; }
        const auto pose = readPose(data["p"]);
        if (type == "locked") { if (pose) grantWork(sender, *pose); else complete(sender, false); return; }
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
        std::map<std::string,U32> expected;
        if(data["a"].asBoolean())
        {
            if(!data["e"].isMap())return;
            for(auto it=data["e"].beginMap();it!=data["e"].endMap();++it)
            {if(!it->second.isInteger() || it->second.asInteger()<0)return;expected[it->first]=static_cast<U32>(it->second.asInteger());}
        }
        const bool revisions=!data["a"].asBoolean() || mOwner.revisionsMatch(expected,joints);
        const auto acquired = revisions?mOwner.acquire(editor, token, lease, joints, time):FSSharedPose::OwnerSession::Reservation();
        LLSD reply = message(acquired.granted ? "locked" : "busy", mOwner.session());
        reply["l"] = data["l"];
        if (acquired.granted)
        {reply["p"] = poseData(subset(mOwner.pose(), joints));reply["jr"]=LLSD::emptyMap();for(const auto& joint:joints)reply["jr"][joint]=static_cast<S32>(mOwner.jointRevision(joint));}
        else reply["by"] = LLUUID(acquired.occupiedBy);
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
        else if(data["a"].asBoolean())
        {
            FSSharedPose::Pose changes;
            std::map<std::string,U32> expected;
            if(!data["q"].isMap() || data["q"].size()<1 || data["q"].size()>64 || !data["e"].isMap())return;
            for(auto it=data["q"].beginMap();it!=data["q"].endMap();++it)
            {
                if(!it->second.isArray() || it->second.size()!=4 || !mOwner.pose().count(it->first) || !assistantJointAllowed(gAgentID,it->first,mOwner.pose()))return;
                fs_pose::Rotation rotation;for(unsigned i=0;i<4;++i){if(!it->second[i].isReal() && !it->second[i].isInteger())return;rotation[i]=it->second[i].asReal();}
                if(!fs_pose::normalize(rotation) || !data["e"][it->first].isInteger() || data["e"][it->first].asInteger()<0)return;
                auto joint=fs_pose::rotationJoint(mOwner.pose().at(it->first),rotation);
                changes[it->first]=joint;joints.insert(it->first);expected[it->first]=static_cast<U32>(data["e"][it->first].asInteger());
            }
            const auto frozen=subset(before,joints),live=subset(mAnimator.captureSharedPose(avatarFor(gAgentID)),joints);
            if(fs_pose::sameSharedPose(frozen,live) && final && data["n"].asInteger()==1 && mOwner.revisionsMatch(expected,joints) &&
                mOwner.apply(editor,token,lease,changes,true,time,true,1))
            {
                fs_pose::Rotations rotations;for(const auto& joint:changes)rotations[joint.first]={{joint.second.transform[0],joint.second.transform[1],joint.second.transform[2],joint.second.transform[3]}};
                accepted=mAnimator.applyAssistantSharedRotations(avatarFor(gAgentID),frozen,rotations,[&]()
                {return available() && mOwner.session()==session.asString() && mOwner.authorized(editor,token,now());});
            }
        }
        else if (auto changes = readPose(data["p"]))
        {
            if (!data["n"].isInteger() || data["n"].asInteger() <= 0) return;
            for (const auto& item : *changes) joints.insert(item.first);
            accepted = mOwner.apply(editor, token, lease, *changes, final, time, true, static_cast<U32>(data["n"].asInteger()));
        }
        if (accepted && !data["a"].asBoolean()) apply(gAgentID, difference(before, mOwner.pose()));
        if (!final && accepted) return;
        LLSD reply = message("ack", mOwner.session());
        reply["l"] = data["l"]; reply["ok"] = accepted; reply["f"] = final;reply["r"]=static_cast<S32>(mOwner.revision());
        reply["jr"]=LLSD::emptyMap();for(const auto& joint:joints)reply["jr"][joint]=static_cast<S32>(mOwner.jointRevision(joint));
        reply["u"] = !mOwner.historyJoints(editor, false).empty();
        reply["d"] = !mOwner.historyJoints(editor, true).empty();
        if (!joints.empty()) reply["p"] = poseData(subset(mOwner.pose(), joints));
        send(sender, reply);
    }
}

void FSSharedPoseManager::tick()
{
    const double time = now();
    tickAssistant();
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
                    if (entry.control && !entry.assistant) for (const auto& packet : entry.packets)
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
        if (!nearby(it->first) || it->second + 60. <= time){mTypedPeers.erase(it->first);it = mPeers.erase(it);}else ++it;
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
    for (auto& entry : mWorks)
    {
        auto& work = entry.second;
        if (!canEdit(entry.first)) { done.emplace_back(entry.first, false); continue; }
        if (!work.asked)
        {
            work.asked = true; work.requested = time;
            if (entry.first == gAgentID)
            {
                const auto result = mOwner.acquire(gAgentID.asString(), "", work.lease.asString(), work.joints, time);
                if (result.granted) grantWork(entry.first, mOwner.pose());
                else { mStatus = "Joint reserved by " + nameFor(LLUUID(result.occupiedBy)); done.emplace_back(entry.first, true); }
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
        const bool final = work.forceFinish || !gFocusMgr.getMouseCapture();
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
    if (!mDeferred.empty() && !mWorks.count(mDeferred.front().avatar))
    {
        auto deferred = std::move(mDeferred.front()); mDeferred.pop_front();
        edit(deferred.avatar, deferred.joints, std::move(deferred.action));
    }
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
        if(entry.assistant)
        {
            const auto work=entry.assistant;const bool edit=entry.data["t"].asString()=="edit";
            if(!assistantAllowed(work))
            {finishAssistant(work,work->sourceSubmitted?"submitted_unknown":"cancelled","Authority changed before queued bridge packet");mQueue.pop_front();}
            else
            {
                const bool queued=FSLSLBridge::instance().sharedPoseToLSLGuarded(command,work->task,
                    [work,edit](bool submit)
                    {return FSSharedPoseManager::instanceExists() && FSSharedPoseManager::instance().assistantAllowed(work,submit && edit);},
                    [work](bool attempted,bool accepted)
                    {
                        if(!FSSharedPoseManager::instanceExists() || work->finished)return;
                        if(!accepted)FSSharedPoseManager::instance().finishAssistant(work,work->sourceSubmitted?"submitted_unknown":"cancelled",
                            attempted?"Bridge transport outcome is unknown; no replay":"Bridge packet cancelled before actual POST");
                        else if(work->cleanup)FSSharedPoseManager::instance().finishAssistant(work,work->publicationStop?"stop_submitted":"release_submitted","Only the exact owned session/lease cleanup was submitted; peer acknowledgement is unavailable");
                        else work->result["transport_acknowledged"]=true;
                    });
                if(queued){entry.started=true;entry.packets.pop_front();if(entry.packets.empty())mQueue.pop_front();}
                else{finishAssistant(work,work->sourceSubmitted?"submitted_unknown":"cancelled","Guarded transport rejected the packet; no retry");mQueue.pop_front();}
            }
        }
        else if (FSLSLBridge::instance().viewerToLSL(command))
        {
            entry.started = true;
            entry.packets.pop_front(); if (entry.packets.empty()) mQueue.pop_front();
        }
        mLastPacket = time;
    }
}

namespace {
LLSD assistantBinding()
{
 return llsd::map("account",gAgentID,"login",gAgent.getSessionID(),"grid",LLGridManager::instance().getGrid(),
  "region",gAgent.getRegion()?gAgent.getRegion()->getRegionID():LLUUID::null,"region_generation",std::to_string(FSSharedPoseManager::instance().assistantRegionGeneration()),"bridge",FSLSLBridge::instance().getAttachedID(),
  "worker",FSSessionWorker::assistantIdentity());
}
bool assistantReadable()
{return !gDisconnected && !LLApp::isExiting() && LLStartUp::getStartupState()==STATE_STARTED && gAgentID.notNull() && gAgent.getRegion() && FSSharedPoseManager::instance().assistantRegionGeneration()!=0;}
bool assistantActive()
{return assistantReadable() && gAgent.getTeleportState()==LLAgent::TELEPORT_NONE && FSSessionWorker::assistantActiveAllowed() && !FSSessionWorker::assistantPaused();}
LLSD revisionData(const std::map<std::string,U32>& revisions)
{LLSD result=LLSD::emptyMap();for(const auto& item:revisions)result[item.first]=static_cast<S32>(item.second);return result;}
bool rotationRequest(const LLSD& request,fs_pose::Rotations& rotations,std::map<std::string,U32>& expected,std::string& error)
{
 const auto& input=request["rotations"];const auto& revisions=request["expected_joint_revisions"];
 if(!input.isMap() || input.size()<1 || input.size()>64 || !revisions.isMap() || revisions.size()!=input.size())
 {error="Shared edits require 1-64 exact local rotations and matching expected_joint_revisions";return false;}
 for(auto it=input.beginMap();it!=input.endMap();++it)
 {
  if(!fs_pose::jointName(it->first) || !it->second.isArray() || it->second.size()!=4 || !revisions[it->first].isInteger() || revisions[it->first].asInteger()<0)
  {error="Shared edits require known non-root joint unit quaternions and nonnegative exact revisions";return false;}
  fs_pose::Rotation q;for(unsigned i=0;i<4;++i){if(!it->second[i].isReal() && !it->second[i].isInteger()){error="Quaternion components must be numbers";return false;}q[i]=it->second[i].asReal();}
  if(!fs_pose::normalize(q)){error="Shared rotations must be finite normalized quaternions";return false;}
  rotations[it->first]=q;expected[it->first]=static_cast<U32>(revisions[it->first].asInteger());
 }
 return true;
}
LLSD rotationData(const fs_pose::Rotations& rotations)
{LLSD result=LLSD::emptyMap();for(const auto& item:rotations){LLSD q=LLSD::emptyArray();for(double value:item.second)q.append(value);result[item.first]=q;}return result;}
std::string rotationLabel(const LLSD& rotation)
{std::ostringstream text;text<<"[";for(S32 i=0;i<rotation.size();++i){if(i)text<<", ";text<<rotation[i].asReal();}text<<"]";return text.str();}

}

bool FSSharedPoseManager::assistantJointAllowed(const LLUUID& target,const std::string& name,const FSSharedPose::Pose& pose) const
{
 const auto found=pose.find(name);const auto* definition=mAnimator.getPoserJointByName(name);auto* avatar=avatarFor(target);
 if(!avatar || !fs_pose::jointName(name) || !definition || definition->boneType()==WHOLEAVATAR || definition->boneType()==COL_VOLUMES ||
  found==pose.end() || !found->second.enabled || found->second.worldLocked || found->second.mirrored)return false;
 for(const auto& item:pose)
 {
  if(!item.second.worldLocked)continue;const auto* joint=avatar->getJoint(item.first);if(!joint)continue;
  for(const auto* parent=joint->getParent();parent;parent=parent->getParent())if(parent->getName()==name)return false;
 }
 return true;
}
LLSD FSSharedPoseManager::assistantSnapshot(const LLUUID& target) const
{
 LLSD result=llsd::map("target",target,"available",available(),"shared",isShared(target),"can_edit",false,"joints",LLSD::emptyMap(),
  "reservation_authority",target==gAgentID?"owner_current":"owner_reservation_required","region_id",gAgent.getRegion()?gAgent.getRegion()->getRegionID():LLUUID::null);
 if(!nearby(target))return result;
 const FSSharedPose::Pose* pose=nullptr;
 if(target==gAgentID)
 {
  result["session_generation"]=mPublicGeneration;result["grant_generation"]=mPublicGeneration;
  result["native_pose_generation"]=mAnimator.nativePoseGeneration(avatarFor(target));
  result["can_publish"]=available() && mAnimator.isPosingAvatar(avatarFor(target)) && !mOwner.active();
  if(mOwner.active()){pose=&mOwner.pose();result["pose_revision"]=static_cast<S32>(mOwner.revision());result["can_edit"]=canEdit(target);}
  else return result;
 }
 else
 {
  const auto remote=mRemotes.find(target);if(remote==mRemotes.end())return result;
  pose=&remote->second.pose;result["session_generation"]=remote->second.generation;result["pose_revision"]=static_cast<S32>(remote->second.revision);
  const auto approval=mApprovals.find(target);if(approval!=mApprovals.end())result["grant_generation"]=approval->second.generation;
  const auto typed=mTypedPeers.find(target);result["typed_rotation_protocol"]=typed!=mTypedPeers.end() && typed->second;
  result["can_edit"]=canEdit(target) && typed!=mTypedPeers.end() && typed->second;
 }
 if(!pose)return result;
 for(const auto& item:*pose)
 {
  if(!fs_pose::jointName(item.first) || !mAnimator.getPoserJointByName(item.first))continue;
  LLSD joint;LLSD q=LLSD::emptyArray();for(unsigned i=0;i<4;++i)q.append(item.second.transform[i]);joint["rotation"]=q;
  if(target==gAgentID){joint["revision"]=static_cast<S32>(mOwner.jointRevision(item.first));const auto owner=mOwner.reservationOwner(item.first,now());joint["reserved"]=!owner.empty();}
  else{const auto& revisions=mRemotes.at(target).jointRevisions;const auto revision=revisions.find(item.first);joint["revision"]=static_cast<S32>(revision==revisions.end()?0:revision->second);joint["reservation_known"]=false;}
  joint["editable"]=result["can_edit"].asBoolean() && assistantJointAllowed(target,item.first,*pose);result["joints"][item.first]=joint;
 }
 return result;
}
bool FSSharedPoseManager::assistantNativeValid(const std::shared_ptr<AssistantWork>& work) const
{
 if(!work || work->binding!=assistantBinding() || !assistantActive() || !available() || !nearby(work->target) ||
  ((!work->publishing || work->cleanup) && now()-work->started>60.))return false;
 if(work->publicationStop && work->cleanup && work->target==gAgentID && work->session.notNull())return true;
 if(work->target==gAgentID)
 {
  if(!mOwner.active() || mOwner.session()!=work->session.asString() || mPublicGeneration!=work->generation ||
     work->nativeMotionGeneration.isNull() || mAnimator.nativePoseGeneration(avatarFor(gAgentID))!=work->nativeMotionGeneration)return false;
  // Initial Ask/submission freezes exact native bytes. Once published, the
  // native owner and explicitly granted peers may keep editing that same
  // publication. Its original source task owns only its session/motion life.
  if(work->publishing)return mAssistantPublishing==work;
 }
 else
 {
  const auto approval=mApprovals.find(work->target);const auto remote=mRemotes.find(work->target);
  if(approval==mApprovals.end() || remote==mRemotes.end() || !canEdit(work->target) || approval->second.token!=work->token ||
   approval->second.session!=work->session || approval->second.generation!=work->grantGeneration || remote->second.generation!=work->generation ||
   !compatible(work->target) || !mTypedPeers.count(work->target) || !mTypedPeers.at(work->target))return false;
 }
 if(work->cleanup)return true;
 const auto& pose=work->target==gAgentID?mOwner.pose():mRemotes.at(work->target).pose;
 for(const auto& joint:work->before)
 {
  const auto current=pose.find(joint.first);if(current==pose.end() || !assistantJointAllowed(work->target,joint.first,pose))return false;
  if(!work->sourceSubmitted)
  {
   const auto revision=work->target==gAgentID?mOwner.jointRevision(joint.first):mRemotes.at(work->target).jointRevisions.at(joint.first);
   if(revision!=work->expected.at(joint.first) || !fs_pose::sameSharedJoint(current->second,joint.second))return false;
  }
  if(work->target==gAgentID && !work->sourceSubmitted)
  {const auto native=mAnimator.captureSharedPose(avatarFor(gAgentID));const auto actual=native.find(joint.first);if(actual==native.end() || !fs_pose::sameSharedJoint(actual->second,joint.second))return false;}
  if(work->sourceSubmitted && !fs_pose::rotationResult(joint.second,current->second,work->rotations.at(joint.first)))return false;
 }
 return true;
}
bool FSSharedPoseManager::assistantAllowed(const std::shared_ptr<AssistantWork>& work,bool submission) const
{
 if(!work || work->finished || !work->task || !work->task->guard() || !assistantNativeValid(work) || !fs_assistant::asyncTaskAllowed(work->task))return false;
 if(submission && !work->publishing && !work->cleanup && work->result["phase"].asString()=="edit")
 {work->sourceSubmitted=true;work->result["state"]="submitted";work->result["uncertain"]=true;}
 return true;
}
void FSSharedPoseManager::sendAssistant(const std::shared_ptr<AssistantWork>& work,const LLSD& data,bool control)
{send(work->target,data,control,work);}
void FSSharedPoseManager::finishAssistant(const std::shared_ptr<AssistantWork>& work,const std::string& state,const std::string& reason)
{
 if(!work || work->finished)return;
 work->finished=true;work->result["state"]=state;work->result["reason"]=reason.substr(0,512);
 work->result["owner_accepted"]=state=="owner_accepted";
 work->result["uncertain"]=work->sourceSubmitted && state!="owner_accepted" && state!="owner_rejected";
 work->result["lease_release_pending_expiry"]=work->granted && !work->sourceSubmitted;
 // No global refresh/undo/mouse-capture action: the adapter never performs
 // speculative local target edits. Only this operation's lease can expire.
 if(work->target==gAgentID && !work->sourceSubmitted)mOwner.release(gAgentID.asString(),work->lease.asString());
 work->task.reset();
}
void FSSharedPoseManager::endAssistantPublishing(const std::string& reason)
{
 const auto publishing=mAssistantPublishing;if(!publishing)return;
 mAssistantPublishing.reset();
 mQueue.erase(std::remove_if(mQueue.begin(),mQueue.end(),[&](const Queued& queued){return queued.data["s"].asString()==publishing->session.asString();}),mQueue.end());
 if(mOwner.active() && mOwner.session()==publishing->session.asString())
 {
  // End only publishing/consent; never stop the owner's native Poser motion.
  mOwner.clear();mPublished.clear();mPublicGeneration=LLUUID::generateNewID();
  mWorks.erase(gAgentID);
 }
 finishAssistant(publishing,"cancelled",reason);
}
void FSSharedPoseManager::tickAssistant()
{
 if(mAssistantPublishing && !assistantAllowed(mAssistantPublishing))endAssistantPublishing("Publishing authority, native motion or sharing session changed");
 std::vector<std::shared_ptr<AssistantWork>> works;
 for(const auto& entry:mAssistantWorks)if((!entry.second->publishing || entry.second->cleanup) && !entry.second->finished)works.push_back(entry.second);
 for(const auto& work:works)
 {
  if(!assistantAllowed(work)){finishAssistant(work,work->sourceSubmitted?"submitted_unknown":"cancelled","Authority, owner grant, target session or joint revision changed");continue;}
  if(!work->cleanup && work->granted && work->renewed+1.<=now())
  {
   if(work->target==gAgentID)mOwner.renew(gAgentID.asString(),"",work->lease.asString(),now());
   else{LLSD renew=message("renew",work->session.asString());renew["g"]=work->token;renew["l"]=work->lease;sendAssistant(work,renew);}
   work->renewed=now();
  }
 }
}
bool FSSharedPoseManager::prepareAssistantPose(FSAssistantPreparedOperation& operation,std::string& error) const
{
 auto& request=operation.request;const auto op=request["op"].asString();const bool read=op=="sharedSessions" || op=="sharedGrants";
 operation.validation=llsd::map("binding",assistantBinding(),"epoch",request["__assistant_epoch"]);
 if(!assistantReadable()){error="A current connected native avatar session is required";return false;}
 if(read)return true;
 if(!assistantActive() || !available()){error="RequiresActive: shared posing needs the explicitly enabled native sharing bridge";return false;}
 const auto target=request["target"].asUUID();const auto source=assistantSnapshot(target);
 if(target.isNull() || !nearby(target) || request["session_generation"].asUUID()!=source["session_generation"].asUUID())
 {error="Target or opaque native sharing generation changed; list shared sessions again";return false;}
 operation.validation["source"]=source;
 if(op=="sharedEdit")
 {
  if(!source["can_edit"].asBoolean()){error="The target owner's existing native grant and typed rotation protocol are required; MCP approval cannot supply consent";return false;}
  fs_pose::Rotations rotations;std::map<std::string,U32> expected;
  if(request["cancel"].asBoolean())
  {
   const auto found=mAssistantWorks.find(request["operation_id"].asUUID());
   if(found==mAssistantWorks.end() || found->second->epoch!=request["__assistant_epoch"].asString() || found->second->binding!=assistantBinding() || found->second->target!=target)
   {error="Only this source's exact shared operation can be cancelled";return false;}
   operation.validation["cancel_operation"]=request["operation_id"];
  }
  else
  {
   if(!rotationRequest(request,rotations,expected,error))return false;
   for(const auto& joint:rotations)
    if(!source["joints"][joint.first]["editable"].asBoolean() || source["joints"][joint.first]["revision"].asInteger()!=static_cast<S32>(expected.at(joint.first)))
    {error="Joint revision, enabled state, mirror or world-lock conflict; request a fresh native snapshot";return false;}
   operation.validation["rotations"]=rotationData(rotations);operation.validation["expected"]=revisionData(expected);
  }
 }
 else if(op=="startSharing")
 {
  if(target!=gAgentID || mOwner.active() || !mAnimator.isPosingAvatar(avatarFor(gAgentID)))
  {error="Publishing requires an already active ordinary native self-Poser session; private assistant transactions cannot be published implicitly";return false;}
  operation.validation["native_pose"]=poseData(mAnimator.captureSharedPose(avatarFor(gAgentID)));
 }
 else if(op=="stopSharing")
 {if(target!=gAgentID || !mOwner.active()){error="Only the current native self publishing session can be stopped";return false;}}
 else{error="Unsupported shared pose operation";return false;}
 request["__pose_shared_key"]=LLUUID::generateNewID();
 if(std::find(operation.requiredClasses.begin(),operation.requiredClasses.end(),"pose_read")==operation.requiredClasses.end())operation.requiredClasses.push_back("pose_read");
 operation.summary=llsd::map("op",op,"subject_count",1,"subjects",LLSD::emptyArray(),"consequences",LLSD::emptyArray(),
  "reference_frame","local","rotations",operation.validation["rotations"],"visibility","native_shared_pose_peers");
 operation.summary["subjects"].append(llsd::map("id",target,"name",nameFor(target),"after_name",op=="startSharing"?
  "Publish existing native Poser state, including its current native position/scale state":op=="stopSharing"?"End native publishing and grants while keeping the owner's pose":request["cancel"].asBoolean()?"Cancel only operation "+request["operation_id"].asString():"Reserve exact joints and request owner-authorized rotation changes"));
 operation.summary["action"]=op=="startSharing"?"Publish this existing native pose?":op=="stopSharing"?"Stop this native pose publication?":request["cancel"].asBoolean()?"Cancel this shared-pose operation?":"Request these owner-authorized rotations?";
 for(auto joint=operation.validation["rotations"].beginMap();joint!=operation.validation["rotations"].endMap();++joint)
  operation.summary["subjects"].append(llsd::map("name",joint->first,"before_name",rotationLabel(source["joints"][joint->first]["rotation"]),
   "after_name",rotationLabel(joint->second)+"; local quaternion; expected joint revision "+operation.validation["expected"][joint->first].asString()));
 if(op=="startSharing")
 {
  const auto pose=readPose(operation.validation["native_pose"]);if(!pose){error="Frozen native publication state is invalid";return false;}
  for(const auto& joint:*pose)
  {
   std::ostringstream text;text<<"local rotation [";
   for(unsigned i=0;i<4;++i){if(i)text<<", ";text<<joint.second.transform[i];}
   text<<"], position [";for(unsigned i=4;i<7;++i){if(i>4)text<<", ";text<<joint.second.transform[i];}
   text<<"], scale [";for(unsigned i=7;i<10;++i){if(i>7)text<<", ";text<<joint.second.transform[i];}
   text<<"], priority "<<static_cast<unsigned>(joint.second.priority)<<", enabled "<<joint.second.enabled
    <<", world lock "<<joint.second.worldLocked<<", mirror "<<joint.second.mirrored;
   operation.summary["subjects"].append(llsd::map("name",joint.first,"after_name",text.str()));
  }
 }
 operation.summary["subject_count"]=operation.summary["subjects"].size();
 operation.summary["consequences"].append("pose_native_shared_consent");return true;
}
bool FSSharedPoseManager::validateAssistantPose(const FSAssistantPreparedOperation& operation,std::string& error) const
{
 const auto& request=operation.request;
 if(!assistantReadable() || operation.validation["binding"]!=assistantBinding() || operation.validation["epoch"]!=request["__assistant_epoch"])
 {error="Shared pose source account, profile, region or bridge changed";return false;}
 const auto op=request["op"].asString();if(op=="sharedSessions" || op=="sharedGrants")return true;
 for(const auto& entry:mAssistantWorks)
  if(entry.second->prepared==request["__pose_shared_key"].asUUID() && entry.second->epoch==request["__assistant_epoch"].asString())
  {
   if(assistantNativeValid(entry.second))return true;error="Shared pose native authority changed";return false;
  }
 if(!assistantActive() || !available() || assistantSnapshot(request["target"].asUUID())!=operation.validation["source"])
 {error="Shared pose session, grant, reservations or joint revisions changed before submission";return false;}
 if(op=="startSharing" && poseData(mAnimator.captureSharedPose(avatarFor(gAgentID)))!=operation.validation["native_pose"])
 {error="Native pose changed before publication approval";return false;}
 return true;
}
LLSD FSSharedPoseManager::executeAssistantPose(const LLSD& request,const fs_assistant::AsyncTaskPtr& task,std::string& error)
{
 const auto op=request["op"].asString();const auto epoch=request["__assistant_epoch"].asString();
 if(!assistantReadable()){error="Native avatar connection ended before private delivery";return LLSD();}
 if(op=="sharedSessions" || op=="sharedGrants")
 {
  if(request.has("operation_id"))
  {
   const auto found=mAssistantWorks.find(request["operation_id"].asUUID());
   if(found==mAssistantWorks.end() || found->second->epoch!=epoch || found->second->binding!=assistantBinding())
   {error="Shared operation belongs to an expired source or connection";return LLSD();}
   return found->second->result;
  }
  LLSD result;result["available"]=available();result["sessions"]=LLSD::emptyArray();
  if(request["target"].asUUID().notNull()){result["session"]=assistantSnapshot(request["target"].asUUID());return result;}
  const auto append=[&](const LLUUID& id){auto summary=assistantSnapshot(id);summary.erase("joints");result["sessions"].append(summary);};
  // Include the current self/native source even before publishing, so callers
  // can obtain an opaque generation for an explicit startSharing request.
  append(gAgentID);
  for(const auto& remote:mRemotes)
  {if(result["sessions"].size()>=64)break;if(nearby(remote.first) && (op!="sharedGrants" || canEdit(remote.first)))append(remote.first);}
  return result;
 }
 if(!task || !task->guard() || !fs_assistant::asyncTaskAllowed(task) || !assistantActive() || !available())
 {error="Shared operation authority is unavailable";return LLSD();}
 if(op=="sharedEdit" && request["cancel"].asBoolean())
 {
  const auto found=mAssistantWorks.find(request["operation_id"].asUUID());
  if(found==mAssistantWorks.end() || found->second->epoch!=epoch || found->second->binding!=assistantBinding())
  {error="Only the exact source-owned operation can be cancelled";return LLSD();}
  const auto cancelled=found->second;
  if(mAssistantWorks.size()<64 && cancelled->granted && !cancelled->sourceSubmitted && !cancelled->finished && cancelled->target!=gAgentID)
  {
   auto release=std::make_shared<AssistantWork>(*cancelled);release->operation=request["reqid"].asUUID();release->prepared=request["__pose_shared_key"].asUUID();
   release->cleanup=true;release->finished=false;release->task=task;release->started=now();
   release->result=llsd::map("operation_id",release->operation,"cancelled_operation_id",cancelled->operation,"target",cancelled->target,"state","release_pending","owner_accepted",false,"uncertain",false);
   cancelled->result["cleanup_operation_id"]=release->operation;
   mAssistantWorks[release->operation]=release;LLSD packet=message("release",release->session.asString());packet["g"]=release->token;packet["l"]=release->lease;sendAssistant(release,packet);
  }
  finishAssistant(cancelled,cancelled->sourceSubmitted?"submitted_unknown":"cancelled","Cancelled by this source; no source replay or global pose reset");return cancelled->result;
 }
 for(auto it=mAssistantWorks.begin();it!=mAssistantWorks.end() && mAssistantWorks.size()>=64;)
  if(it->second->finished)it=mAssistantWorks.erase(it);else ++it;
 if(mAssistantWorks.size()>=64){error="Too many active shared operations";return LLSD();}
 auto work=std::make_shared<AssistantWork>();work->operation=request["reqid"].asUUID();work->prepared=request["__pose_shared_key"].asUUID();work->target=request["target"].asUUID();
 work->binding=assistantBinding();work->epoch=epoch;work->task=task;work->started=now();work->lease=LLUUID::generateNewID();
 const auto source=assistantSnapshot(work->target);
 if(source["session_generation"].asUUID()!=request["session_generation"].asUUID()){error="Native sharing generation changed";return LLSD();}
 work->generation=source["session_generation"].asUUID();work->grantGeneration=source["grant_generation"].asUUID();
 if(work->target==gAgentID)work->nativeMotionGeneration=mAnimator.nativePoseGeneration(avatarFor(gAgentID));
 work->result=llsd::map("operation_id",work->operation,"target",work->target,"session_generation",work->generation,
  "state","pending","owner_accepted",false,"uncertain",false,"rotation_only",true);
 if(op=="startSharing")
 {
  if(work->target!=gAgentID || mOwner.active() || !mAnimator.isPosingAvatar(avatarFor(gAgentID)) || !fs_assistant::asyncTaskSubmit(task))
  {error="Native self pose or publication authority changed";return LLSD();}
  work->before=mAnimator.captureSharedPose(avatarFor(gAgentID));
  if(!FSSharedPose::validPose(work->before)){error="Native pose cannot be published";return LLSD();}
  mOwner.start(gAgentID.asString(),uuid(),work->before);mPublicGeneration=LLUUID::generateNewID();
  work->session=LLUUID(mOwner.session());work->generation=mPublicGeneration;work->publishing=true;
  work->result["session_generation"]=work->generation;work->result["state"]="published_local_session";work->result["remote_synchronization"]="unconfirmed";
  mAssistantWorks[work->operation]=work;mAssistantPublishing=work;mPublished.clear();mPublishedRevision=0;snapshot(LLUUID::null);mPublished=work->before;
  return work->result;
 }
 if(op=="stopSharing")
 {
  if(work->target!=gAgentID || !mOwner.active() || !fs_assistant::asyncTaskSubmit(task)){error="Native publication changed";return LLSD();}
  // Explicit stop affects this exact publishing/consent session only. No
  // native motion or newer publishing session is stopped.
  work->session=LLUUID(mOwner.session());work->cleanup=true;work->publicationStop=true;work->publishing=true;
  mAssistantWorks[work->operation]=work;
  mQueue.erase(std::remove_if(mQueue.begin(),mQueue.end(),[&](const Queued& queued){return queued.data["s"].asString()==work->session.asString();}),mQueue.end());
  if(mAssistantPublishing)endAssistantPublishing("Explicit publishing stop");
  else{mOwner.clear();mPublished.clear();mPublicGeneration=LLUUID::generateNewID();}
  work->result["state"]="publishing_stopped_local";work->result["native_pose_preserved"]=true;work->result["remote_synchronization"]="unconfirmed_until_native_peer_timeout";
  send(LLUUID::null,message("stop",work->session.asString()),true,work);return work->result;
 }
 if(op!="sharedEdit" || !source["can_edit"].asBoolean() || !rotationRequest(request,work->rotations,work->expected,error))
 {if(error.empty())error="Native owner consent and typed rotation protocol are required";return LLSD();}
 FSSharedPose::JointSet joints;const auto& pose=work->target==gAgentID?mOwner.pose():mRemotes.at(work->target).pose;
 for(const auto& joint:work->rotations)
 {
  if(!source["joints"][joint.first]["editable"].asBoolean() || source["joints"][joint.first]["revision"].asInteger()!=static_cast<S32>(work->expected.at(joint.first)))
  {error="Joint revision or effect-set conflict";return LLSD();}
  joints.insert(joint.first);work->before[joint.first]=pose.at(joint.first);
 }
 if(work->target==gAgentID)work->session=LLUUID(mOwner.session());
 else{work->session=mApprovals.at(work->target).session;work->token=mApprovals.at(work->target).token;}
 mAssistantWorks[work->operation]=work;
 if(work->target==gAgentID)
 {
  if(!assistantAllowed(work) || !mOwner.revisionsMatch(work->expected,joints)){finishAssistant(work,"conflicted","Native joint revisions changed");return work->result;}
  const auto acquired=mOwner.acquire(gAgentID.asString(),"",work->lease.asString(),joints,now());
  if(!acquired.granted){finishAssistant(work,"conflicted","Another editor reserved an affected joint");return work->result;}
  work->granted=true;auto changes=work->before;for(auto& joint:changes)joint.second=fs_pose::rotationJoint(joint.second,work->rotations.at(joint.first));
  if(!fs_assistant::asyncTaskSubmit(task) || !assistantAllowed(work)){finishAssistant(work,"cancelled","Authority changed before owned joint commit");return work->result;}
  work->sourceSubmitted=true;
  if(!mOwner.apply(gAgentID.asString(),"",work->lease.asString(),changes,true,now(),true,1)){finishAssistant(work,"owner_rejected","Owner reservation/revision refused");return work->result;}
  if(!mAnimator.applyAssistantSharedRotations(avatarFor(gAgentID),work->before,work->rotations,[&](){return assistantAllowed(work);}))
  {finishAssistant(work,"submitted_unknown","Native joint ownership changed after the owner model accepted the edit");return work->result;}
  finishAssistant(work,"owner_accepted","");return work->result;
 }
 LLSD acquire=message("acquire",work->session.asString());acquire["g"]=work->token;acquire["l"]=work->lease;acquire["a"]=true;acquire["e"]=revisionData(work->expected);
 acquire["j"]=LLSD::emptyArray();for(const auto& joint:joints)acquire["j"].append(joint);
 sendAssistant(work,acquire);work->result["phase"]="acquire";return work->result;
}
bool FSSharedPoseManager::handleAssistant(const LLUUID& sender,const LLSD& data)
{
 const auto type=data["t"].asString();if(type!="locked" && type!="busy" && type!="ack")return false;
 for(const auto& entry:mAssistantWorks)
 {
  const auto work=entry.second;
  if(work->target!=sender || work->session.asString()!=data["s"].asString() || work->lease!=data["l"].asUUID())continue;
  if(work->binding!=assistantBinding()){finishAssistant(work,work->sourceSubmitted?"submitted_unknown":"cancelled","Late native callback belongs to another login/region/bridge");return true;}
  if(work->finished && !(type=="ack" && work->sourceSubmitted && work->result["state"].asString()=="submitted_unknown"))return true;
  if(type=="ack")
  {
   const auto pose=readPose(data["p"]);bool confirmed=work->granted && work->sourceSubmitted && data["f"].asBoolean() && data["ok"].isBoolean();
   if(data["ok"].asBoolean())
    for(const auto& joint:work->before)
     if(!pose || !pose->count(joint.first) || !fs_pose::rotationResult(joint.second,pose->at(joint.first),work->rotations.at(joint.first),false))confirmed=false;
   if(!confirmed){if(!work->finished)finishAssistant(work,"submitted_unknown","Malformed or premature owner acknowledgement");return true;}
   // Historical owner acceptance does not authorize an old session/grant to
   // overwrite the current cache. Require exact private native provenance and
   // monotonically advance only still-existing, explicitly requested joints.
   unsigned updated=0;bool currentSource=false;
   const auto remote=mRemotes.find(sender);const auto approval=mApprovals.find(sender);
   if(pose && remote!=mRemotes.end() && approval!=mApprovals.end() && canEdit(sender))
   {
    const fs_pose::SharedAcknowledgementSource expected{work->session.asString(),work->generation.asString(),work->session.asString(),work->grantGeneration.asString(),work->token.asString()},
     current{remote->second.session.asString(),remote->second.generation.asString(),approval->second.session.asString(),approval->second.generation.asString(),approval->second.token.asString()};
    currentSource=fs_pose::currentAcknowledgementSource(expected,current);
    FSSharedPose::JointSet requested;std::map<std::string,U32> revisions;
    for(const auto& joint:work->before)
    {
     requested.insert(joint.first);const auto& revision=data["jr"][joint.first];
     if(revision.isInteger() && revision.asInteger()>=0 && data["r"].isInteger() && data["r"].asInteger()>=revision.asInteger())revisions[joint.first]=static_cast<U32>(revision.asInteger());
    }
    updated=fs_pose::updateAcknowledgedCache(expected,current,requested,*pose,revisions,remote->second.pose,remote->second.jointRevisions);
   }
   work->result["acknowledgement_applies_to_current_session"]=currentSource;work->result["current_cache_joints_updated"]=static_cast<S32>(updated);
   work->finished=false;
   finishAssistant(work,data["ok"].asBoolean()?"owner_accepted":"owner_rejected",data["ok"].asBoolean()?"":"Owner refused final joint commit");return true;
  }
  if(!assistantAllowed(work)){finishAssistant(work,work->sourceSubmitted?"submitted_unknown":"cancelled","Native owner grant or source authority changed while reserving");return true;}
  if(type=="busy"){finishAssistant(work,"conflicted","Joint reservation or expected revision conflict");return true;}
  if(work->granted)return true; // duplicate locked replies never replay finalization
  const auto pose=readPose(data["p"]);
  if(!pose || !fs_pose::sameSharedPose(subset(*pose,[&](){FSSharedPose::JointSet names;for(const auto& joint:work->before)names.insert(joint.first);return names;}()),work->before))
  {finishAssistant(work,"conflicted","Reserved joint snapshot differs from the approved effect set");return true;}
  for(const auto& expected:work->expected)
   if(!data["jr"][expected.first].isInteger() || data["jr"][expected.first].asInteger()!=static_cast<S32>(expected.second))
   {finishAssistant(work,"conflicted","Owner joint revision changed before reservation");return true;}
  work->granted=true;work->renewed=now();work->result["state"]="reserved";
  LLSD renew=message("renew",work->session.asString());renew["g"]=work->token;renew["l"]=work->lease;sendAssistant(work,renew);
  LLSD edit=message("edit",work->session.asString());edit["g"]=work->token;edit["l"]=work->lease;edit["a"]=true;edit["e"]=revisionData(work->expected);
  edit["q"]=rotationData(work->rotations);edit["f"]=true;edit["n"]=1;work->result["phase"]="edit";sendAssistant(work,edit);return true;
 }
 return false;
}
