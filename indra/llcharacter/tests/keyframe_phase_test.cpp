// Actual keyframe/controller behavior compared with the frozen pre-phase methods.
#include "linden_common.h"
#include "animation_serialization_fixture.h"
#include "llframetimer.h"
#include "sampling_test_context.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

namespace KeyframePhaseTest
{
unsigned checks = 0, updates = 0, accepted = 0, fallbacks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
void scalar(std::vector<U32>& bits, F32 value)
{
    U32 raw; std::memcpy(&raw, &value, sizeof(raw)); bits.push_back(raw);
}
void transform(std::vector<U32>& bits, const LLVector3& pos, const LLQuaternion& rot, const LLVector3& scale)
{
    for (auto v : pos.mV) scalar(bits, v);
    for (auto v : rot.mQ) scalar(bits, v);
    for (auto v : scale.mV) scalar(bits, v);
}
class Character final : public FSKeyframeTests::Character
{
public:
    LLJoint root;
    LLJoint joints[3];
    unsigned volumeCalls = 0;
    Character()
    {
        root.setName("mRoot");
        for (unsigned i = 0; i < 3; ++i)
        {
            joints[i].setName("joint" + std::to_string(i)); joints[i].mJointNum = i;
            root.addChild(&joints[i]); joints[i].setPosition(LLVector3(.25f * i, .5f, .75f));
        }
    }
    ~Character() override { getMotionController().deactivateAllMotions(); }
    LLJoint* getRootJoint() override { return &root; }
    LLJoint* getCharacterJoint(U32 i) override { return i < 3 ? &joints[i] : nullptr; }
    F32 getPixelArea() const override { return 10000.f; }
    LLJoint* findCollisionVolume(S32 i) override { return &joints[i % 3]; }
    LLVector3 getVolumePos(S32 i, LLVector3& offset) override
    { ++volumeCalls; return joints[i % 3].getWorldPosition() + offset; }
    LLVector3d getPosGlobalFromAgent(const LLVector3& p) override { return LLVector3d(p); }
    LLVector3 getPosAgentFromGlobal(const LLVector3d& p) override { return LLVector3(p); }
};

class Probe : public LLKeyframeMotion
{
public:
    using Request = SamplingRequest;
    using Result = SamplingResult;
    using Identity = SamplingIdentity;
    using Asset = JointMotionList;
    using Constraint = JointConstraintSharedData;
    using Times = KeyframeUpdateTime;
    using LLKeyframeMotion::adoptJointMotionList;
    using LLKeyframeMotion::prepareKeyframeSampling;
    using LLKeyframeMotion::resolveKeyframeUpdateTime;
    using LLKeyframeMotion::updateWithSample;
    using LLKeyframeMotion::sampleKeyframes;
    using LLMotion::isActive;
    Probe(const LLUUID& id = LLUUID::null): LLKeyframeMotion(id) {}
    void init(Character& character, std::shared_ptr<Asset> data, U32 usage = 7)
    {
        setCharacter(&character); adoptJointMotionList(std::move(data)); mAssetStatus = ASSET_LOADED;
        setStopped(false);
        mJointStates.clear();
        for (unsigned i = 0; i < mJointMotionList->getNumJointMotions(); ++i)
        {
            LLPointer<LLJointState> state = new LLJointState;
            state->setJoint(&character.joints[i % 3]); state->setUsage(7);
            state->setScale(LLVector3(2.5f, 3.5f, 4.5f));
            state->setRotation(LLQuaternion(.5f, LLVector3::x_axis));
            state->setPosition(LLVector3(101.f, -101.f, .125f));
            state->setWeight(.375f); state->setPriority(LLJoint::HIGH_PRIORITY);
            state->setUsage(usage); mJointStates.push_back(state);
        }
        mPelvisp = &character.joints[0];
        for (auto* shared : mJointMotionList->mConstraints)
        {
            auto* constraint = new JointConstraint(shared);
            initializeConstraint(constraint); mConstraints.push_back(constraint);
        }
    }
    void addPose() { for (const auto& state : mJointStates) if (state) addJointState(state); }
    void times(F32 last, F32 looped) { mLastUpdateTime = last; mLastLoopedTime = looped; }
    LLPointer<LLJointState>& state(unsigned i) { return mJointStates[i]; }
    void removeLastState() { mJointStates.pop_back(); }
    void loaded(bool value) { mAssetStatus = value ? ASSET_LOADED : ASSET_UNDEFINED; }
    void borrowed() { mJointMotionOwner.reset(); } // Caller retains the fixture asset.
    bool legacyUpdate(F32 time, U8* mask);
    void legacyApplyKeyframes(F32 time);
    std::vector<U32> snapshot()
    {
        std::vector<U32> bits{mLastSkeletonSerialNum, U32(mStopped), U32(mActive), U32(mAssetStatus)};
        scalar(bits, mLastUpdateTime); scalar(bits, mLastLoopedTime);
        scalar(bits, getPose()->getWeight()); scalar(bits, getFadeWeight());
        for (const auto& state : mJointStates)
        {
            bits.push_back(bool(state));
            if (!state) continue;
            transform(bits, state->getPosition(), state->getRotation(), state->getScale());
            scalar(bits, state->getWeight()); bits.insert(bits.end(), {state->getUsage(), U32(state->getPriority())});
        }
        const auto* pose = static_cast<LLHandMotion::eHandPose*>(mCharacter->getAnimationData("Hand Pose"));
        const auto* priority = static_cast<LLJoint::JointPriority*>(mCharacter->getAnimationData("Hand Pose Priority"));
        bits.push_back(pose ? U32(*pose) : ~0u); bits.push_back(priority ? U32(*priority) : ~0u);
        for (const auto* c : mConstraints)
        { bits.push_back(c->mActive); scalar(bits, c->mWeight); scalar(bits, c->mTotalLength); }
        for (unsigned i = 0; i < 3; ++i)
        {
            auto* joint = mCharacter->getCharacterJoint(i);
            transform(bits, joint->getPosition(), joint->getRotation(), joint->getScale());
        }
        return bits;
    }
};
#include "keyframe_legacy_update.inc"

std::shared_ptr<Probe::Asset> asset(unsigned which = 2, unsigned constraint = 0)
{
    auto data = AnimationSerializationTest::asset(which);
    if (constraint)
    {
        auto* c = new Probe::Constraint;
        c->mChainLength = 0; c->mJointStateIndices = new S32[1]{0};
        c->mSourceConstraintVolume = 0; c->mTargetConstraintVolume = 1;
        c->mConstraintTargetType = constraint == 1 ? CONSTRAINT_TARGET_TYPE_BODY : CONSTRAINT_TARGET_TYPE_GROUND;
        c->mEaseInStartTime = 0.f; c->mEaseInStopTime = .125f;
        c->mEaseOutStartTime = 1.5f; c->mEaseOutStopTime = 1.75f;
        data->mConstraints.push_back(c);
    }
    return data;
}
const Probe::Identity ID{11, 13, 17, 19, 23};
void compare(Probe& legacy, Probe& candidate, bool old_result, bool new_result)
{
    require(old_result == new_result, "Motion continuation changed");
    const auto before = legacy.snapshot(), after = candidate.snapshot();
    if (before != after)
        for (size_t i = 0; i < before.size() && i < after.size(); ++i)
            if (before[i] != after[i])
            { std::cerr << "Comparison " << updates << " word " << i << " old " << before[i] << " current " << after[i] << '\n'; break; }
    require(before == after, "Motion times/poses/metadata/constraint effects changed");
    ++updates;
}
void trajectories(bool owned_supported)
{
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
    for (unsigned config = 0; config < 12; ++config)
        for (U32 usage : {0u, 1u, 2u, 4u, 7u, 0x87u})
        {
            Character a, b, c;
            auto data = asset(config % 2 ? 2 : 1, config / 4);
            data->mLoop = config % 3 != 0;
            if (config % 4 == 0) data->mDuration = 0.f;
            if (config % 4 == 1) data->mLoopInPoint = data->mLoopOutPoint;
            if (config % 4 == 2) data->mLoopInPoint = 1.75f; // Preserve legacy reversed-range behavior.
            Probe old, direct, prepared;
            old.init(a, data, usage); direct.init(b, data, usage); prepared.init(c, data, usage);
            unsigned step = 0;
            for (F32 time : {-3.f, -0.f, 0.f, .125f, 1.5f, 1.50001f, 3.f, 2.75f, 100.f, .25f})
            {
                std::memset(mask, step == 8 ? 255 : 0, sizeof(mask));
                if (step == 5) { old.setStopped(true); direct.setStopped(true); prepared.setStopped(true); }
                if (step == 7) { data->mDuration = 2.5f; data->mHandPose = LLHandMotion::HAND_POSE_RELAXED; }
                ++step;
                a.setSkeletonSerialNum(step); b.setSkeletonSerialNum(step); c.setSkeletonSerialNum(step);
                const auto before = prepared.snapshot();
                const auto keys = AnimationSerializationTest::snapshot(*data);
                Probe::Request input;
                require(prepared.prepareKeyframeSampling(time, ID, input) == owned_supported,
                        "Preparation did not honor numeric-context support");
                require(before == prepared.snapshot() && keys == AnimationSerializationTest::snapshot(*data),
                        "Preparation changed live state or curves");
                Probe::Result result;
                if (owned_supported) require(Probe::sampleKeyframes(std::move(input), result), "Prepared sampling failed");
                bool applied = false;
                const bool reference = old.legacyUpdate(time, mask);
                compare(old, direct, reference, direct.onUpdate(time, mask));
                compare(old, prepared, reference, prepared.updateWithSample(time, mask, result, ID, applied));
                require(applied == owned_supported, "Application did not honor numeric-context support");
                if (applied) ++accepted; else ++fallbacks;
            }
        }
}
void staleAndMetadata()
{
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
    for (unsigned reason = 0; reason < 21; ++reason)
    {
        Character a, b;
        auto data = asset(); Probe old, candidate;
        old.init(a, data); candidate.init(b, data); old.times(.25f, .125f); candidate.times(.25f, .125f);
        old.setStopped(true); candidate.setStopped(true);
        Probe::Request input;
        require(candidate.prepareKeyframeSampling(1.f, ID, input), "Initial prepare failed");
        Probe::Result result;
        bool sampled = false;
        std::thread worker([&] { sampled = Probe::sampleKeyframes(std::move(input), result); });
        worker.join(); require(sampled, "Owned background sample failed");
        auto current = ID; F32 time = 1.f;
        if (reason == 0) ++current.instance;
        if (reason == 1) ++current.activation;
        if (reason == 2) ++current.frame;
        if (reason == 3) ++current.skeleton;
        if (reason == 4) ++current.edit;
        if (reason == 5) time = 1.125f;
        if (reason == 6) data->mDuration = 2.125f;
        if (reason == 7) { old.state(2)->setUsage(1); candidate.state(2)->setUsage(1); }
        if (reason == 8) { candidate.adoptJointMotionList(asset()); old.adoptJointMotionList(asset()); }
        if (reason == 9) result.takeRequest();
        if (reason == 10) { candidate.borrowed(); old.borrowed(); }
        if (reason == 11) { old.state(1) = nullptr; candidate.state(1) = nullptr; }
        if (reason == 12) { data->mJointMotionArray[0]->mPositionCurve.mKeys.begin()->second.mPosition.set(9, 8, 7); ++current.edit; }
        // Hand pose/priority are main-thread metadata and must be read at application.
        if (reason >= 13 && reason < 18)
        {
            data->mHandPose = LLHandMotion::HAND_POSE_RELAXED;
            data->mMaxPriority = reason == 13 ? LLJoint::MEDIUM_PRIORITY : LLJoint::HIGH_PRIORITY;
            if (reason != 17)
            {
                const auto priority = reason == 14 ? LLJoint::LOW_PRIORITY : LLJoint::HIGH_PRIORITY;
                a.setHandPoseAnimationData(LLHandMotion::HAND_POSE_FIST, priority);
                b.setHandPoseAnimationData(LLHandMotion::HAND_POSE_FIST, priority);
            }
            if (reason == 16) { old.state(0)->setWeight(.75f); candidate.state(0)->setWeight(.75f); }
        }
        if (reason == 18)
        {
            old.setStopped(false); candidate.setStopped(false);
            data->mLoopOutPoint = .5f;
        }
        if (reason == 19) { old.times(.5f, .25f); candidate.times(.5f, .25f); }
        if (reason == 20) { old.loaded(false); candidate.loaded(false); }
        bool applied = true;
        const bool reference = old.legacyUpdate(time, mask);
        compare(old, candidate, reference, candidate.updateWithSample(time, mask, result, current, applied));
        require(applied == (reason >= 13 && reason < 18), "Stale result accepted or current metadata rejected");
        if (applied) ++accepted; else ++fallbacks;
    }
}
void rejectedPreparation()
{
    Character character;
    Probe empty; Probe::Request input;
    require(!empty.prepareKeyframeSampling(1.f, ID, input) && !input.asset, "Uninitialized motion accepted");
    for (unsigned reason = 0; reason < 8; ++reason)
    {
        auto data = asset(); Probe candidate; candidate.init(character, data);
        require(candidate.prepareKeyframeSampling(1.f, ID, input), "Valid prepare failed before rejection");
        if (reason == 0) candidate.loaded(false);
        if (reason == 1) candidate.setCharacter(nullptr);
        if (reason == 2) candidate.borrowed();
        if (reason == 3) candidate.removeLastState();
        if (reason == 4) { delete data->mJointMotionArray[1]; data->mJointMotionArray[1] = nullptr; }
        if (reason == 5) data->mJointMotionArray.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS + 1, nullptr);
        if (reason == 6) data->mDuration = std::numeric_limits<F32>::infinity();
        const F32 time = reason == 7 ? std::numeric_limits<F32>::infinity() : 1.f;
        const bool prepared = candidate.prepareKeyframeSampling(time, ID, input);
        if (prepared || input.asset || !input.usage.empty()) std::cerr << "Rejected preparation case " << reason << " time " << input.time << '\n';
        require(!prepared && !input.asset && input.usage.empty(),
                "Malformed/borrowed preparation accepted or retained old payload");
    }
    auto data = asset(); Probe candidate; candidate.init(character, data); candidate.state(1) = nullptr;
    require(candidate.prepareKeyframeSampling(1.f, ID, input) && input.usage[1] == 0,
            "Missing joint state did not preserve the legacy no-op");

    Character a, b; Probe old, phase;
    auto zero = asset(0); old.init(a, zero); phase.init(b, zero);
    require(phase.prepareKeyframeSampling(3.f, ID, input) && input.usage.empty(), "Empty owned motion rejected");
    Probe::Result result; require(Probe::sampleKeyframes(std::move(input), result), "Empty sampling failed");
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; bool applied = false;
    const bool reference = old.legacyUpdate(3.f, mask);
    compare(old, phase, reference, phase.updateWithSample(3.f, mask, result, ID, applied));
    require(applied, "Empty current result rejected");
}

class Clock : public LLFrameTimer
{
public:
    static void set(F64 time) { sFrameTime = time; ++sFrameCount; }
};
class Controlled final : public Probe
{
public:
    bool legacy;
    unsigned calls = 0, successful = 0;
    Controlled(const LLUUID& id, bool old): Probe(id), legacy(old) {}
    LLMotionInitStatus onInitialize(LLCharacter* character) override
    {
        init(*static_cast<Character*>(character), asset(2, 1)); addPose(); return STATUS_SUCCESS;
    }
    bool canDeprecate() override { return false; } // Keep fixture handles stable for reactivation.
    bool onUpdate(F32 time, U8* mask) override
    {
        ++calls;
        if (legacy) return legacyUpdate(time, mask);
        Request request; Result result;
        const Identity current{1, 2, LLFrameTimer::getFrameCount(), mCharacter->getSkeletonSerialNum(), 3};
        require(prepareKeyframeSampling(time, current, request), "Controller preparation failed");
        require(sampleKeyframes(std::move(request), result), "Controller sample failed");
        bool applied = false;
        const bool continuation = updateWithSample(time, mask, result, current, applied);
        require(applied, "Controller current result rejected"); ++successful;
        return continuation;
    }
};
LLMotion* oldFactory(const LLUUID& id) { return new Controlled(id, true); }
LLMotion* phaseFactory(const LLUUID& id) { return new Controlled(id, false); }
void controllerLifecycle()
{
    const LLUUID oldID("fe000000-0000-0000-0000-0000000014c0"), newID("fe000000-0000-0000-0000-0000000014c1");
    Clock::set(100.0);
    Character a, b;
    // The old controller leaves signatures indeterminate until its first update.
    // Establish defined masks through the real path; track constructor initialization separately.
    a.getMotionController().updateMotions(); b.getMotionController().updateMotions();
    require(a.registerMotion(oldID, oldFactory) && b.registerMotion(newID, phaseFactory), "Test factory registration failed");
    require(a.startMotion(oldID) && b.startMotion(newID), "Controller activation failed");
    auto* old = static_cast<Controlled*>(a.findMotion(oldID));
    auto* candidate = static_cast<Controlled*>(b.findMotion(newID));
    compare(*old, *candidate, true, true);
    auto update = [&](F64 clock, unsigned mode)
    {
        Clock::set(clock);
        if (mode == 1) { a.updateMotions(LLCharacter::HIDDEN_UPDATE); b.updateMotions(LLCharacter::HIDDEN_UPDATE); }
        else { a.getMotionController().updateMotions(mode == 2); b.getMotionController().updateMotions(mode == 2); }
        compare(*old, *candidate, old->isActive(), candidate->isActive());
        require(old->calls == candidate->calls && a.getMotionController().getAnimTime() == b.getMotionController().getAnimTime(),
                "Controller call count or clock changed");
    };
    update(100.125, 0); update(101.75, 0);
    a.getMotionController().pauseAllMotions(); b.getMotionController().pauseAllMotions();
    const auto paused = candidate->calls;
    update(102.0, 0); require(paused == candidate->calls, "Paused controller sampled a motion");
    update(102.25, 2); require(paused < candidate->calls, "Forced pause update was not exercised");
    a.getMotionController().unpauseAllMotions(); b.getMotionController().unpauseAllMotions();
    const auto hidden = candidate->calls;
    update(102.5, 1); require(hidden == candidate->calls, "Hidden controller sampled a motion");
    update(102.75, 0);
    require(a.stopMotion(oldID) && b.stopMotion(newID), "Controller stop failed");
    update(103.0, 0); update(110.0, 1);
    require(!candidate->isActive(), "Hidden stopped motion did not deactivate");
    require(a.startMotion(oldID) && b.startMotion(newID), "Controller reactivation failed");
    compare(*old, *candidate, true, true); update(110.125, 0);
    require(a.stopMotion(oldID, true) && b.stopMotion(newID, true), "Immediate controller stop failed");
    update(111.0, 0);
    require(candidate->successful > 5 && a.volumeCalls == b.volumeCalls && a.volumeCalls > 5,
            "Controller/constraint paths were not exercised equally");
}
int run()
{
    const bool supported = FSSamplingTest::supportedContext("owned phase contract");
    trajectories(supported);
    if (!supported)
    {
        std::cout << checks << " direct/fallback checks and " << updates
                  << " frozen-update comparisons passed; owned/controller sampling checks SKIPPED\n";
        return 0;
    }
    staleAndMetadata(); rejectedPreparation(); controllerLifecycle();
    std::cout << checks << " phase checks, " << updates << " frozen-update comparisons, "
              << accepted << " accepted samples and " << fallbacks << " stale fallbacks passed\n";
    return 0;
}
}
int main() { return KeyframePhaseTest::run(); }
