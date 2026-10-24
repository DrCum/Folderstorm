#include "linden_common.h"
#include "current_motion_fixture.h"
#include "fssamplingshadow.h"
#include "sampling_test_context.h"
#include "sampling_allocation_probe.h"
#include "llsimdmath.h"
#include <cstdlib>
#include <iostream>
#include <numeric>
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#endif

namespace SamplingShadowTest
{
using namespace FSCurrentMotion;
using FSSampling::ShadowScope;
unsigned checks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
U32 bits(F32 value) { U32 raw; std::memcpy(&raw, &value, sizeof(raw)); return raw; }
class ProjectionController : public LLMotionController
{
public:
    void seed(F32 delta, F32 factor, F32 update, bool paused, F32 step, S32 count)
    {
        mTimer.setAge(2.0); mPrevTimerElapsed = 2.f - delta; mAnimTime = 1.25f; mLastTime = .75f;
        mTimeFactor = factor; mUpdateFactor = update; mPaused = paused; mTimeStep = step; mTimeStepCount = count;
    }
    std::vector<U32> state() const
    {
        return {bits(mPrevTimerElapsed), bits(mAnimTime), bits(mLastTime), bits(mTimeFactor), bits(mUpdateFactor),
                bits(mTimeStep), U32(mTimeStepCount), U32(mPaused), U32(mHasRunOnce), bits(mLastInterp)};
    }
};
void projection()
{
    for (F32 factor : {-.5f, 0.f, .5f, 1.f, 2.75f})
        for (F32 update : {.5f, 1.f, 3.125f})
            for (F32 delta : {0.f, .016666667f, .25f, 2.0001f})
                for (bool paused : {false, true}) for (bool force : {false, true})
                    for (F32 step : {0.f, .125f}) for (S32 count : {0, 1, 11})
                    {
                        Clock::set(100.0); Character character; ProjectionController controller;
                        controller.setCharacter(&character); controller.seed(delta, factor, update, paused, step, count);
                        const auto before = controller.state(); LLMotionController::SamplingClock projected;
                        require(controller.predictSamplingClock(force, projected), "Finite projection rejected");
                        require(before == controller.state(), "Clock projection mutated the controller");
                        controller.updateMotions(force);
                        require(bits(projected.animTime) == bits(controller.getAnimTime()), "Projection differs from actual controller arithmetic");
                    }
    Clock::set(100.0); Character character; ProjectionController controller; controller.setCharacter(&character);
    LLMotionController::SamplingClock projected;
    controller.seed(.25f, 1.f, 1.f, true, 0.f, 0);
    require(controller.predictSamplingClock(false, projected) && !projected.callbacks, "Paused nonforced projection promised callbacks");
    require(controller.predictSamplingClock(true, projected) && projected.callbacks && projected.animTime == 1.25f,
            "Forced paused projection changed animation time");
    require(controller.predictSamplingClock(false, projected, true) && projected.callbacks && projected.animTime == 1.5f,
            "Pause-handle release projection did not advance normally");
    controller.seed(0.f, 1.f, 1.f, false, .125f, 11);
    require(controller.predictSamplingClock(false, projected) && !projected.callbacks, "Same quantum promised normal callbacks");
    controller.seed(.25f, std::numeric_limits<F32>::infinity(), 1.f, false, 0.f, 0);
    require(!controller.predictSamplingClock(false, projected), "Nonfinite projection accepted");
    controller.seed(.25f, 1.f, 1.f, false, -1.f, 0);
    require(!controller.predictSamplingClock(false, projected), "Unsupported negative quantum accepted");
}

struct Seed { Seed() { Clock::set(100.0); } };
struct Pair
{
    Seed seed;
    Character actual, expected;
    LLMotionController& a = actual.getMotionController();
    LLMotionController& b = expected.getMotionController();
    const LLUUID id{"fe000000-0000-0000-0000-0000000014a0"};
    std::shared_ptr<LLKeyframeMotion::JointMotionList> data = asset();
    Pair()
    {
        LLKeyframeDataCache::addKeyframeData(id, data); a.updateMotions(); b.updateMotions();
        require(a.startMotion(id, 0.f) && b.startMotion(id, 0.f), "Pair activation failed");
        Clock::set(100.125);
    }
    ~Pair() { a.deactivateAllMotions(); b.deactivateAllMotions(); LLKeyframeDataCache::clear(); }
    LLKeyframeMotion& motion(LLMotionController& c) { return *static_cast<LLKeyframeMotion*>(c.findMotion(id)); }
    void compare()
    {
        require(bits(a.getAnimTime()) == bits(b.getAnimTime()), "Shadow changed controller animation time");
        require(a.getActiveMotions().size() == b.getActiveMotions().size(), "Shadow changed active-list membership");
        auto left = a.getActiveMotions().begin(), right = b.getActiveMotions().begin();
        for (; left != a.getActiveMotions().end(); ++left, ++right)
        {
            require((*left)->getID() == (*right)->getID() && (*left)->isStopped() == (*right)->isStopped() &&
                    bits((*left)->getStopTime()) == bits((*right)->getStopTime()), "Shadow changed motion order/stop state");
            auto* x = dynamic_cast<LLKeyframeMotion*>(*left); auto* y = dynamic_cast<LLKeyframeMotion*>(*right);
            if (x && y) require(pose(*x) == pose(*y), "Shadow changed actual joint poses");
        }
        require(actual.getHandPoseAnimationPriority() && expected.getHandPoseAnimationPriority() &&
                *actual.getHandPoseAnimationPriority() == *expected.getHandPoseAnimationPriority(), "Shadow changed hand priority");
    }
};
void conservation()
{
    const auto stats = ShadowScope::totals();
    require(stats.predicted == stats.matched + stats.mismatched + stats.unused, "Prediction accounting lost an entry");
    require(stats.bufferBytesPeak <= ShadowScope::MAX_BUFFER_BYTES && stats.retainedKeysPeak <= ShadowScope::MAX_RETAINED_KEYS,
            "Shadow admission exceeded global limits");
    require(std::accumulate(stats.matchedGapNs.begin(), stats.matchedGapNs.end(), U64(0)) == stats.matched,
            "Matched gap histogram count differs");
    require(std::accumulate(stats.firstMatchedGapNs.begin(), stats.firstMatchedGapNs.end(), U64(0)) == stats.firstMatched &&
            stats.firstMatched <= stats.matched && stats.validationNs <= stats.observationNs,
            "First-callback histogram or observation cost accounting differs");
    require(!FSSampling::activeShadow, "Shadow scope survived its owner boundary");
}
void mutations()
{
    for (unsigned reason = 0; reason < 21; ++reason)
    {
        Pair p; require(ShadowScope::resetTotals(), "Previous scope remained live");
        LLAnimPauseRequest held_a, held_b;
#if defined(_MSC_VER) && defined(_M_X64)
        const unsigned original_controls = _mm_getcsr();
#endif
        if (reason == 15 || reason == 16) { held_a = p.actual.requestPause(); held_b = p.expected.requestPause(); }
        if (reason == 16) { held_a = nullptr; held_b = nullptr; }
        if (reason == 18)
        {
            p.motion(p.a).setEaseOut(.5f);
            p.expected.updateMotions(LLCharacter::NORMAL_UPDATE); p.actual.updateMotions(LLCharacter::NORMAL_UPDATE);
            Clock::set(100.25);
        }
        {
            ShadowScope shadow(p.a, true, false, !p.actual.hasMotionPauseRequests());
            require(!ShadowScope::resetTotals(), "Live scope reset aggregate counters");
            if (reason == 1) Clock::count();
            if (reason == 2) Clock::set(100.25, false);
            if (reason == 3) { p.actual.setSkeletonSerialNum(1); p.expected.setSkeletonSerialNum(1); }
            if (reason == 4) { p.actual.setAppearanceSerialNum(1); p.expected.setAppearanceSerialNum(1); }
            if (reason == 5)
            {
                p.motion(p.a).getPose()->getFirstJointState()->setUsage(LLJointState::POS);
                p.motion(p.b).getPose()->getFirstJointState()->setUsage(LLJointState::POS);
            }
            if (reason == 6) p.actual.allowOwned = false;
            if (reason == 7) p.motion(p.a).setLoopIn(.125f);
            if (reason == 8) { p.a.stopMotionLocally(p.id, false); p.b.stopMotionLocally(p.id, false); }
            if (reason == 10) { p.a.setTimeFactor(2.f); p.b.setTimeFactor(2.f); }
            if (reason == 11) LLKeyframeDataCache::clear();
            if (reason == 12) { p.a.startMotion(p.id, 0.f); p.b.startMotion(p.id, 0.f); }
            if (reason == 13)
            {
                p.motion(p.a).getPose()->getFirstJointState()->setJoint(&p.actual.spare);
                p.motion(p.b).getPose()->getFirstJointState()->setJoint(&p.expected.spare);
            }
            if (reason == 14) { held_a = p.actual.requestPause(); held_b = p.expected.requestPause(); }
            if (reason == 15) { held_a = nullptr; held_b = nullptr; }
            if (reason == 17)
            {
                p.a.removeMotion(p.id); p.b.removeMotion(p.id);
                p.a.startMotion(p.id, 0.f); p.b.startMotion(p.id, 0.f);
            }
            if (reason == 18)
            {
                p.a.stopMotionLocally(p.id, false); p.b.stopMotionLocally(p.id, false);
                p.a.startMotion(p.id, 0.f); p.b.startMotion(p.id, 0.f);
            }
#if defined(_MSC_VER) && defined(_M_X64)
            if (reason == 19) _mm_setcsr(original_controls ^ _MM_FLUSH_ZERO_MASK);
            if (reason == 20) _mm_setcsr((original_controls & ~_MM_ROUND_MASK) | _MM_ROUND_UP);
#endif
            const auto kind = reason == 9 ? LLCharacter::HIDDEN_UPDATE : LLCharacter::NORMAL_UPDATE;
            p.expected.updateMotions(kind); p.actual.updateMotions(kind);
#if defined(_MSC_VER) && defined(_M_X64)
            _mm_setcsr(original_controls);
#endif
        }
        p.compare(); conservation();
        require(!p.motion(p.a).ownedSamplingBindingBytes(), "Diagnostic retained owner bindings between batches");
        for (auto* motion : p.a.getActiveMotions())
            require(!static_cast<LLKeyframeMotion*>(motion)->ownedSamplingBindingBytes(), "Deprecated active motion retained bindings");
        if (reason == 18) require(p.a.getActiveMotions().size() == 2, "Fixture did not exercise deprecated duplicate-ID ownership");
        const auto stats = ShadowScope::totals();
        if (reason == 0 || reason == 11 || reason == 16) require(stats.matched == 1, "Unchanged query did not match");
        else if (reason == 9 || reason == 14) require(stats.unused == 1, "Skipped callback forced a query");
        else if (reason == 15) require(!stats.predicted && stats.unpredicted == 1, "Pause release was not counted as an unpredicted callback");
        else require(!stats.matched && (stats.mismatched || stats.unused), "Stale context was reported as a current query");
    }
}
void lifetimeAndBounds()
{
    Pair p; ShadowScope::resetTotals();
    { ShadowScope outer(p.a, true); { ShadowScope nested(p.b, true); p.b.updateMotions(); } p.a.updateMotions(); }
    conservation(); require(ShadowScope::totals().nested == 1 && ShadowScope::totals().unused == 1, "Nested update was treated as independent overlap");
    require(!p.motion(p.a).ownedSamplingBindingBytes(), "Cancelled batch retained bindings");

    ShadowScope::resetTotals();
    auto character = std::make_unique<Character>(); auto& controller = character->getMotionController();
    const LLUUID dying_id("fe000000-0000-0000-0000-0000000014a1");
    auto dying_data = asset(); std::weak_ptr<LLKeyframeMotion::JointMotionList> life = dying_data;
    LLKeyframeDataCache::addKeyframeData(dying_id, dying_data); dying_data.reset();
    require(controller.startMotion(dying_id, 0.f), "Destruction fixture activation failed");
    { ShadowScope shadow(controller, true); LLKeyframeDataCache::removeKeyframeData(dying_id); character.reset();
      require(!life.expired(), "Scope lost its immutable lease when the controller died"); }
    require(life.expired(), "Finished scope retained a destroyed controller's asset");
    conservation(); require(ShadowScope::totals().destroyed == 1, "Destroyed controller remained reachable through scope");

    ShadowScope::resetTotals();
    for (unsigned i = 1; i < 41; ++i)
    {
        LLUUID id; id.generate(); LLKeyframeDataCache::addKeyframeData(id, p.data);
        require(p.a.startMotion(id, 0.f), "Bound fixture activation failed");
    }
    { ShadowScope shadow(p.a, true); p.a.updateMotions(); }
    conservation(); require(ShadowScope::totals().predicted == ShadowScope::MAX_MOTIONS && ShadowScope::totals().rejected >= 9,
                            "Motion admission was unbounded or silently dropped");
    for (auto* motion : p.a.getActiveMotions())
        require(!static_cast<LLKeyframeMotion*>(motion)->ownedSamplingBindingBytes(), "Bound fixture retained bindings");
}

void storageLimits()
{
    for (bool oversized_keys : {false, true})
    {
        Clock::set(300.0); Character character(oversized_keys ? 1 : LL_CHARACTER_MAX_ANIMATED_JOINTS);
        auto data = asset(oversized_keys ? 1 : LL_CHARACTER_MAX_ANIMATED_JOINTS);
        if (oversized_keys)
        {
            auto& curve = data->mJointMotionArray[0]->mPositionCurve;
            curve.mKeys.clear();
            for (U32 i = 0; i < ShadowScope::MAX_RETAINED_KEYS + 1; ++i)
            {
                const F32 time = i * .0001f;
                curve.mKeys[time] = LLKeyframeMotion::PositionKey(time, LLVector3(time, -time, .25f));
            }
            curve.mNumKeys = static_cast<U32>(curve.mKeys.size());
        }
        else for (auto* joint : data->mJointMotionArray)
        {
            auto trim = [](auto& curve) { curve.mKeys.erase(std::next(curve.mKeys.begin()), curve.mKeys.end()); curve.mNumKeys = 1; };
            trim(joint->mScaleCurve); trim(joint->mRotationCurve); trim(joint->mPositionCurve);
        }
        auto& controller = character.getMotionController();
        for (unsigned i = 0; i < (oversized_keys ? 1u : 33u); ++i)
        {
            LLUUID id; id.generate(); LLKeyframeDataCache::addKeyframeData(id, data);
            require(controller.startMotion(id, 0.f), "Storage limit fixture activation failed");
        }
        ShadowScope::resetTotals();
        { ShadowScope shadow(controller, true); }
        conservation(); const auto stats = ShadowScope::totals();
        require(stats.rejected > 0, "Storage limit did not reject admission");
        if (oversized_keys) require(stats.predicted == 0, "Oversized canonical curve was retained");
        else require(stats.predicted > 0 && stats.predicted < ShadowScope::MAX_MOTIONS && stats.retainedKeysPeak < ShadowScope::MAX_RETAINED_KEYS,
                     "Buffer budget was not independently enforced");
        for (auto* motion : controller.getActiveMotions())
            require(!static_cast<LLKeyframeMotion*>(motion)->ownedSamplingBindingBytes(), "Storage rejection retained binding capacity");
        controller.deactivateAllMotions(); LLKeyframeDataCache::clear();
    }
}

struct Event
{
    unsigned label; U32 time;
    std::array<U8, LL_CHARACTER_MAX_ANIMATED_JOINTS> mask;
    bool operator==(const Event& rhs) const { return label == rhs.label && time == rhs.time && mask == rhs.mask; }
};
std::vector<Event>* trace = nullptr;
bool loadingReady = false;
bool recursiveUpdate = false;
class Recorder final : public LLNullMotion
{
public:
    unsigned label; bool loading;
    LLCharacter* owner = nullptr;
    Recorder(const LLUUID& id, unsigned value, bool pending = false): LLNullMotion(id), label(value), loading(pending) {}
    bool canDeprecate() override { return false; }
    LLMotionBlendType getBlendType() override { return label == 2 ? ADDITIVE_BLEND : NORMAL_BLEND; }
    LLJoint::JointPriority getPriority() override { return LLJoint::HIGH_PRIORITY; }
    LLMotionInitStatus onInitialize(LLCharacter* character) override
    {
        if (loading && !loadingReady) return STATUS_HOLD;
        owner = character;
        LLPointer<LLJointState> state = new LLJointState;
        state->setJoint(character->getCharacterJoint(0)); state->setUsage(LLJointState::ROT);
        state->setRotation(LLQuaternion()); state->setPriority(getPriority()); addJointState(state);
        return STATUS_SUCCESS;
    }
    bool onUpdate(F32 time, U8* mask) override
    {
        if (trace) { Event event{label, bits(time), {}}; std::copy_n(mask, event.mask.size(), event.mask.begin()); trace->push_back(event); }
        if (label == 1 && recursiveUpdate) { recursiveUpdate = false; owner->getMotionController().updateMotions(true); }
        return true;
    }
};
LLMotion* makeRegular(const LLUUID& id) { return new Recorder(id, 1); }
LLMotion* makeAdditive(const LLUUID& id) { return new Recorder(id, 2); }
LLMotion* makeLoading(const LLUUID& id) { return new Recorder(id, 3, true); }
void callbacks()
{
    const LLUUID regular("fe000000-0000-0000-0000-0000000014a2"), additive("fe000000-0000-0000-0000-0000000014a3"),
                 loading("fe000000-0000-0000-0000-0000000014a4");
    Character registrar;
    require(registrar.registerMotion(regular, makeRegular) && registrar.registerMotion(additive, makeAdditive) &&
            registrar.registerMotion(loading, makeLoading), "Callback fixture registration failed");
    for (bool pending : {false, true})
    {
        Pair p;
        if (pending) { loadingReady = false; p.a.startMotion(loading, 0.f); p.b.startMotion(loading, 0.f); }
        else
        {
            p.a.startMotion(additive, 0.f); p.b.startMotion(additive, 0.f);
            p.a.startMotion(regular, 0.f); p.b.startMotion(regular, 0.f);
        }
        ShadowScope::resetTotals(); std::vector<Event> actual, expected;
        {
            ShadowScope shadow(p.a, true);
            loadingReady = true;
            trace = &expected; p.expected.updateMotions(LLCharacter::NORMAL_UPDATE);
            trace = &actual; p.actual.updateMotions(LLCharacter::NORMAL_UPDATE); trace = nullptr;
        }
        require(actual == expected && actual.size() == 2, "Shadow changed callback times, masks or order");
        require(actual[0].label == (pending ? 3 : 2) && actual[1].label == (pending ? 3 : 1),
                "Activation/additive/regular callback ordering changed");
        p.compare(); conservation();
        if (pending) require(ShadowScope::totals().predicted == 0 && ShadowScope::totals().unpredicted == 1,
                             "Loading callbacks were treated as predictable stable motions");
    }
}

void clockPaths()
{
    for (F32 factor : {0.f, .5f, 1.f, 2.75f}) for (F32 update : {.5f, 1.5f})
    {
        Pair p; p.a.setTimeFactor(factor); p.b.setTimeFactor(factor);
        p.a.setUpdateFactor(update); p.b.setUpdateFactor(update);
        ShadowScope::resetTotals();
        { ShadowScope shadow(p.a, true); p.expected.updateMotions(LLCharacter::NORMAL_UPDATE); p.actual.updateMotions(LLCharacter::NORMAL_UPDATE); }
        p.compare(); conservation(); require(ShadowScope::totals().matched == 1, "Unchanged time/update factor query did not match");
    }
    for (unsigned path = 0; path < 3; ++path)
    {
        Pair p; LLAnimPauseRequest held_a, held_b;
        if (path == 0) { held_a = p.actual.requestPause(); held_b = p.expected.requestPause(); }
        if (path == 1) { p.a.stopMotionLocally(p.id, false); p.b.stopMotionLocally(p.id, false); }
        if (path == 2) { p.a.startMotion(p.id, -.5f); p.b.startMotion(p.id, -.5f); }
        ShadowScope::resetTotals();
        { ShadowScope shadow(p.a, true, path == 0); const auto kind = path == 0 ? LLCharacter::FORCE_UPDATE : LLCharacter::NORMAL_UPDATE;
          p.expected.updateMotions(kind); p.actual.updateMotions(kind); }
        p.compare(); conservation();
        if (path == 1) require(ShadowScope::totals().predicted == 0 && ShadowScope::totals().unpredicted == 1,
                               "Stopped motion was not excluded before its stop-clamped callback");
        else require(ShadowScope::totals().matched == 1, "Forced paused/pre-activation callback did not match");
    }
    for (S32 quantum : {10, 11})
    {
        Clock::set(400.0); Character actual, expected; ProjectionController a, b;
        a.setCharacter(&actual); b.setCharacter(&expected);
        const LLUUID id("fe000000-0000-0000-0000-0000000014a5");
        auto data = asset(); data->mEaseInDuration = .5f;
        LLKeyframeDataCache::addKeyframeData(id, data);
        require(a.startMotion(id, 0.f) && b.startMotion(id, 0.f), "Quantum fixture activation failed");
        // Seed only the controlled branch; do not enable the disabled viewer timestep setter.
        a.seed(0.f, 1.f, 1.f, false, .125f, quantum); b.seed(0.f, 1.f, 1.f, false, .125f, quantum);
        ShadowScope::resetTotals();
        { ShadowScope shadow(a, true); b.updateMotions(); a.updateMotions(); }
        conservation();
        auto* left = static_cast<LLKeyframeMotion*>(a.findMotion(id)); auto* right = static_cast<LLKeyframeMotion*>(b.findMotion(id));
        require(bits(a.getAnimTime()) == bits(b.getAnimTime()) && pose(*left) == pose(*right), "Quantum observation changed actual poses/time");
        if (quantum == 11) require(!ShadowScope::totals().predicted && !ShadowScope::totals().unpredicted,
                                   "Interpolation-only quantum fabricated a normal callback");
        else require(ShadowScope::totals().matched == 1, "New-quantum callback did not match");
        a.deactivateAllMotions(); b.deactivateAllMotions(); LLKeyframeDataCache::clear();
    }
}

void reentry()
{
    Pair p; const LLUUID regular("fe000000-0000-0000-0000-0000000014a2");
    require(p.a.startMotion(regular, 0.f) && p.b.startMotion(regular, 0.f), "Recursive fixture activation failed");
    ShadowScope::resetTotals(); std::vector<Event> actual, expected;
    {
        ShadowScope shadow(p.a, true);
        trace = &expected; recursiveUpdate = true; p.expected.updateMotions(LLCharacter::NORMAL_UPDATE);
        trace = &actual; recursiveUpdate = true; p.actual.updateMotions(LLCharacter::NORMAL_UPDATE); trace = nullptr;
    }
    require(actual == expected && actual.size() == 2, "Reentrant controller callbacks changed time, mask or order");
    p.compare(); conservation(); const auto stats = ShadowScope::totals();
    require(stats.matched == 1 && stats.unpredicted == 1, "Repeated controller callback reused a consumed prediction");
}

void queryPurity()
{
    Fixture f; LLKeyframeMotion::SamplingRequest query;
    require(f.candidate->prepareOwnedSampling(.625f, query), "Query purity fixture preparation failed");
    const auto before = pose(*f.candidate);
#if defined(_MSC_VER) && defined(_M_X64)
    const unsigned original = _mm_getcsr();
    _mm_setcsr(original | _MM_EXCEPT_MASK); const unsigned prior = _mm_getcsr();
#endif
    require(f.candidate->matchesOwnedSamplingQuery(.625f, query), "Query-only current validation failed");
    require(before == pose(*f.candidate), "Query-only validation applied a pose");
#if defined(_MSC_VER) && defined(_M_X64)
    require(_mm_getcsr() == prior, "Query validation changed owner numeric controls/status");
    LLMotionController::SamplingClock projected;
    require(f.actual.getMotionController().predictSamplingClock(false, projected) && _mm_getcsr() == prior,
            "Clock projection changed owner numeric controls/status");
    _mm_setcsr(original);
#endif
}

void preparationPurity()
{
#if defined(_MSC_VER) && defined(_M_X64)
    Pair p; p.a.setTimeFactor(1.1f); p.a.setUpdateFactor(1.3f);
    const unsigned original = _mm_getcsr();
    for (unsigned flags : {0u, unsigned(_MM_EXCEPT_MASK)})
    {
        const unsigned expected = (original & ~_MM_EXCEPT_MASK) | flags;
        _mm_setcsr(expected);
        { ShadowScope shadow(p.a, true); require(_mm_getcsr() == expected, "Preparation changed owner numeric state"); }
        require(_mm_getcsr() == expected, "Scope cleanup changed owner numeric state");
        ProjectionController controller; controller.setCharacter(&p.actual); controller.seed(.1f, 1.1f, 1.3f, false, 0.f, 0);
        LLMotionController::SamplingClock clock; _mm_setcsr(expected);
        require(controller.predictSamplingClock(false, clock) && _mm_getcsr() == expected, "Inexact projection leaked owner status");
    }
    _mm_setcsr(original);
#endif
}

int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    Pair p; ShadowScope::resetTotals();
    for (unsigned i = 0; i < 8; ++i) { p.a.updateMotions(); p.b.updateMotions(); }
    FSSamplingAlloc::Stats plain, disabled;
    { FSSamplingAlloc::Scope probe; for (unsigned i = 0; i < 32; ++i) p.a.updateMotions(); plain = FSSamplingAlloc::snapshot(); }
    { FSSamplingAlloc::Scope probe; for (unsigned i = 0; i < 32; ++i) { ShadowScope scope(p.b, false); p.b.updateMotions(); }
      disabled = FSSamplingAlloc::snapshot(); }
    require(plain.calls == disabled.calls && plain.requested == disabled.requested && !ShadowScope::totals().batches,
            "Disabled diagnostic added allocations or a batch");
    require(!p.motion(p.a).ownedSamplingBindingBytes() && !p.motion(p.b).ownedSamplingBindingBytes(),
            "Disabled diagnostics created owner bindings");
    for (int failure : {0, 1, 2})
    {
        bool escaped = false; FSSamplingAlloc::Stats result;
        const auto rejected = ShadowScope::totals().rejected;
        {
            FSSamplingAlloc::Scope probe; FSSamplingAlloc::failureCountdown = failure;
            try
            {
                ShadowScope scope(p.a, true);
                FSSamplingAlloc::failureCountdown = -1;
                require(!FSSampling::activeShadow, "Failed collection stayed active");
                p.a.updateMotions();
            }
            catch (const std::bad_alloc&) { escaped = true; }
            FSSamplingAlloc::failureCountdown = -1;
            result = FSSamplingAlloc::snapshot();
        }
        require(!escaped && !FSSampling::activeShadow && ShadowScope::totals().rejected > rejected,
                "Diagnostic allocation failure escaped instead of retaining normal playback");
        require(!p.motion(p.a).ownedSamplingBindingBytes() && result.live == result.baseline,
                "Failed constructor leaked bindings or diagnostic allocations");
        require(ShadowScope::resetTotals(), "Failed scope prevented the next batch");
    }
    FSSamplingAlloc::Stats enabled;
    { FSSamplingAlloc::Scope probe;
      { ShadowScope scope(p.a, true); p.a.updateMotions(); }
      enabled = FSSamplingAlloc::snapshot(); }
    require(enabled.calls > 0 && enabled.live == enabled.baseline && !p.motion(p.a).ownedSamplingBindingBytes(),
            "Completed diagnostic batch retained allocation capacity");
    conservation(); p.compare();
    std::cout << "mode,plain_calls,disabled_calls,plain_bytes,disabled_bytes,enabled_calls,enabled_live_delta\n"
              << "alloc," << plain.calls << ',' << disabled.calls << ',' << plain.requested << ',' << disabled.requested
              << ',' << enabled.calls << ',' << enabled.live - enabled.baseline << '\n';
    std::cout << checks << " allocation/failure/cleanup checks passed\n";
#endif
    return 0;
}
int run()
{
    if (!FSSamplingTest::supportedContext("sampling shadow"))
    {
        Pair p; ShadowScope::resetTotals(); LLMotionController::SamplingClock clock;
        require(!p.a.predictSamplingClock(false, clock), "Unsupported context projected a query");
        { ShadowScope shadow(p.a, true); p.expected.updateMotions(LLCharacter::NORMAL_UPDATE); p.actual.updateMotions(LLCharacter::NORMAL_UPDATE); }
        p.compare(); conservation(); require(!ShadowScope::totals().predicted, "Unsupported context admitted owned queries");
        std::cout << checks << " unsupported-context direct playback/accounting checks passed\n";
        return 0;
    }
    LLVector4a::initClass(); projection(); mutations(); lifetimeAndBounds(); storageLimits(); callbacks(); clockPaths(); reentry(); queryPurity(); preparationPurity();
    std::cout << checks << " real-controller projection, shadow, lifecycle and bounded-accounting checks passed\n";
    return 0;
}
}
int main(int argc, char** argv)
{
    if (FS_SAMPLING_ALLOCATION_PROBE)
    {
        LLVector4a::initClass();
        if (!FSSamplingTest::supportedContext("sampling shadow allocations")) return 0;
        return argc == 2 && std::string(argv[1]) == "--allocations" ? SamplingShadowTest::allocations() : 0;
    }
    return SamplingShadowTest::run();
}
#if defined(_MSC_VER) && defined(_M_X64)
#pragma fenv_access(off)
#pragma float_control(pop)
#endif
