/**
 * @file fssharedposemanager.h
 * @brief Viewer adapter for bridge-backed shared avatar posing.
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
#ifndef FS_SHARED_POSE_MANAGER_H
#define FS_SHARED_POSE_MANAGER_H

#include "llsingleton.h"
#include "lluuid.h"
#include "llsd.h"
#include "fsposeranimator.h"
#include "fssharedposemodel.h"
#include "fsassistantfeature.h"
#include "fsposepolicy.h"
#include "fssharedposepolicy.h"
#include <deque>
#include <boost/signals2/connection.hpp>
#include <functional>

class FSSharedPoseManager : public LLSingleton<FSSharedPoseManager>
{
    LLSINGLETON(FSSharedPoseManager);
    ~FSSharedPoseManager();
public:
    bool available() const;
    bool canEdit(const LLUUID& avatar) const;
    bool compatible(const LLUUID& avatar) const;
    bool isShared(const LLUUID& avatar) const;
    bool isApplying() const { return mApplying; }
    bool hasSessions() const { return mOwner.active() || !mApprovals.empty(); }
    bool edit(const LLUUID& avatar, const FSSharedPose::JointSet& joints, std::function<void()> action);
    bool history(const LLUUID& avatar, bool redo);
    bool canHistory(const LLUUID& avatar, bool redo) const;
    void shareSelf();
    void requestPermission(const LLUUID& avatar);
    void stopAll();
    void leave(const LLUUID& avatar);
    void removeEditor(const LLUUID& editor);
    std::vector<LLUUID> editors() const;
    void bridgeReady();
    void receive(const LLUUID& owner, const LLUUID& object, const std::string& packet);
    std::string status() const { return mStatus; }
    // Typed assistant adapter: native owner grants remain private and can only
    // originate from the ordinary FSSharedPoseRequest notification flow.
    bool prepareAssistantPose(FSAssistantPreparedOperation& operation,std::string& error) const;
    bool validateAssistantPose(const FSAssistantPreparedOperation& operation,std::string& error) const;
    LLSD executeAssistantPose(const LLSD& request,const fs_assistant::AsyncTaskPtr& task,std::string& error);
    LLSD assistantSnapshot(const LLUUID& target) const;
    std::uint64_t assistantRegionGeneration() const{return mAssistantRegionGeneration.value();}
    LLUUID assistantPrivacyGeneration() const{return mAssistantPrivacyGeneration;}
private:
    struct Remote
    {
        LLUUID session,generation=LLUUID::generateNewID();
        FSSharedPose::Pose pose;
        fs_pose::ReceivedOverlay overlay;
        std::map<std::string,U32> jointRevisions;
        U32 revision = 0;
        double seen = 0., requestedSnapshot = -100.;
    };
    struct Approval { LLUUID session, token; double seen = 0., heartbeat = 0.; bool undo = false, redo = false; LLUUID generation=LLUUID::generateNewID(); };
    struct Request { LLUUID id; double expires = 0.; };
    struct Work
    {
        LLUUID avatar, session, token, lease,generation,grantGeneration;
        FSSharedPose::JointSet joints;
        std::vector<std::function<void()>> actions;
        FSSharedPose::Pose latest, lastSent, legacyLockedPose;
        double requested = 0., sent = -100., renewed = -100.;
        bool asked = false, granted = false, finished = false, forceFinish = false, awaitingLegacySnapshot = false;
        int history = 0;
        U32 sequence = 0;
    };
    struct AssistantWork
    {
        LLUUID operation,prepared,target,session,token,lease,generation,grantGeneration,nativeMotionGeneration;
        fs_pose::Rotations rotations;
        std::map<std::string,U32> expected;
        FSSharedPose::Pose before;
        LLSD binding,result;
        std::string epoch;
        fs_assistant::AsyncTaskPtr task;
        double started=0.,renewed=-100.;
        bool granted=false,sourceSubmitted=false,finished=false,publishing=false,cleanup=false,publicationStop=false;
    };
    struct Queued { LLUUID target; std::deque<std::string> packets; bool control = false, started = false; LLSD data; std::shared_ptr<AssistantWork> assistant; };
    struct Deferred { LLUUID avatar; FSSharedPose::JointSet joints; std::function<void()> action; };
    static void onIdle(void* data);
    void tick();
    void tickAssistant();
    bool handleAssistant(const LLUUID& sender,const LLSD& data);
    bool assistantNativeValid(const std::shared_ptr<AssistantWork>& work) const;
    bool assistantAllowed(const std::shared_ptr<AssistantWork>& work,bool submission=false) const;
    void finishAssistant(const std::shared_ptr<AssistantWork>& work,const std::string& state,const std::string& reason);
    void sendAssistant(const std::shared_ptr<AssistantWork>& work,const LLSD& data,bool control=true);
    bool assistantJointAllowed(const LLUUID& target,const std::string& joint,const FSSharedPose::Pose& pose) const;
    void endAssistantPublishing(const std::string& reason);
    void send(const LLUUID& target, const LLSD& message, bool control = true,const std::shared_ptr<AssistantWork>& assistant = {});
    void handle(const LLUUID& sender, const LLSD& message);
    void publish();
    void snapshot(const LLUUID& target);
    void apply(const LLUUID& avatar, const FSSharedPose::Pose& pose);
    void stopRemote(const LLUUID& avatar);
    void grantWork(const LLUUID& avatar, const FSSharedPose::Pose& pose);
    void execute(Work& work);
    bool nativeWorkCurrent(const Work& work) const;
    void complete(const LLUUID& avatar, bool accepted);
    void accept(const LLUUID& editor, const LLUUID& request);
    void ensureSharedSelf(bool nativeTakeover);
    void invalidateNativeSharing(const std::string& reason);
    LLSD workMessage(const Work& work, const std::string& type) const;
    bool nearby(const LLUUID& avatar) const;
    boost::signals2::scoped_connection mSharingConnection,mBridgeUseConnection,mAssistantRegionConnection,mAssistantPrivacyConnection;
    fs_pose::RegionGeneration mAssistantRegionGeneration;
    LLUUID mAssistantPrivacyGeneration=LLUUID::generateNewID();
    FSPoserAnimator mAnimator;
    FSSharedPose::OwnerSession mOwner;
    FSSharedPose::Assembler mAssembler;
    std::map<LLUUID, Remote> mRemotes;
    std::map<LLUUID,std::shared_ptr<AssistantWork>> mAssistantWorks;
    std::shared_ptr<AssistantWork> mAssistantPublishing;
    std::map<LLUUID,bool> mTypedPeers;
    LLUUID mPublicGeneration=LLUUID::generateNewID();
    std::map<LLUUID, Approval> mApprovals;
    std::map<LLUUID, Request> mRequests, mOutgoingRequests;
    std::map<LLUUID, double> mLastRequests;
    std::map<LLUUID, double> mLastIncomingRequests, mPeers;
    std::map<LLUUID, Work> mWorks;
    std::map<LLUUID, std::pair<LLUUID, double>> mClosed;
    std::deque<Queued> mQueue;
    std::deque<Deferred> mDeferred;
    FSSharedPose::Pose mPublished;
    U32 mPublishedRevision = 0;
    LLUUID mRegion, mBridge;
    bool mApplying = false, mReady = false;
    double mLastHeartbeat = -100., mLastPublish = -100., mLastPacket = -100.;
    double mLastBeacon = -100.;
    std::string mStatus;
};
#endif
