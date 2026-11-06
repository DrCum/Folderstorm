#include "linden_common.h"
#include "current_motion_fixture.h"
#include "sampling_test_context.h"
#include "llkeyframewalkmotion.h"
#include "llkeyframestandmotion.h"
#include "llkeyframefallmotion.h"
#include "lldatapacker.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#endif

namespace CurrentMotionTest
{
using namespace FSCurrentMotion;
using Request = LLKeyframeMotion::SamplingRequest;
using Result = LLKeyframeMotion::SamplingResult;
unsigned checks = 0, accepted = 0, fallbacks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
void counters()
{
    std::atomic<U64> next{1}; std::array<std::vector<U64>, 4> values;
    std::vector<std::thread> workers;
    for (auto& output : values) workers.emplace_back([&next, &output]
        { for (unsigned i = 0; i < 256; ++i) output.push_back(FSSampling::takeIdentity(next)); });
    for (auto& worker : workers) worker.join();
    std::set<U64> unique;
    for (const auto& row : values) for (auto value : row) require(value && unique.insert(value).second, "Instance identity reused");
    std::atomic<U64> end{std::numeric_limits<U64>::max()};
    require(FSSampling::takeIdentity(end) == std::numeric_limits<U64>::max(), "Final identity skipped");
    require(!FSSampling::takeIdentity(end) && !FSSampling::takeIdentity(end), "Exhausted identity wrapped");
    U64 epoch = std::numeric_limits<U64>::max(); FSSampling::advanceGeneration(epoch);
    require(!epoch, "Generation did not disable at exhaustion");
    FSSampling::advanceGeneration(epoch); require(!epoch, "Disabled generation became valid again");
}
Result sample(LLKeyframeMotion& motion, F32 time = .625f)
{
    Request input; Result result;
    require(motion.prepareOwnedSampling(time, input), "Ordinary factory preparation rejected");
    require(input.identity.instance && input.identity.activation && input.identity.edit && input.identity.frameTimeBits,
            "Current identity has missing owner authority");
    bool ok = false;
    std::thread worker([owned = std::move(input), &result, &ok]() mutable
        { ok = LLKeyframeMotion::sampleKeyframes(std::move(owned), result); });
    worker.join(); require(ok, "Owned sampling failed"); return result;
}
void compare(Fixture& fixture, Result& result, bool expected_accept, F32 time = .625f)
{
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; bool applied = false;
    const bool old = fixture.reference->onUpdate(time, mask);
    const bool current = fixture.candidate->updateWithOwnedSampling(time, mask, result, applied);
    require(old == current && pose(*fixture.reference) == pose(*fixture.candidate), "Owner application/fallback changed direct pose or continuation");
    require(applied == expected_accept, "Current identity accepted stale input or rejected current input");
    if (applied) ++accepted; else ++fallbacks;
}
class Copied final : public LLKeyframeMotion
{
public:
    explicit Copied(const LLKeyframeMotion& source): LLKeyframeMotion(source) {}
    unsigned calls = 0;
    bool onUpdate(F32 time, U8*) override { ++calls; return time < 0.f; }
};
void eligibility()
{
    Fixture f;
    require(f.candidate->permitsOwnedSampling(), "Ordinary factory did not grant capability");
    Request input;
    LLKeyframeMotion raw(f.id);
    require(!raw.permitsOwnedSampling() && !raw.prepareOwnedSampling(.625f, input), "Raw upload/preview constructor became eligible");
    Copied copy(*f.candidate);
    require(!copy.permitsOwnedSampling() && !copy.prepareOwnedSampling(.625f, input), "Base copy inherited sampling permission");
    Result result = sample(*f.candidate); U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; bool applied = true;
    require(!copy.updateWithOwnedSampling(.625f, mask, result, applied) && !applied && copy.calls == 1,
            "Ineligible subclass virtual update was bypassed");
    auto assigned = motion(f.actual, f.id); *assigned = *f.candidate;
    require(!assigned->permitsOwnedSampling() && !assigned->prepareOwnedSampling(.625f, input), "Assignment inherited factory permission");
    for (auto create : {LLKeyframeWalkMotion::create, LLKeyframeStandMotion::create, LLKeyframeFallMotion::create})
    {
        std::unique_ptr<LLMotion> base(create(f.id)); auto* derived = dynamic_cast<LLKeyframeMotion*>(base.get());
        require(derived && !derived->permitsOwnedSampling() && !derived->prepareOwnedSampling(.625f, input),
                "Custom keyframe factory was automatically eligible");
    }
}
void editsAndClocks()
{
    for (unsigned reason = 0; reason < 28; ++reason)
    {
        Fixture f; Character other_actual, other_expected;
        auto result = sample(*f.candidate);
        bool valid = reason == 0 || reason == 20 || reason == 21;
        F32 time = .625f;
        if (reason == 1) Clock::count();
        if (reason == 2) Clock::set(100.000001, false); // Modal clock update without a frame increment.
        if (reason == 3) Clock::set(102.0);
        if (reason == 4) { f.actual.setSkeletonSerialNum(1); f.expected.setSkeletonSerialNum(1); }
        if (reason == 5)
        {
            f.candidate->getPose()->getFirstJointState()->setUsage(LLJointState::POS);
            f.reference->getPose()->getFirstJointState()->setUsage(LLJointState::POS);
        }
        if (reason == 6)
        {
            f.candidate->getPose()->getFirstJointState()->setJoint(&f.actual.spare);
            f.reference->getPose()->getFirstJointState()->setJoint(&f.expected.spare);
        }
        if (reason == 7) { f.candidate->setCharacter(&other_actual); f.reference->setCharacter(&other_expected); }
        if (reason == 8) { f.candidate->setStopTime(.4f); f.reference->setStopTime(.4f); }
        if (reason == 9) { f.candidate->setStopped(true); f.reference->setStopped(true); }
        if (reason == 10) { f.candidate->activate(0.f); f.reference->activate(0.f); }
        if (reason == 11) { f.candidate->onDeactivate(); f.reference->onDeactivate(); }
        if (reason == 12) f.candidate->setLoop(true);
        if (reason == 13) f.candidate->setLoopIn(.125f);
        if (reason == 14) f.candidate->setLoopOut(1.25f);
        if (reason == 15) f.candidate->setHandPose(LLHandMotion::HAND_POSE_FIST);
        if (reason == 16) f.candidate->setEaseIn(.333f);
        if (reason == 17) f.candidate->setEaseOut(.333f);
        if (reason == 18) { f.candidate->setPriority(LLJoint::HIGH_PRIORITY); f.reference->setPriority(LLJoint::HIGH_PRIORITY); }
        if (reason == 19) f.candidate->setEmote(LLUUID::null);
        if (reason == 20) LLKeyframeDataCache::clear();
        if (reason == 21) LLKeyframeDataCache::addKeyframeData(f.id, asset()); // Active owners keep their old publication.
        if (reason == 22) result.takeRequest();
#if defined(_MSC_VER) && defined(_M_X64)
        const unsigned previous = _mm_getcsr();
        if (reason == 23) _mm_setcsr(previous ^ _MM_FLUSH_ZERO_MASK);
#endif
        if (reason == 24) time = 1.f;
        if (reason == 25) result = Result{};
        if (reason == 26) f.actual.allowOwned = false;
        if (reason == 27) { f.actual.setAppearanceSerialNum(1); f.expected.setAppearanceSerialNum(1); }
        compare(f, result, valid, time);
#if defined(_MSC_VER) && defined(_M_X64)
        _mm_setcsr(previous);
#endif
    }
}
void lifetimeAndController()
{
    const LLUUID id("fe000000-0000-0000-0000-0000000014f1");
    Clock::set(200.0); auto a = std::make_unique<Character>(); Character b;
    auto data = asset(); std::weak_ptr<LLKeyframeMotion::JointMotionList> life = data;
    LLKeyframeDataCache::addKeyframeData(id, data);
    a->getMotionController().updateMotions(); b.getMotionController().updateMotions();
    require(a->startMotion(id), "Real controller default factory activation failed");
    auto* first = dynamic_cast<LLKeyframeMotion*>(a->findMotion(id));
    require(first && first->permitsOwnedSampling(), "Registry default was not ordinary factory behavior");
    auto result = sample(*first, 0.f); const U64 first_instance = result.getRequest().identity.instance;
    require(a->stopMotion(id, true), "Controller stop failed");
    require(a->startMotion(id), "Controller reactivation failed");
    auto* current = dynamic_cast<LLKeyframeMotion*>(a->findMotion(id));
    Request new_input; require(current && current->prepareOwnedSampling(0.f, new_input), "Reactivated context unavailable");
    require(!(new_input.identity == result.getRequest().identity), "Controller stop/reactivation reused authority");
    a.reset(); data.reset(); LLKeyframeDataCache::clear();
    require(!life.expired(), "Outstanding result lost destroyed motion's immutable asset");
    LLKeyframeDataCache::addKeyframeData(id, asset());
    require(b.startMotion(id), "Same-ID replacement failed");
    current = dynamic_cast<LLKeyframeMotion*>(b.findMotion(id));
    require(current && current->prepareOwnedSampling(0.f, new_input) && new_input.identity.instance != first_instance,
            "Same-ID replacement reused old instance authority");
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; bool applied = true;
    current->updateWithOwnedSampling(0.f, mask, result, applied);
    require(!applied, "Destroyed/same-ID old result applied to replacement");
    new_input = {}; result.takeRequest();
    require(life.expired(), "Old result consumption retained dead asset data");
    LLKeyframeDataCache::clear();
}
void publication()
{
    Fixture f; auto old = sample(*f.candidate);
    std::vector<U8> packet(f.candidate->getFileSize());
    LLDataPackerBinaryBuffer output(packet.data(), static_cast<S32>(packet.size()));
    require(f.candidate->serialize(output), "Publication fixture serialization failed");
    LLDataPackerBinaryBuffer input(packet.data(), static_cast<S32>(packet.size()));
    require(f.candidate->deserialize(input, f.id), "Valid replacement publication failed");
    Request current;
    require(f.candidate->prepareOwnedSampling(.625f, current), "Replacement publication context unavailable");
    require(current.identity.instance == old.getRequest().identity.instance &&
            current.identity.activation != old.getRequest().identity.activation &&
            current.asset.get() != old.getRequest().asset.get(), "Publication reused old asset/binding context");
    U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{}; bool applied = true;
    f.candidate->updateWithOwnedSampling(.625f, mask, old, applied);
    require(!applied, "Old publication applied after a successful reload");
    Result replacement;
    require(LLKeyframeMotion::sampleKeyframes(std::move(current), replacement), "Replacement sample failed");
    f.candidate->updateWithOwnedSampling(.625f, mask, replacement, applied);
    require(applied, "Current replacement publication rejected");
}
int run()
{
    counters();
    if (!FSSamplingTest::supportedContext("current ordinary-motion owner context")) return 0;
    eligibility(); editsAndClocks(); lifetimeAndController(); publication();
    std::cout << checks << " current-owner checks; " << accepted << " current applications and " << fallbacks
              << " stale fallbacks passed with actual factories/controller\n";
    return 0;
}
}
int main() { return CurrentMotionTest::run(); }
