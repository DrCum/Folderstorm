// Exercise actual character/keyframe publication without a viewer or live avatar.
#include "linden_common.h"
#include "llcharacter.h"
#include "llkeyframemotion.h"

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
class Character final : public LLCharacter
{
public:
    const char* getAnimationPrefix() override { return "test"; }
    LLJoint* getRootJoint() override { return nullptr; }
    LLVector3 getCharacterPosition() override { return LLVector3(); }
    LLQuaternion getCharacterRotation() override { return LLQuaternion(); }
    LLVector3 getCharacterVelocity() override { return LLVector3(); }
    LLVector3 getCharacterAngularVelocity() override { return LLVector3(); }
    void getGround(const LLVector3&, LLVector3& position, LLVector3& normal) override
    { position = LLVector3(); normal = LLVector3(0,0,1); }
    LLJoint* getCharacterJoint(U32) override { return nullptr; }
    F32 getTimeDilation() override { return 1.f; }
    F32 getPixelArea() const override { return 1.f; }
    LLPolyMesh* getHeadMesh() override { return nullptr; }
    LLPolyMesh* getUpperBodyMesh() override { return nullptr; }
    LLVector3d getPosGlobalFromAgent(const LLVector3&) override { return LLVector3d(); }
    LLVector3 getPosAgentFromGlobal(const LLVector3d&) override { return LLVector3(); }
    void addDebugText(const std::string&) override {}
    const LLUUID& getID() const override { return mID; }
private:
    LLUUID mID{"51abb30a-f75f-4bca-9bb0-167ce8c60974"};
};
class Motion final : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    Motion(Character& character, const LLUUID& id, Asset* asset) : LLKeyframeMotion(id)
    { setCharacter(&character); mJointMotionList = asset; }
    void publish() { applyKeyframes(0.f); }
};
Motion::Asset* asset(const LLUUID& id, LLHandMotion::eHandPose pose, LLJoint::JointPriority priority)
{
    auto* data = new Motion::Asset;
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
}
void consume(Character& character)
{
    character.removeAnimationData("Hand Pose");
    character.removeAnimationData("Hand Pose Priority");
    require(!character.getAnimationData("Hand Pose") && !character.getAnimationData("Hand Pose Priority"),
            "Hand-motion consumption could not clear entries");
}
int run()
{
    LLKeyframeDataCache::clear();
    const LLUUID idA("00000000-0000-0000-0000-000000000001");
    const LLUUID idB("00000000-0000-0000-0000-000000000002");
    const LLUUID idC("00000000-0000-0000-0000-000000000003");
    const LLUUID idD("00000000-0000-0000-0000-000000000004");
    Character first, second;
    auto* a = asset(idA, LLHandMotion::HAND_POSE_FIST, LLJoint::MEDIUM_PRIORITY);
    auto* b = asset(idB, LLHandMotion::HAND_POSE_RELAXED, LLJoint::LOW_PRIORITY);
    auto* c = asset(idC, LLHandMotion::HAND_POSE_POINT, LLJoint::MEDIUM_PRIORITY);
    Motion ma(first,idA,a), mb(first,idB,b), mc(first,idC,c), shared(second,idA,a);
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
    ma.publish();
    require(first.getAnimationData("Hand Pose") == &legacyPose, "Lower priority overwrote legacy producer");
    legacyPriority = LLJoint::LOW_PRIORITY;
    ma.publish();
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    require(first.getAnimationData("Hand Pose") != &legacyPose, "Winning request did not own its value");

    // Do not update through an evicted raw asset; test the already published values.
    LLKeyframeDataCache::removeKeyframeData(idA);
    expect(first,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    expect(second,LLHandMotion::HAND_POSE_POINT,LLJoint::MEDIUM_PRIORITY);
    consume(first); consume(second);
    {
        auto* d = asset(idD,LLHandMotion::HAND_POSE_FIST,LLJoint::HIGHEST_PRIORITY);
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
