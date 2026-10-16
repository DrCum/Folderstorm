/**
 * @file fssharedposeeditqueue.h
 * @brief Local drag previews and gesture boundaries for shared posing.
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
#ifndef FS_SHARED_POSE_EDIT_QUEUE_H
#define FS_SHARED_POSE_EDIT_QUEUE_H

#include "fssharedposemodel.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

namespace FSSharedPose
{
constexpr std::size_t MAX_PENDING_GESTURES = 32, MAX_RETAINED_ACTIONS = 8192;
struct EditGesture
{
    JointSet joints;
    std::vector<std::function<void()>> actions;
    Pose latest, lastSent;
    bool asked = false, granted = false, finished = false, inputComplete = false;

    bool accepts(const JointSet& next) const
    {
        return !inputComplete && !finished &&
            (!asked || std::includes(joints.begin(), joints.end(), next.begin(), next.end()));
    }

    template<typename Current>
    bool preview(Current current)
    {
        if (!current()) return false;
        if (!granted)
        {
            // Retain the intent so a grant can replay it against the owner's pose.
            if (!actions.empty()) actions.back()();
            return current();
        }
        auto pending = std::move(actions);
        actions.clear();
        for (const auto& action : pending)
        {
            if (!current()) return false;
            action();
        }
        return current();
    }
    void preview() { preview([] { return true; }); }
};

template<typename Gesture, typename Current>
bool currentGestureQueue(const std::deque<Gesture>& queue, Current current)
{
    return !queue.empty() && std::all_of(queue.begin(), queue.end(), current);
}

template<typename Gesture>
bool canAppendGesture(const std::deque<Gesture>& queue, const JointSet& joints)
{
    if (!queue.empty() && !queue.back().accepts(joints) && queue.size() >= MAX_PENDING_GESTURES) return false;
    std::size_t retained = 0;
    for (const auto& gesture : queue) retained += gesture.actions.size();
    return retained < MAX_RETAINED_ACTIONS;
}

template<typename Gesture>
bool gestureWaitExpired(const Gesture& gesture, double time)
{
    // A waiting legacy snapshot retains the original acquire deadline.
    return gesture.asked && (!gesture.granted || gesture.finished) && gesture.requested + 20. < time;
}

template<typename Gesture, typename Current, typename Matches>
bool gesturePacketCurrent(const std::deque<Gesture>& queue, double time, Current current, Matches matches)
{
    return currentGestureQueue(queue,current) && matches(queue.front()) && queue.front().asked &&
        !gestureWaitExpired(queue.front(),time);
}

// Native packets have no assistant task/worker authority. Keep their delayed
// POST claim separate, one-shot, and permanently cancelled after any failure.
class NativePacketSubmission
{
    std::function<bool()> mCurrent;
    bool mClaimed = false, mCancelled = false;
public:
    explicit NativePacketSubmission(std::function<bool()> current) : mCurrent(std::move(current)) {}
    bool allowed()
    {
        if (mCancelled || !mCurrent || !mCurrent()) { mCancelled = true; return false; }
        return true;
    }
    bool claim()
    {
        if (mClaimed || !allowed()) return false;
        mClaimed = true;
        return true;
    }
};

template<typename Gesture>
Gesture& appendGesture(std::deque<Gesture>& queue, const JointSet& joints, std::function<void()> action)
{
    if (queue.empty() || !queue.back().accepts(joints))
    {
        if (!queue.empty()) queue.back().inputComplete = true;
        queue.emplace_back();
    }
    auto& gesture = queue.back();
    if (!gesture.asked) gesture.joints.insert(joints.begin(), joints.end());
    gesture.actions.push_back(std::move(action));
    return gesture;
}

template<typename Gesture>
void finishGesture(std::deque<Gesture>& queue)
{
    if (!queue.empty()) queue.back().inputComplete = true;
}

template<typename Gesture>
Pose previewPose(const std::deque<Gesture>& queue)
{
    Pose pose;
    for (const auto& gesture : queue)
        for (const auto& joint : gesture.latest) pose[joint.first] = joint.second;
    return pose;
}
}
#endif
