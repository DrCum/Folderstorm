/**
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
#include "../fssharedposemodel.h"
#include "../fssharedposeeditqueue.h"
#include <cassert>
#include <limits>

using namespace FSSharedPose;

void previewBeforeReservation()
{
    const Pose initial{{"mHead", {}}};
    OwnerSession owner;
    owner.start("owner", "preview", initial);
    owner.grant("editor", "token", 0);
    std::deque<EditGesture> queue;
    float localPosition = 0.f;
    for (int frame = 0; frame < 300; ++frame)
    {
        auto& gesture = appendGesture(queue, {"mHead"}, [&] { localPosition += 1.f; });
        gesture.asked = true;
        gesture.preview();
        assert(localPosition == static_cast<float>(frame + 1));
        assert(queue.size() == 1 && owner.pose() == initial);
    }
    finishGesture(queue);
    assert(queue.front().inputComplete);

    // The owner moved before granting the reservation. Replay the intent once
    // against that canonical pose, rather than applying the preview twice.
    Pose canonical = initial;
    canonical.at("mHead").transform[4] = 10.f;
    assert(owner.acquire("owner", "", "owner-move", {"mHead"}, 0).granted);
    assert(owner.apply("owner", "", "owner-move", canonical, true, 0));
    assert(owner.acquire("editor", "token", "drag", queue.front().joints, 1).granted);
    localPosition = owner.pose().at("mHead").transform[4];
    queue.front().granted = true;
    queue.front().preview();
    assert(localPosition == 310.f && queue.front().actions.empty());
    canonical.at("mHead").transform[4] = localPosition;
    assert(owner.apply("editor", "token", "drag", canonical, true, 1, true, 1));
    assert(owner.pose() == canonical);
    assert(owner.historyJoints("editor", false) == JointSet{"mHead"});
}

void rapidRegrabGroupsWholeDrags()
{
    std::deque<EditGesture> queue;
    float localPosition = 0.f;
    auto& first = appendGesture(queue, {"mHead"}, [&] { localPosition += 1.f; });
    first.preview();
    first.asked = first.granted = true;
    finishGesture(queue);
    first.finished = true; // The final update is waiting for its acknowledgment.

    for (int frame = 0; frame < 120; ++frame)
    {
        auto& second = appendGesture(queue, {"mHead"}, [&] { localPosition += 1.f; });
        second.preview();
        assert(queue.size() == 2 && second.actions.size() == static_cast<std::size_t>(frame + 1));
        assert(localPosition == static_cast<float>(frame + 2));
    }
    finishGesture(queue);
    assert(queue.back().inputComplete);

    // A quick release and re-grab stays a new gesture even between idle ticks.
    auto& third = appendGesture(queue, {"mHead"}, [&] { localPosition += 1.f; });
    third.preview();
    finishGesture(queue);
    assert(queue.size() == 3 && queue.back().actions.size() == 1);

    queue.pop_front();
    localPosition = 1.f;
    queue.front().asked = queue.front().granted = true;
    queue.front().preview();
    assert(localPosition == 121.f && queue.front().actions.empty());
    queue.pop_front();
    queue.front().granted = true;
    queue.front().preview();
    assert(localPosition == 122.f);
    queue.pop_front();
    assert(queue.empty());
}

void reservationScopeAndLiveUpdates()
{
    std::deque<EditGesture> queue;
    int calls = 0;
    auto& first = appendGesture(queue, {"mWristLeft"}, [&] { ++calls; });
    first.preview();
    appendGesture(queue, {"mWristRight"}, [&] { ++calls; }).preview();
    assert(queue.size() == 1 && first.joints.size() == 2 && calls == 2);
    first.asked = true;
    calls = 0; // Restore the canonical pose before accepting the grant.
    first.granted = true;
    first.preview();
    assert(calls == 2 && first.actions.empty());
    appendGesture(queue, {"mWristLeft"}, [&] { ++calls; }).preview();
    assert(queue.size() == 1 && first.actions.empty() && calls == 3);

    // A new joint cannot expand an already requested reservation. Its entire
    // gesture waits together and previews while the old scope is finalized.
    appendGesture(queue, {"mHead"}, [&] { ++calls; }).preview();
    appendGesture(queue, {"mHead", "mWristLeft"}, [&] { ++calls; }).preview();
    assert(queue.size() == 2 && first.inputComplete && calls == 5);
    assert(queue.back().actions.size() == 2 && queue.back().joints.size() == 2);
}

void newerPreviewsSurviveOlderConfirmations()
{
    std::deque<EditGesture> queue(3);
    Joint first, second, third, other;
    first.transform[4] = 1.f;
    second.transform[4] = 2.f;
    third.transform[4] = 3.f;
    other.transform[4] = 4.f;
    queue[0].latest = {{"mHead", first}};
    queue[1].latest = {{"mHead", second}, {"mWristLeft", other}};
    queue[2].latest = {{"mHead", third}};
    const Pose expected{{"mHead", third}, {"mWristLeft", other}};
    assert(previewPose(queue) == expected);
    queue.pop_front();
    assert(previewPose(queue) == expected);
    queue.pop_front();
    assert(previewPose(queue) == Pose({{"mHead", third}}));
    queue.clear();
    assert(previewPose(queue).empty());
}

void unchangedPoseUpdates()
{
    const Pose initial{{"mHead", {}}};
    OwnerSession session;
    session.start("owner", "idle-drag", initial);
    session.grant("editor", "token", 0);
    assert(session.acquire("editor", "token", "idle", {"mHead"}, 0).granted);
    for (std::uint32_t sequence = 1; sequence <= 10; ++sequence)
    {
        assert(session.apply("editor", "token", "idle", initial, false, 0, true, sequence));
        assert(session.pose() == initial && session.revision() == 0);
    }

    Pose moved = initial;
    moved.at("mHead").transform[4] = .25f;
    // No-op updates still consume their sequence numbers and retain the lease.
    assert(!session.apply("editor", "token", "idle", moved, false, 0, true, 10));
    assert(!session.acquire("owner", "", "blocked", {"mHead"}, 0).granted);
    assert(session.apply("editor", "token", "idle", initial, true, 1, true, 11));
    assert(session.revision() == 0 && session.historyJoints("editor", false).empty());
    assert(!session.renew("editor", "token", "idle", 1));
    assert(session.acquire("owner", "", "released", {"mHead"}, 1).granted);
    session.release("owner", "released");

    assert(session.acquire("editor", "token", "drag", {"mHead"}, 1).granted);
    assert(session.apply("editor", "token", "drag", moved, false, 1, true, 1));
    assert(session.revision() == 1);
    for (std::uint32_t sequence = 2; sequence <= 11; ++sequence)
    {
        assert(session.apply("editor", "token", "drag", moved, false, 1, true, sequence));
        assert(session.pose() == moved && session.revision() == 1);
    }
    // An unchanged final packet must commit the drag's history and release it.
    assert(session.apply("editor", "token", "drag", moved, true, 2, true, 12));
    assert(session.revision() == 1);
    assert(session.historyJoints("editor", false) == JointSet{"mHead"});
    assert(session.acquire("editor", "token", "undo", {"mHead"}, 2).granted);
    assert(session.restore("editor", "token", "undo", false, 2));
    assert(session.pose() == initial && session.revision() == 2);
    assert(session.acquire("editor", "token", "redo", {"mHead"}, 2).granted);
    assert(session.restore("editor", "token", "redo", true, 2));
    assert(session.pose() == moved && session.revision() == 3);
}

int main()
{
    previewBeforeReservation();
    rapidRegrabGroupsWholeDrags();
    reservationScopeAndLiveUpdates();
    newerPreviewsSurviveOlderConfirmations();
    unchangedPoseUpdates();
    Pose pose{{"mWristLeft", {}}, {"mWristRight", {}}, {"mHead", {}}};
    pose["mHead"].worldLocked = true;
    assert(decodePose(encodePose(pose)) == std::optional<Pose>(pose));
    assert(!decodePose(encodePose(pose) + "\xff"));
    auto bad = pose;
    bad.begin()->second.transform[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!validPose(bad));

    OwnerSession session;
    session.start("owner", "session", pose);
    assert(!session.acquire("editor-a", "token-a", "a", {"mWristLeft"}, 0).granted);
    session.grant("editor-a", "token-a", 0);
    session.grant("editor-b", "token-b", 0);
    assert(!session.acquire("editor-a", "forged", "a", {"mWristLeft"}, 0).granted);
    assert(session.acquire("editor-a", "token-a", "a", {"mWristLeft"}, 0).granted);
    assert(session.acquire("editor-b", "token-b", "b", {"mWristRight"}, 0).granted);
    const auto conflict = session.acquire("editor-b", "token-b", "c", {"mWristLeft", "mHead"}, 0);
    assert(!conflict.granted && conflict.occupiedBy == "editor-a");
    // A failed multi-joint reservation must not reserve its otherwise free joints.
    assert(session.acquire("owner", "", "head", {"mHead"}, 0).granted);
    Joint changed = pose.at("mWristLeft");
    changed.transform[4] = .25f;
    assert(!session.apply("editor-b", "token-b", "a", {{"mWristLeft", changed}}, true, 0));
    assert(!session.apply("editor-a", "token-a", "a", {{"mHead", changed}}, true, 0));
    assert(session.apply("editor-a", "token-a", "a", {{"mWristLeft", changed}}, true, 0));
    assert(!session.apply("editor-a", "token-a", "a", {{"mWristLeft", changed}}, false, 0));
    assert(session.acquire("editor-b", "token-b", "c", {"mWristLeft"}, 0).granted);
    session.release("editor-b", "c");

    // Undo/redo uses the editor's accepted operations and refuses another editor's changes.
    assert(session.acquire("editor-a", "token-a", "undo", session.historyJoints("editor-a", false), 1).granted);
    assert(session.restore("editor-a", "token-a", "undo", false, 1));
    assert(session.pose().at("mWristLeft") == pose.at("mWristLeft"));
    assert(session.acquire("editor-a", "token-a", "redo", session.historyJoints("editor-a", true), 1).granted);
    assert(session.restore("editor-a", "token-a", "redo", true, 1));
    assert(session.acquire("editor-b", "token-b", "overwrite", {"mWristLeft"}, 1).granted);
    changed.transform[4] = .5f;
    assert(session.apply("editor-b", "token-b", "overwrite", {{"mWristLeft", changed}}, true, 1));
    assert(session.acquire("editor-a", "token-a", "stale-undo", {"mWristLeft"}, 1).granted);
    assert(!session.restore("editor-a", "token-a", "stale-undo", false, 1));
    session.release("editor-a", "stale-undo");

    assert(!session.renew("editor-b", "token-b", "b", 6));
    assert(session.acquire("editor-a", "token-a", "expired", {"mWristRight"}, 6).granted);
    session.revoke("editor-a");
    assert(!session.apply("editor-a", "token-a", "expired", {{"mWristRight", changed}}, true, 6));
    assert(session.acquire("owner", "", "after-revoke", {"mWristRight"}, 6).granted);
    assert(!session.heartbeat("editor-b", "token-b", 11));
    session.clear();
    assert(!session.authorized("owner", "", 0));

    // Multiple undo steps, including a drag with intermediate updates.
    session.start("owner", "new-session", pose);
    for (int i = 1; i <= 3; ++i)
    {
        assert(session.acquire("owner", "", "gesture", {"mHead"}, i).granted);
        changed = pose.at("mHead");
        changed.transform[4] = static_cast<float>(i);
        assert(session.apply("owner", "", "gesture", {{"mHead", changed}}, false, i));
        assert(session.apply("owner", "", "gesture", {{"mHead", changed}}, true, i));
    }
    for (int i = 2; i >= 0; --i)
    {
        assert(session.acquire("owner", "", "undo", session.historyJoints("owner", false), 4).granted);
        assert(session.restore("owner", "", "undo", false, 4));
        assert(session.pose().at("mHead").transform[4] == static_cast<float>(i));
    }

    const std::string id = "11111111-2222-3333-4444-555555555555";
    const std::string payload(1500, 'x');
    const auto packets = fragment(id, payload);
    assert(packets.size() == 3);
    Assembler assembler;
    assert(!assembler.receive("sender-a", packets[2], 0));
    assert(!assembler.receive("sender-a", packets[2], 0));
    assert(!assembler.receive("sender-b", packets[0], 0));
    assert(!assembler.receive("sender-a", packets[0], 0));
    assert(assembler.receive("sender-a", packets[1], 0) == payload);
    for (const auto& packet : packets) assert(!assembler.receive("sender-a", packet, 1));
    assert(!assembler.receive("sender", "FSP1:" + id + ":0:999:x", 0));
    assert(!assembler.receive("sender", "FSP1:" + id + ":0:1:", 0));
    assert(!assembler.receive("sender", std::string(801, 'x'), 0));
    assert(fragment(id, std::string(FRAGMENT_SIZE * MAX_FRAGMENTS + 1, 'x')).empty());
    assembler.clear();
    assert(!assembler.receive("sender", packets[0], 0));
    assert(!assembler.receive("sender", packets[1], 16));
    assert(!assembler.receive("sender", packets[2], 16));
    assert(assembler.receive("sender", packets[0], 16) == payload);

    // Owner, two editors and an observer converge through complete, reordered frames.
    Pose skeleton;
    for (int i = 0; i < 160; ++i) skeleton.emplace("joint" + std::to_string(i), Joint{});
    session.start("owner", "four-client-session", skeleton);
    session.grant("left-editor", "left-token", 0);
    session.grant("right-editor", "right-token", 0);
    assert(session.acquire("left-editor", "left-token", "left-lease", {"joint1", "joint2"}, 0).granted);
    assert(session.acquire("right-editor", "right-token", "right-lease", {"joint3"}, 0).granted);
    Joint left, right;
    left.transform[4] = .125f; right.transform[5] = .25f;
    assert(session.apply("left-editor", "left-token", "left-lease", {{"joint1", left}, {"joint2", left}}, false, 0, true, 2));
    assert(!session.apply("left-editor", "left-token", "left-lease", {{"joint1", Joint{}}}, false, 0, true, 1));
    assert(session.apply("right-editor", "right-token", "right-lease", {{"joint3", right}}, true, 1, true, 1));
    // Renewals protect a long final transfer; duplicates never rewind the pose.
    assert(session.renew("left-editor", "left-token", "left-lease", 4));
    assert(session.heartbeat("left-editor", "left-token", 4));
    assert(session.apply("left-editor", "left-token", "left-lease", {{"joint1", left}, {"joint2", left}}, true, 6, true, 3));
    const auto serialized = encodePose(session.pose());
    auto network = fragment(id, serialized);
    assert(network.size() > 10);
    std::reverse(network.begin(), network.end());
    for (const auto& peer : {"left-editor", "right-editor", "observer"})
    {
        Assembler receiver;
        Pose display = skeleton;
        for (std::size_t i = 0; i < network.size(); ++i)
        {
            auto completeFrame = receiver.receive(peer, network[i], 7);
            if (i + 1 < network.size()) { assert(!completeFrame); assert(display == skeleton); }
            else { assert(completeFrame); auto received = decodePose(*completeFrame); assert(received); display = *received; }
        }
        assert(display == session.pose());
    }
    session.revoke("left-editor");
    assert(!session.acquire("left-editor", "left-token", "after-revoke", {"joint1"}, 7).granted);
    session.clear();
    assert(!session.active());
}
