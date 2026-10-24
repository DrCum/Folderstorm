#include "linden_common.h"
#include "llkeyframemotion.h"
#include "keyframe_test_character.h"
#include "sampling_test_context.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#endif

namespace SamplingFPMathTest
{
unsigned checks = 0, differences = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
class Probe final : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    using Joint = JointMotion;
    using Request = SamplingRequest;
    using Result = SamplingResult;
    using Identity = SamplingIdentity;
    using Row = SampledJoint;
    using SK = ScaleKey; using RK = RotationKey; using PK = PositionKey;
    using LLKeyframeMotion::sampleKeyframes;
    using LLKeyframeMotion::samplingResultMatches;
    using LLKeyframeMotion::prepareKeyframeSampling;
    using LLKeyframeMotion::updateWithSample;
    Probe(): LLKeyframeMotion(LLUUID::null) {}
    void init(LLCharacter& character, std::shared_ptr<Asset> data)
    {
        setCharacter(&character); adoptJointMotionList(std::move(data)); mAssetStatus = ASSET_LOADED;
        mJointStates.push_back(seed()); setStopped(false);
    }
    static LLPointer<LLJointState> seed()
    {
        LLPointer<LLJointState> state = new LLJointState;
        state->setUsage(7); state->setPosition(LLVector3(101, -101, .125f));
        state->setScale(LLVector3(2, 3, 4)); state->setRotation(LLQuaternion(.5f, .5f, .5f, .5f));
        return state;
    }
    const LLJointState& state() const { return *mJointStates[0]; }
};
using Bits = std::vector<U32>;
Bits bits(const LLJointState& state)
{
    Bits output;
    auto append = [&](const F32* value, unsigned count)
    { for (unsigned i = 0; i < count; ++i) { U32 raw; std::memcpy(&raw, &value[i], 4); output.push_back(raw); } };
    append(state.getScale().mV, 3); append(state.getRotation().mQ, 4); append(state.getPosition().mV, 3);
    return output;
}
F32 value(U32 raw) { F32 result; std::memcpy(&result, &raw, 4); return result; }
std::shared_ptr<Probe::Asset> asset()
{
    auto data = std::make_shared<Probe::Asset>(); data->mDuration = 2.f;
    auto* joint = new Probe::Joint; joint->mUsage = 7; joint->mPriority = LLJoint::MEDIUM_PRIORITY;
    const F32 normal = value(0x00800000), subnormal = value(0x00400000), tiny = value(2);
    joint->mScaleCurve.mKeys[0.f] = Probe::SK(0.f, LLVector3(normal, subnormal, 1.1f));
    joint->mScaleCurve.mKeys[1.f] = Probe::SK(1.f, LLVector3(0.f, tiny, 2.2f));
    joint->mRotationCurve.mKeys[0.f] = Probe::RK(0.f, LLQuaternion(subnormal, 0.f, 0.f, 1.f));
    joint->mRotationCurve.mKeys[1.f] = Probe::RK(1.f, LLQuaternion(0.f, subnormal, 0.f, 1.f));
    joint->mPositionCurve.mKeys[0.f] = Probe::PK(0.f, LLVector3(normal, subnormal, .1f));
    joint->mPositionCurve.mKeys[1.f] = Probe::PK(1.f, LLVector3(-normal, tiny, .33333334f));
    joint->mScaleCurve.mNumKeys = joint->mRotationCurve.mNumKeys = joint->mPositionCurve.mNumKeys = 2;
    data->mJointMotionArray.push_back(joint); return data;
}
Probe::Request request(const std::shared_ptr<Probe::Asset>& data, FSSampling::NumericContext context)
{
    Probe::Request input; input.asset = data; input.numericContext = context;
    input.identity = {1, 2, 3, 4, 5}; input.time = .625f; input.duration = data->mDuration;
    input.usage.assign(data->mJointMotionArray.size(), 7); return input;
}
Bits sampled(const Probe::Result& result)
{
    auto state = Probe::seed(); const auto& row = result.getJoints()[0];
    if (row.usage & LLJointState::SCALE) state->setScale(row.scale);
    if (row.usage & LLJointState::ROT) state->setRotation(row.rotation);
    if (row.usage & LLJointState::POS) state->setPosition(row.position);
    return bits(*state);
}
int run()
{
    if (!FSSamplingTest::supportedContext("actual curve numeric-context math")) return 0;
#if defined(_MSC_VER) && defined(_M_X64)
    const unsigned original = _mm_getcsr();
    const auto data = asset();
    for (unsigned owner_denormal : {0u, unsigned(_MM_FLUSH_ZERO_MASK), unsigned(_MM_DENORMALS_ZERO_MASK), unsigned(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK)})
    {
        const unsigned owner = _MM_MASK_MASK | owner_denormal;
        _mm_setcsr(owner);
        const auto owned_context = FSSampling::NumericContext::capture();
        auto expected = Probe::seed(); data->getJointMotion(0)->update(expected.get(), .625f, 2.f);
        const auto expected_bits = bits(*expected);
        std::vector<Probe::Request> inputs;
        for (unsigned i = 0; i < 32; ++i) inputs.push_back(request(data, owned_context));
        std::vector<Probe::Result> output(32);
        std::thread worker([&, inputs = std::move(inputs)]() mutable
        {
            const unsigned previous_thread = _mm_getcsr();
            unsigned i = 0;
            for (unsigned rounding : {unsigned(_MM_ROUND_NEAREST), unsigned(_MM_ROUND_DOWN), unsigned(_MM_ROUND_UP), unsigned(_MM_ROUND_TOWARD_ZERO)})
                for (unsigned denormal : {0u, unsigned(_MM_FLUSH_ZERO_MASK), unsigned(_MM_DENORMALS_ZERO_MASK), unsigned(_MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK)})
                    for (unsigned flags : {0u, 0x3fu})
                    {
                        const unsigned prior = _MM_MASK_MASK | rounding | denormal | flags;
                        _mm_setcsr(prior);
                        require(Probe::sampleKeyframes(std::move(inputs[i]), output[i]), "Guarded worker sample failed");
                        require(_mm_getcsr() == prior, "Sampler did not restore worker controls/status");
                        require(sampled(output[i]) == expected_bits, "Guarded curve math differs from owner JointMotion update");
                        auto unguarded = Probe::seed(); data->getJointMotion(0)->update(unguarded.get(), .625f, 2.f);
                        if (bits(*unguarded) != expected_bits) ++differences;
                        ++i;
                    }
            for (unsigned kind = 0; kind < 4; ++kind)
            {
                const unsigned prior = _MM_MASK_MASK | _MM_ROUND_UP | _MM_EXCEPT_MASK;
                auto bad = request(data, owned_context);
                if (kind == 0) bad.numericContext = {};
                if (kind == 1) bad.time = std::numeric_limits<F32>::infinity();
                if (kind == 2) bad.usage.push_back(7);
                if (kind == 3)
                {
                    auto malformed = asset(); malformed->mJointMotionArray.push_back(nullptr);
                    bad.asset = malformed; bad.usage.push_back(7);
                }
                _mm_setcsr(prior);
                Probe::Result failed;
                require(!Probe::sampleKeyframes(std::move(bad), failed) && !failed.isReady() && failed.getJoints().empty(),
                        "Rejected/partial sample exposed a result");
                require(_mm_getcsr() == prior, "Rejected/partial sampling leaked worker numeric state");
            }
            _mm_setcsr(previous_thread);
        });
        worker.join();
        for (const auto& row : output)
        {
            auto current = request(data, FSSampling::NumericContext::capture());
            require(Probe::samplingResultMatches(row, current), "Current owner controls rejected");
            _mm_setcsr(owner ^ _MM_FLUSH_ZERO_MASK);
            auto changed = request(data, FSSampling::NumericContext::capture());
            require(!Probe::samplingResultMatches(row, changed), "Changed current controls accepted stale result");
            _mm_setcsr(owner | _MM_EXCEPT_MASK);
            auto flags_only = request(data, FSSampling::NumericContext::capture());
            require(Probe::samplingResultMatches(row, flags_only), "Sticky flags incorrectly invalidate current result");
            _mm_setcsr(owner);
        }
        Probe::Result local;
        auto local_request = request(data, owned_context);
        _mm_setcsr(owner | _MM_EXCEPT_MASK); const auto prior = _mm_getcsr();
        require(Probe::sampleKeyframes(std::move(local_request), local) && _mm_getcsr() == prior,
                "Same-thread sampler changed caller numeric state");

        FSKeyframeTests::Character a, b; Probe candidate, direct;
        candidate.init(a, data); direct.init(b, data);
        Probe::Request input; Probe::Result result;
        const Probe::Identity identity{1, 2, 3, 4, 5};
        require(candidate.prepareKeyframeSampling(.625f, identity, input), "Live owner preparation failed");
        require(Probe::sampleKeyframes(std::move(input), result), "Live prepared sample failed");
        U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; bool applied = false;
        direct.onUpdate(.625f, mask); candidate.updateWithSample(.625f, mask, result, identity, applied);
        require(applied && bits(direct.state()) == bits(candidate.state()), "Current numeric live application changed pose");
        _mm_setcsr(owner ^ _MM_FLUSH_ZERO_MASK);
        direct.onUpdate(.625f, mask); candidate.updateWithSample(.625f, mask, result, identity, applied);
        require(!applied && bits(direct.state()) == bits(candidate.state()), "Changed live controls did not use exact direct fallback");
        _mm_setcsr(_MM_MASK_MASK | _MM_ROUND_UP);
        require(!candidate.prepareKeyframeSampling(.625f, identity, input), "Unsupported live owner mode prepared work");
        direct.onUpdate(.625f, mask); candidate.updateWithSample(.625f, mask, result, identity, applied);
        require(!applied && bits(direct.state()) == bits(candidate.state()), "Unsupported owner changed direct playback/fallback");
    }
    _mm_setcsr(original);
    require(differences > 0, "Negative control did not expose any unguarded actual-math mismatch");
    std::cout << checks << " actual numeric sampling/application checks passed over 128 worker cases; "
              << differences << " unguarded actual-math mismatches reproduced\n";
#endif
    return 0;
}
}
int main() { return SamplingFPMathTest::run(); }
#if defined(_MSC_VER) && defined(_M_X64)
#pragma fenv_access(off)
#pragma float_control(pop)
#endif
