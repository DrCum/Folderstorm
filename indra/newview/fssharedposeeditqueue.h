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

    void preview()
    {
        if (!granted)
        {
            // Retain the intent so a grant can replay it against the owner's pose.
            if (!actions.empty()) actions.back()();
            return;
        }
        auto pending = std::move(actions);
        actions.clear();
        for (const auto& action : pending) action();
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
