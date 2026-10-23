#ifndef FS_ANIMATION_SERIALIZATION_FIXTURE_H
#define FS_ANIMATION_SERIALIZATION_FIXTURE_H
#include "llkeyframemotion.h"
#include "keyframe_test_character.h"
#include <cstring>
#include <vector>

namespace AnimationSerializationTest
{
class Character final : public FSKeyframeTests::Character
{
public:
    Character() { mVolume.setName("collisionvolume"); } // 15 characters plus NUL fill the wire field.
    LLJoint* findCollisionVolume(S32) override { return &mVolume; }
private:
    LLJoint mVolume;
};
class Probe final : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    using Joint = JointMotion;
    using SK = ScaleKey;
    using RK = RotationKey;
    using PK = PositionKey;
    using Constraint = JointConstraintSharedData;
    using Request = SamplingRequest;
    using Result = SamplingResult;
    using Row = SampledJoint;
    Probe(Character& character, std::shared_ptr<Asset> data): LLKeyframeMotion(LLUUID::null)
    { setCharacter(&character); adoptJointMotionList(std::move(data)); }
    static bool sample(Request request, Result& result) { return sampleKeyframes(std::move(request), result); }
};
inline constexpr U32 ALL = LLJointState::POS | LLJointState::ROT | LLJointState::SCALE;
inline std::shared_ptr<Probe::Asset> asset(unsigned which)
{
    auto data = std::make_shared<Probe::Asset>();
    data->mDuration = which == 4 ? 0.f : 2.f;
    data->mLoop = true; data->mLoopInPoint = 0.25f; data->mLoopOutPoint = 1.5f;
    data->mBasePriority = LLJoint::MEDIUM_PRIORITY; data->mMaxPriority = LLJoint::HIGH_PRIORITY;
    data->mHandPose = LLHandMotion::HAND_POSE_FIST;
    data->mEaseInDuration = 0.125f; data->mEaseOutDuration = 0.5f;
    data->mEmoteName = "fixture";
    const unsigned joints = which == 0 ? 0 : (which == 2 ? 3 : (which == 3 ? 2 : 1));
    const unsigned keys = which == 2 ? 4 : (which == 3 ? 3 : 0);
    for (unsigned j = 0; j < joints; ++j)
    {
        auto* joint = new Probe::Joint;
        joint->mJointName = "joint" + std::to_string(j); joint->mUsage = ALL;
        joint->mPriority = LLJoint::MEDIUM_PRIORITY;
        joint->mScaleCurve.mKeys[0.f] = Probe::SK(0.f, LLVector3(1.1234567f, 2.f, -0.f));
        joint->mScaleCurve.mNumKeys = 1;
        joint->mScaleCurve.mLoopInKey = Probe::SK(.25f, LLVector3(1.f, 1.f, 1.f));
        joint->mScaleCurve.mLoopOutKey = Probe::SK(1.5f, LLVector3(2.f, 2.f, 2.f));
        joint->mRotationCurve.mLoopInKey = Probe::RK(.25f, LLQuaternion());
        joint->mRotationCurve.mLoopOutKey = Probe::RK(1.5f, LLQuaternion(1, 0, 0, 0));
        joint->mPositionCurve.mLoopInKey = Probe::PK(.25f, LLVector3(1.f, 2.f, 3.f));
        joint->mPositionCurve.mLoopOutKey = Probe::PK(1.5f, LLVector3(3.f, 2.f, 1.f));
        const LLQuaternion rotations[] = {LLQuaternion(0, 0, 0, 1), LLQuaternion(1, 0, 0, 0),
                                          LLQuaternion(.5f, .5f, .5f, .5f), LLQuaternion(-.5f, -.5f, -.5f, -.5f)};
        const F32 z[] = {5.5f, -5.5f, 0.00001f, -0.00001f};
        for (unsigned k = 0; k < keys; ++k)
        {
            const F32 time = static_cast<F32>(k) / 2.f;
            joint->mRotationCurve.mKeys[time] = Probe::RK(time, rotations[(j + k) % 4]);
            joint->mPositionCurve.mKeys[time] = Probe::PK(time,
                LLVector3((j + 1) * .1234567f + k * .010203f, k ? -static_cast<F32>(j + k) * .314159f : -0.f, z[k]));
        }
        joint->mRotationCurve.mNumKeys = joint->mPositionCurve.mNumKeys = keys;
        data->mJointMotionArray.push_back(joint);
    }
    if (which == 3)
    {
        auto* constraint = new Probe::Constraint;
        constraint->mChainLength = 1; constraint->mJointStateIndices = new S32[1]{0};
        constraint->mSourceConstraintVolume = constraint->mTargetConstraintVolume = 0;
        constraint->mSourceConstraintOffset = LLVector3(.125f, .25f, .5f);
        constraint->mTargetConstraintOffset = LLVector3(1.f, -1.f, 0.f);
        constraint->mTargetConstraintDir = LLVector3(0.f, 0.f, 1.f);
        constraint->mEaseInStartTime = .125f; constraint->mEaseInStopTime = .25f;
        constraint->mEaseOutStartTime = 1.5f; constraint->mEaseOutStopTime = 1.75f;
        data->mConstraints.push_back(constraint);
    }
    return data;
}
inline Probe::Request request(const std::shared_ptr<Probe::Asset>& data, F32 time)
{
    Probe::Request input;
    input.asset = data; input.time = time; input.duration = data->mDuration;
    input.identity = {1, 2, 3, 4, 5}; input.usage.assign(data->getNumJointMotions(), ALL);
    return input;
}
inline std::vector<U32> snapshot(const Probe::Asset& data)
{
    std::vector<U32> bits;
    auto scalar = [&](F32 value) { U32 raw; std::memcpy(&raw, &value, sizeof(raw)); bits.push_back(raw); };
    auto vector = [&](const LLVector3& value) { for (F32 lane : value.mV) scalar(lane); };
    auto quaternion = [&](const LLQuaternion& value) { for (F32 lane : value.mQ) scalar(lane); };
    scalar(data.mDuration); scalar(data.mLoopInPoint); scalar(data.mLoopOutPoint);
    scalar(data.mEaseInDuration); scalar(data.mEaseOutDuration);
    bits.insert(bits.end(), {U32(data.mLoop), U32(data.mBasePriority), U32(data.mMaxPriority), U32(data.mHandPose)});
    for (const auto* joint : data.mJointMotionArray)
    {
        bits.insert(bits.end(), {joint->mUsage, U32(joint->mPriority)});
        const auto& s = joint->mScaleCurve; const auto& r = joint->mRotationCurve; const auto& p = joint->mPositionCurve;
        bits.insert(bits.end(), {U32(s.mNumKeys), U32(r.mNumKeys), U32(p.mNumKeys),
                                U32(s.mInterpolationType), U32(r.mInterpolationType), U32(p.mInterpolationType)});
        for (const auto& key : s.mKeys) { scalar(key.first); scalar(key.second.mTime); vector(key.second.mScale); }
        for (const auto& key : r.mKeys) { scalar(key.first); scalar(key.second.mTime); quaternion(key.second.mRotation); }
        for (const auto& key : p.mKeys) { scalar(key.first); scalar(key.second.mTime); vector(key.second.mPosition); }
        scalar(s.mLoopInKey.mTime); vector(s.mLoopInKey.mScale); scalar(s.mLoopOutKey.mTime); vector(s.mLoopOutKey.mScale);
        scalar(r.mLoopInKey.mTime); quaternion(r.mLoopInKey.mRotation); scalar(r.mLoopOutKey.mTime); quaternion(r.mLoopOutKey.mRotation);
        scalar(p.mLoopInKey.mTime); vector(p.mLoopInKey.mPosition); scalar(p.mLoopOutKey.mTime); vector(p.mLoopOutKey.mPosition);
    }
    return bits;
}
}
#endif
