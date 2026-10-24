// Exercise actual character/keyframe publication without a viewer or live avatar.
#include "linden_common.h"
#include "llcharacter.h"
#include "llkeyframemotion.h"
#include "keyframe_test_character.h"

#include <cstdlib>
#include <iostream>

namespace HandPoseLifetimeTest
{
unsigned checks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
using Character = FSKeyframeTests::Character;
class Motion final : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    Motion(Character& character, const LLUUID& id, std::shared_ptr<Asset> data) : LLKeyframeMotion(id)
    { setCharacter(&character); adoptJointMotionList(std::move(data)); }
    void publish() { applyKeyframes(0.f); }
};
std::shared_ptr<Motion::Asset> asset(const LLUUID& id, LLHandMotion::eHandPose pose, LLJoint::JointPriority priority)
{
    auto data = std::make_shared<Motion::Asset>();
    data->mHandPose = pose; data->mMaxPriority = priority;
    LLKeyframeDataCache::addKeyframeData(id, data);
    return data;
}
void expect(Character& character, LLHandMotion::eHandPose pose, LLJoint::JointPriority priority)
{
    auto* value = static_cast<LLHandMotion::eHandPose*>(character.getAnimationData("Hand Pose"));
    auto* rank = static_cast<LLJoint::JointPriority*>(character.getAnimationData("Hand Pose Priority"));
    require(value && rank, "Missing hand-pose publication");
    require(*value == pose && *rank == priority, "Hand-pose value/priority changed");
    const Character& view = character;
    require(view.getHandPoseAnimationPriority() == rank, "Typed lookup bypassed the current map entry");
}
void consume(Character& character)
{
    character.removeAnimationData("Hand Pose");
    character.removeAnimationData("Hand Pose Priority");
    require(!character.getAnimationData("Hand Pose") && !character.getAnimationData("Hand Pose Priority"),
            "Hand-motion consumption could not clear entries");
    require(!character.getHandPoseAnimationPriority(), "Typed lookup retained a removed priority");
}
int run()
{
    LLKeyframeDataCache::clear();
    const LLUUID idA("00000000-0000-0000-0000-000000000001");
    const LLUUID idB("00000000-0000-0000-0000-000000000002");
    const LLUUID idC("00000000-0000-0000-0000-000000000003");
    const LLUUID idD("00000000-0000-0000-0000-000000000004");
    Character first, second;
    auto a = asset(idA, LLHandMotion::HAND_POSE_FIST, LLJoint::MEDIUM_PRIORITY);
    auto b = asset(idB, LLHandMotion::HAND_POSE_RELAXED, LLJoint::LOW_PRIORITY);
    auto c = asset(idC, LLHandMotion::HAND_POSE_POINT, LLJoint::MEDIUM_PRIORITY);
    Motion ma(first,idA,a), mb(first,idB,b), mc(first,idC,c), shared(second,idA,a);
    require(!first.getHandPoseAnimationPriority(), "Empty character has a cached hand priority");
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_FIST,LLJoint::MEDIUM_PRIORITY);
    const auto* poseAddress = first.getAnimationData("Hand Pose");
    const auto* rankAddress = first.getAnimationData("Hand Pose Priority");
    require(poseAddress != &a->mHandPose && rankAddress != &a->mMaxPriority,
            "Character hand-pose data still borrows cached asset storage");
    mb.publish();
    expect(first,LLHandMotion::HAND_POSE_FIST,LLJoint::MEDIUM_PRIORITY);
    mc.publish(); // Equal priority retains existing last-wins order.
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    require(poseAddress == first.getAnimationData("Hand Pose") && rankAddress == first.getAnimationData("Hand Pose Priority"),
            "Winning publication changed character-owned addresses");
    b->mMaxPriority = LLJoint::HIGH_PRIORITY;
    mb.publish();
    expect(first,LLHandMotion::HAND_POSE_RELAXED,LLJoint::HIGH_PRIORITY);
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_RELAXED,LLJoint::HIGH_PRIORITY);

    shared.publish();
    expect(second,LLHandMotion::HAND_POSE_FIST,LLJoint::MEDIUM_PRIORITY);
    require(first.getAnimationData("Hand Pose") != second.getAnimationData("Hand Pose"),
            "Different characters share hand-pose value storage");
    a->mHandPose = LLHandMotion::HAND_POSE_POINT;
    shared.publish();
    expect(second,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    expect(first,LLHandMotion::HAND_POSE_RELAXED,LLJoint::HIGH_PRIORITY);
    consume(first);
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);

    // Legacy producers can still supply the existing void-pointer data contract.
    LLHandMotion::eHandPose legacyPose = LLHandMotion::HAND_POSE_SPREAD;
    LLJoint::JointPriority legacyPriority = LLJoint::HIGHEST_PRIORITY;
    first.setAnimationData("Hand Pose",&legacyPose);
    first.setAnimationData("Hand Pose Priority",&legacyPriority);
    require(first.getHandPoseAnimationPriority() == &legacyPriority, "Typed lookup missed generic priority override");
    ma.publish();
    require(first.getAnimationData("Hand Pose") == &legacyPose, "Lower priority overwrote legacy producer");
    legacyPriority = LLJoint::LOW_PRIORITY;
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    require(first.getAnimationData("Hand Pose") != &legacyPose, "Winning request did not own its value");

    // Null and independently removed entries follow the same generic map contract.
    first.setAnimationData("Hand Pose Priority", nullptr);
    require(!first.getHandPoseAnimationPriority(), "Null priority entry did not behave as absent");
    mb.publish();
    expect(first,LLHandMotion::HAND_POSE_RELAXED,LLJoint::HIGH_PRIORITY);
    first.removeAnimationData("Hand Pose Priority");
    require(!first.getHandPoseAnimationPriority(), "Removed priority was retained behind the map");
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    for (unsigned i = 0; i < 8; ++i)
    {
        consume(first); ma.publish();
        expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
        require(first.getAnimationData("Hand Pose") == poseAddress && first.getHandPoseAnimationPriority() == rankAddress,
                "Removal/republication changed character-owned value addresses");
    }

    LLHandMotion hand(LLUUID::null);
    require(hand.onInitialize(&first) == LLMotion::STATUS_SUCCESS && hand.onActivate(), "Actual hand consumer initialization failed");
    for (unsigned i = 0; i < 8; ++i)
    {
        consume(first); mb.publish(); // Relaxed matches the consumer's initial pose; no mesh is needed.
        expect(first,LLHandMotion::HAND_POSE_RELAXED,LLJoint::HIGH_PRIORITY);
        require(hand.onUpdate(i * .1f, nullptr), "Actual hand consumer update failed");
        require(!first.getAnimationData("Hand Pose") && !first.getHandPoseAnimationPriority(),
                "Actual hand consumer did not remove both publication entries");
    }
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);

    // Already published values remain character-owned when the cache drops its owner.
    LLKeyframeDataCache::removeKeyframeData(idA);
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    expect(second,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    consume(first); consume(second);
    {
        auto d = asset(idD,LLHandMotion::HAND_POSE_FIST,LLJoint::HIGHEST_PRIORITY);
        Motion temporary(first,idD,d);
        temporary.publish();
    }
    LLKeyframeDataCache::clear();
    expect(first,LLHandMotion::HAND_POSE_FIST,LLJoint::HIGHEST_PRIORITY);
    require(first.getAnimationData("Hand Pose") == poseAddress, "Character storage did not survive motion/cache destruction");
    consume(first);
    std::cout << checks << " actual character/keyframe hand-pose lifetime checks passed\n";
    return 0;
}
}
int main() { return HandPoseLifetimeTest::run(); }
