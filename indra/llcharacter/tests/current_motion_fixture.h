#ifndef FS_CURRENT_MOTION_FIXTURE_H
#define FS_CURRENT_MOTION_FIXTURE_H
#include "llkeyframemotion.h"
#include "llframetimer.h"
#include "keyframe_test_character.h"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace FSCurrentMotion
{
class Clock : public LLFrameTimer
{
public:
    static void set(F64 time, bool count = true) { sFrameTime = time; if (count) ++sFrameCount; }
    static void count() { ++sFrameCount; }
};
class Character final : public FSKeyframeTests::Character
{
public:
    LLJoint root, spare;
    std::vector<std::unique_ptr<LLJoint>> joints;
    bool allowOwned = true; // Explicit permission for this controlled non-viewer fixture.
    explicit Character(unsigned count = 3)
    {
        root.setName("mRoot"); spare.setName("spare"); root.addChild(&spare);
        for (unsigned i = 0; i < count; ++i)
        {
            auto joint = std::make_unique<LLJoint>(i);
            joint->setName("joint" + std::to_string(i)); root.addChild(joint.get());
            joints.push_back(std::move(joint));
        }
    }
    ~Character() override { getMotionController().deactivateAllMotions(); }
    LLJoint* getRootJoint() override { return &root; }
    LLJoint* getCharacterJoint(U32 i) override { return i < joints.size() ? joints[i].get() : nullptr; }
    F32 getPixelArea() const override { return 10000.f; }
    bool allowsOwnedKeyframeSampling() const override { return allowOwned; }
};
inline std::shared_ptr<LLKeyframeMotion::JointMotionList> asset(unsigned count = 3)
{
    auto data = std::make_shared<LLKeyframeMotion::JointMotionList>();
    data->mDuration = 2.f; data->mLoop = true; data->mLoopInPoint = .25f; data->mLoopOutPoint = 1.5f;
    data->mBasePriority = data->mMaxPriority = LLJoint::MEDIUM_PRIORITY;
    for (unsigned i = 0; i < count; ++i)
    {
        auto* joint = new LLKeyframeMotion::JointMotion;
        joint->mJointName = "joint" + std::to_string(i); joint->mUsage = 7; joint->mPriority = LLJoint::MEDIUM_PRIORITY;
        for (unsigned k = 0; k < 4; ++k)
        {
            const F32 time = k * .5f, value = (i + k + 1) * .03125f;
            joint->mScaleCurve.mKeys[time] = LLKeyframeMotion::ScaleKey(time, LLVector3(1 + value, 2 + value, 3 + value));
            joint->mRotationCurve.mKeys[time] = LLKeyframeMotion::RotationKey(time, LLQuaternion(value, LLVector3::x_axis));
            joint->mPositionCurve.mKeys[time] = LLKeyframeMotion::PositionKey(time, LLVector3(value, -value, value * 2));
        }
        joint->mScaleCurve.mNumKeys = joint->mRotationCurve.mNumKeys = joint->mPositionCurve.mNumKeys = 4;
        data->mJointMotionArray.push_back(joint);
    }
    return data;
}
inline std::unique_ptr<LLKeyframeMotion> motion(Character& character, const LLUUID& id)
{
    auto result = std::unique_ptr<LLKeyframeMotion>(static_cast<LLKeyframeMotion*>(LLKeyframeMotion::create(id)));
    if (result->onInitialize(&character) != LLMotion::STATUS_SUCCESS) throw std::runtime_error("Fixture initialization failed");
    result->activate(0.f);
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; result->onUpdate(0.f, mask);
    return result;
}
inline std::vector<U32> pose(LLKeyframeMotion& motion)
{
    std::vector<U32> result;
    auto append = [&](const F32* data, unsigned count)
    { for (unsigned i = 0; i < count; ++i) { U32 raw; std::memcpy(&raw, &data[i], sizeof(raw)); result.push_back(raw); } };
    for (auto* state = motion.getPose()->getFirstJointState(); state; state = motion.getPose()->getNextJointState())
    {
        append(state->getScale().mV, 3); append(state->getRotation().mQ, 4); append(state->getPosition().mV, 3);
        const F32 weight = state->getWeight(); append(&weight, 1);
        result.push_back(state->getUsage()); result.push_back(state->getPriority());
    }
    return result;
}
struct Fixture
{
    const LLUUID id{"fe000000-0000-0000-0000-0000000014f0"};
    Character actual, expected;
    std::shared_ptr<LLKeyframeMotion::JointMotionList> data;
    std::unique_ptr<LLKeyframeMotion> candidate, reference;
    explicit Fixture(unsigned count = 3): actual(count), expected(count), data(asset(count))
    {
        Clock::set(100.0); LLKeyframeDataCache::addKeyframeData(id, data);
        candidate = motion(actual, id); reference = motion(expected, id);
    }
    ~Fixture() { LLKeyframeDataCache::clear(); }
};
}
#endif
