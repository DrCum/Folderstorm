#include "../fssharedposemodel.h"
#include <cassert>
#include <limits>

using namespace FSSharedPose;

int main()
{
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
