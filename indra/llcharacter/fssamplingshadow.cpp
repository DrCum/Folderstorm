#include "linden_common.h"
#include "fssamplingshadow.h"
#include "llmotioncontroller.h"
#include "llkeyframemotion.h"
#include <chrono>
#include <algorithm>
#include <cstring>
#include <new>

namespace FSSampling
{
thread_local ShadowScope* activeShadow = nullptr;
namespace
{
thread_local ShadowTotals sShadowTotals;
using ShadowClock = std::chrono::steady_clock;
std::uint64_t shadowElapsed(ShadowClock::time_point start)
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ShadowClock::now() - start).count());
}
}

struct ShadowScope::Impl
{
    struct Entry { LLKeyframeMotion::SamplingRequest query; LLUUID id; U64 instance = 0; bool seen = false, admitted = false; };
    std::array<Entry, MAX_MOTIONS> entries;
    std::array<U64, MAX_MOTIONS> masked{}; // Instance tokens only; zero marks an observed estimate.
    U8 covered[2][LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
    size_t maskedCount = 0;
    size_t count = 0, bufferBytes = sizeof(Impl), retainedKeys = 0;
    std::uint64_t observationNs = 0;
    bool ordinaryObserved = false;
    ShadowClock::time_point preparedAt;
};

#if defined(_MSC_VER) && defined(_M_X64)
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#endif
ShadowScope::ShadowScope(LLMotionController& controller, bool enabled, bool force_update, bool will_unpause)
{
    if (!enabled) return;
    const auto start = ShadowClock::now();
    ++sShadowTotals.scopes;
    mPrevious = activeShadow;
    mRegistered = true;
    activeShadow = this;
    if (mPrevious)
    {
        mPrevious->mCancelled = true;
        ++sShadowTotals.nested;
        sShadowTotals.prepareNs += shadowElapsed(start);
        return; // Nested work suspends observation and invalidates the outer opportunity.
    }
    try
    {
        if (!controller.mCharacter || !controller.mCharacter->allowsOwnedKeyframeSampling())
        {
            sShadowTotals.prepareNs += shadowElapsed(start);
            return;
        }
        mController = &controller;
        {
            ScopedNumericContext preserve(NumericContext::capture());
            mImpl = std::make_unique<Impl>();
            static_assert(sizeof(Impl) < MAX_BUFFER_BYTES);
            ++sShadowTotals.batches;
            LLMotionController::SamplingClock clock;
            const bool project = controller.predictSamplingClock(force_update, clock, will_unpause) &&
                controller.mLoadingMotions.empty();
            for (auto* motion : controller.mActiveMotions)
            {
                auto* keyframe = dynamic_cast<LLKeyframeMotion*>(motion);
                F32 time = 0.f;
                if (!keyframe || !keyframe->permitsOwnedSampling() || !project ||
                    !controller.predictSamplingTime(*motion, clock, time))
                {
                    std::memset(mImpl->covered, 0, sizeof(mImpl->covered));
                    ++sShadowTotals.excluded;
                    continue;
                }
                const bool callback = controller.predictSamplingMask(*motion, mImpl->covered);
                const U64 instance = keyframe->getOwnedSamplingInstance();
                if (!callback && instance && mImpl->maskedCount < MAX_MOTIONS)
                {
                    mImpl->masked[mImpl->maskedCount++] = instance;
                    ++sShadowTotals.masked;
                    continue;
                }
                if (mImpl->count == MAX_MOTIONS) { ++sShadowTotals.rejected; continue; }
                auto& entry = mImpl->entries[mImpl->count++];
                entry.id = motion->getID();
                entry.instance = keyframe->getOwnedSamplingInstance();
                if (!keyframe->prepareOwnedSampling(time, entry.query))
                {
                    keyframe->releaseOwnedSamplingBindings(entry.instance);
                    entry = {}; --mImpl->count; ++sShadowTotals.rejected; continue;
                }
                size_t keys = 0;
                for (const auto* joint : entry.query.asset->mJointMotionArray)
                    keys += joint->mScaleCurve.mKeys.size() + joint->mRotationCurve.mKeys.size() + joint->mPositionCurve.mKeys.size();
                const size_t buffers = entry.query.usage.capacity() * sizeof(U32) + keyframe->ownedSamplingBindingBytes();
                if (keys > MAX_RETAINED_KEYS - mImpl->retainedKeys || buffers > MAX_BUFFER_BYTES - mImpl->bufferBytes)
                {
                    keyframe->releaseOwnedSamplingBindings(entry.instance);
                    entry = {}; --mImpl->count;
                    ++sShadowTotals.rejected;
                    continue;
                }
                mImpl->retainedKeys += keys;
                mImpl->bufferBytes += buffers;
                entry.admitted = true;
                ++sShadowTotals.predicted;
            }
            sShadowTotals.bufferBytesPeak = std::max(sShadowTotals.bufferBytesPeak, std::uint64_t(mImpl->bufferBytes));
            sShadowTotals.retainedKeysPeak = std::max(sShadowTotals.retainedKeysPeak, std::uint64_t(mImpl->retainedKeys));
        }
        sShadowTotals.prepareNs += shadowElapsed(start);
        mImpl->preparedAt = ShadowClock::now(); // Preparation itself is not independent overlap.
    }
    catch (const std::bad_alloc&)
    {
        sShadowTotals.prepareNs += shadowElapsed(start);
        ++sShadowTotals.rejected;
        finish(); // Diagnostic memory pressure leaves ordinary playback available.
    }
    catch (...) { sShadowTotals.prepareNs += shadowElapsed(start); finish(); throw; }
}
#if defined(_MSC_VER) && defined(_M_X64)
#pragma fenv_access(off)
#pragma float_control(pop)
#endif

ShadowScope::~ShadowScope()
{
    finish();
}

void ShadowScope::finish()
{
    if (!mRegistered) return;
    const auto start = ShadowClock::now();
    if (mImpl)
    {
        for (size_t i = 0; i < mImpl->maskedCount; ++i)
            if (mImpl->masked[i]) ++sShadowTotals.maskedUnobserved;
        for (size_t i = 0; i < mImpl->count; ++i)
        {
            const auto& entry = mImpl->entries[i];
            if (entry.admitted && !entry.seen) ++sShadowTotals.unused;
            // Resolve only through the controller's current owning containers. An ID/token never dereferences a pointer.
            if (mController)
            {
                const auto release = [&](LLMotion* motion)
                {
                    auto* keyframe = dynamic_cast<LLKeyframeMotion*>(motion);
                    return keyframe && keyframe->releaseOwnedSamplingBindings(entry.instance);
                };
                const auto canonical = mController->mAllMotions.find(entry.id);
                const bool found = canonical != mController->mAllMotions.end() && release(canonical->second);
                if (!found) for (auto* motion : mController->mDeprecatedMotions) if (release(motion)) break;
            }
        }
        mImpl.reset(); // No leases, usage buffers or owner binding capacities persist between diagnostic batches.
    }
    activeShadow = mPrevious;
    mRegistered = false;
    sShadowTotals.cleanupNs += shadowElapsed(start);
}

void ShadowScope::observe(LLMotionController& controller, LLMotion& motion, float time)
{
    if (mCancelled || mController != &controller || !mImpl) return;
    const auto entered = ShadowClock::now();
    struct ObservationTimer
    {
        Impl& state;
        ShadowClock::time_point start;
        ~ObservationTimer()
        {
            const auto elapsed = shadowElapsed(start);
            state.observationNs += elapsed;
            sShadowTotals.observationNs += elapsed;
        }
    } timer{*mImpl, entered};
    auto* keyframe = dynamic_cast<LLKeyframeMotion*>(&motion);
    if (!keyframe || !keyframe->getOwnedSamplingInstance()) return;
    const bool first = !mImpl->ordinaryObserved;
    mImpl->ordinaryObserved = true;
    const U64 instance = keyframe->getOwnedSamplingInstance();
    for (size_t i = 0; i < mImpl->count; ++i)
    {
        auto& entry = mImpl->entries[i];
        if (entry.seen || entry.query.identity.instance != instance) continue;
        const auto raw_gap = std::chrono::duration_cast<std::chrono::nanoseconds>(entered - mImpl->preparedAt).count();
        const auto gap = std::uint64_t(raw_gap) > mImpl->observationNs ? std::uint64_t(raw_gap) - mImpl->observationNs : 0;
        const auto start = ShadowClock::now();
        const bool matches = keyframe->matchesOwnedSamplingQuery(time, entry.query);
        sShadowTotals.validationNs += shadowElapsed(start);
        entry.seen = true;
        if (matches)
        {
            ++sShadowTotals.matched;
            unsigned bucket = 0;
            while (bucket + 1 < sShadowTotals.matchedGapNs.size() && gap > (U64(1) << bucket)) ++bucket;
            ++sShadowTotals.matchedGapNs[bucket];
            if (first) { ++sShadowTotals.firstMatched; ++sShadowTotals.firstMatchedGapNs[bucket]; }
        }
        else ++sShadowTotals.mismatched;
        return;
    }
    for (size_t i = 0; i < mImpl->maskedCount; ++i)
        if (mImpl->masked[i] == instance)
        {
            mImpl->masked[i] = 0;
            ++sShadowTotals.maskedObserved;
            break;
        }
    ++sShadowTotals.unpredicted;
}

void ShadowScope::controllerDestroyed(LLMotionController& controller)
{
    for (auto* scope = activeShadow; scope; scope = scope->mPrevious)
        if (scope->mController == &controller)
        {
            scope->mController = nullptr;
            scope->mCancelled = true;
            ++sShadowTotals.destroyed;
        }
}
ShadowTotals ShadowScope::totals() { return sShadowTotals; }
bool ShadowScope::resetTotals()
{
    if (activeShadow) return false;
    sShadowTotals = {};
    return true;
}
}
