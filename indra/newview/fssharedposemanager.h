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
#include "fssharedposeeditqueue.h"
#include <deque>
#include <functional>
#include <set>

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
    void finishEdit(const LLUUID& avatar);
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
private:
    struct Remote
    {
        LLUUID session;
        FSSharedPose::Pose pose;
        U32 revision = 0;
        double seen = 0., requestedSnapshot = -100.;
    };
    struct Approval { LLUUID session, token; double seen = 0., heartbeat = 0.; bool undo = false, redo = false; };
    struct Request { LLUUID id; double expires = 0.; };
    struct Work : FSSharedPose::EditGesture
    {
        LLUUID avatar, session, token, lease;
        double requested = 0., sent = -100., renewed = -100.;
        int history = 0;
        U32 sequence = 0;
    };
    struct Queued { LLUUID target; std::deque<std::string> packets; bool control = false, started = false; LLSD data; };
    static void onIdle(void* data);
    void tick();
    void send(const LLUUID& target, const LLSD& message, bool control = true);
    void handle(const LLUUID& sender, const LLSD& message);
    void publish();
    void snapshot(const LLUUID& target);
    void apply(const LLUUID& avatar, const FSSharedPose::Pose& pose);
    void stopRemote(const LLUUID& avatar);
    bool grantWork(const LLUUID& avatar, const FSSharedPose::Pose& pose);
    void execute(Work& work);
    void restorePreviews(const LLUUID& avatar);
    void complete(const LLUUID& avatar, bool accepted);
    void accept(const LLUUID& editor, const LLUUID& request);
    LLSD workMessage(const Work& work, const std::string& type) const;
    bool nearby(const LLUUID& avatar) const;
    FSPoserAnimator mAnimator;
    FSSharedPose::OwnerSession mOwner;
    FSSharedPose::Assembler mAssembler;
    std::map<LLUUID, Remote> mRemotes;
    std::map<LLUUID, Approval> mApprovals;
    std::map<LLUUID, Request> mRequests, mOutgoingRequests;
    std::map<LLUUID, double> mLastRequests;
    std::map<LLUUID, double> mLastIncomingRequests, mPeers;
    std::map<LLUUID, std::deque<Work>> mWorks;
    std::set<LLUUID> mBlockedEdits;
    std::map<LLUUID, std::pair<LLUUID, double>> mClosed;
    std::deque<Queued> mQueue;
    FSSharedPose::Pose mPublished;
    U32 mPublishedRevision = 0;
    LLUUID mRegion, mBridge;
    bool mApplying = false, mReady = false;
    double mLastHeartbeat = -100., mLastPublish = -100., mLastPacket = -100.;
    double mLastBeacon = -100.;
    std::string mStatus;
};
#endif
