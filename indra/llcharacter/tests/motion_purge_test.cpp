// Real motion ownership and eviction compared with the independent parent purge.
#include "linden_common.h"
#include "keyframe_test_character.h"
#include "llframetimer.h"
#include "llsimdmath.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace MotionPurgeTest
{
unsigned checks = 0, cases = 0;
unsigned caseCount = 0, caseActive = 0, caseDuplicate = 0;
const char* stage = "initialization";
void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::cerr << message << " (count=" << caseCount << ", active=" << caseActive
                  << ", duplicate=" << caseDuplicate << ", stage=" << stage << ")\n";
        std::exit(1);
    }
    ++checks;
}
U32 bits(F32 value) { U32 result; std::memcpy(&result, &value, sizeof(result)); return result; }
class Clock : public LLFrameTimer
{
public:
    static void set(F64 time) { sFrameTime = time; ++sFrameCount; }
};
class Character : public FSKeyframeTests::Character
{
public:
    LLJoint joint;
    unsigned created = 0;
    std::vector<std::array<U32, 4>> events;
    Character() { joint.setName("joint"); joint.mJointNum = 0; }
    LLJoint* getRootJoint() override { return &joint; }
    LLJoint* getCharacterJoint(U32 i) override { return i ? nullptr : &joint; }
    F32 getPixelArea() const override { return 10000.f; }
};
class Motion : public LLNullMotion
{
public:
    Character* owner = nullptr;
    unsigned serial = 0;
    bool loading = false;
    explicit Motion(const LLUUID& id): LLNullMotion(id) {}
    ~Motion() override { event(4); }
    void event(U32 kind, U32 time = 0, U32 mask = 0)
    { if (owner) owner->events.push_back({kind, serial, time, mask}); }
    LLMotionInitStatus onInitialize(LLCharacter* character) override
    {
        if (!owner)
        {
            owner = static_cast<Character*>(character); serial = owner->created++;
            LLPointer<LLJointState> state = new LLJointState;
            state->setJoint(&owner->joint); state->setUsage(LLJointState::POS);
            state->setPosition(LLVector3(F32(serial + 1), 2.f, 3.f));
            addJointState(state); getPose()->setWeight(1.f);
        }
        return loading ? STATUS_HOLD : STATUS_SUCCESS;
    }
    bool onActivate() override { event(1); return true; }
    bool onUpdate(F32 time, U8* mask) override { event(2, bits(time), mask ? mask[0] : 0); return true; }
    void onDeactivate() override { event(3); }
};
LLMotion* create(const LLUUID& id) { return new Motion(id); }
LLMotion* createLoading(const LLUUID& id)
{ auto* motion = new Motion(id); motion->loading = true; return motion; }

class Controller : public LLMotionController
{
    void referencePurge();
public:
    void purge(bool reference) { if (reference) referencePurge(); else purgeExcessMotions(); }
    void deprecate(LLMotion* motion) { deprecateMotionInstance(motion); }
    void seedBlend(LLMotion* motion) { mPoseBlender.addMotion(motion); }
    void applyBlend() { mPoseBlender.blendAndApply(); }
    void clear() { mPoseBlender.clearBlenders(); deleteAllMotions(); }
    size_t loaded() const { return mLoadedMotions.size(); }
    size_t deprecated() const { return mDeprecatedMotions.size(); }
    std::vector<U32> snapshot()
    {
        std::vector<U32> result;
        auto set = [&](const motion_set_t& motions)
        {
            std::vector<U32> members;
            for (auto* motion : motions) members.push_back(static_cast<Motion*>(motion)->serial);
            std::sort(members.begin(), members.end()); result.push_back(U32(members.size()));
            result.insert(result.end(), members.begin(), members.end());
        };
        set(mLoadedMotions); set(mLoadingMotions); set(mDeprecatedMotions);
        result.push_back(U32(mAllMotions.size()));
        for (const auto& entry : mAllMotions) result.push_back(static_cast<Motion*>(entry.second)->serial);
        result.push_back(U32(mActiveMotions.size()));
        for (auto* motion : mActiveMotions)
        {
            result.push_back(static_cast<Motion*>(motion)->serial);
            result.push_back(isMotionActive(motion)); result.push_back(motion->isStopped());
            result.push_back(bits(motion->getPose()->getWeight()));
        }
        return result;
    }
};
#include "motion_purge_reference.h"

LLUUID id(unsigned i)
{
    LLUUID value("6e78644a-207a-465f-9013-000000000000");
    // Descending, distinct IDs make UUID eviction differ from creation order.
    const unsigned suffix = 1000 - i;
    value.mData[14] = U8(suffix >> 8); value.mData[15] = U8(suffix);
    return value;
}
struct Fixture
{
    struct Seed { Seed() { Clock::set(100.0); } } clockSeed;
    Character character;
    Controller controller;
    std::vector<Motion*> original;
    Fixture(unsigned count, unsigned active)
    {
        controller.setCharacter(&character);
        for (unsigned i = 0; i < count; ++i)
        {
            auto* motion = static_cast<Motion*>(controller.createMotion(id(i)));
            require(motion != nullptr, "Motion creation failed"); original.push_back(motion);
            if (i < active) require(controller.startMotion(id(i), 0.f), "Activation failed");
        }
        for (unsigned i = 0; i < 2; ++i)
        {
            auto* motion = controller.createMotion(id(100 + i));
            require(motion && controller.isMotionLoading(motion), "Missing loading motion");
        }
    }
    void seed()
    {
        character.joint.setPosition(LLVector3(-9.f, -8.f, -7.f));
        if (!original.empty()) controller.seedBlend(original.front());
        character.events.clear();
    }
    ~Fixture() { controller.clear(); }
};
void compare(Fixture& actual, Fixture& reference)
{
    require(actual.controller.snapshot() == reference.controller.snapshot(), "Purge ownership/membership/order changed");
    require(actual.character.events == reference.character.events, "Purge callback/destructor order changed");
    for (unsigned i = 0; i < 3; ++i)
        require(bits(actual.character.joint.getPosition().mV[i]) == bits(reference.character.joint.getPosition().mV[i]),
                "Purge changed retained blender behavior");
}
void runCase(unsigned count, unsigned active, unsigned duplicate)
{
    caseCount = count; caseActive = active; caseDuplicate = duplicate; stage = "setup";
    Fixture actual(count, active), reference(count, active);
    auto prepare = [duplicate](Fixture& f)
    {
        if (duplicate)
        {
            auto* old = f.original.front(); f.controller.deprecate(old);
            // Use the real deprecated owner, optionally with a canonical replacement.
            if (duplicate != 3)
            {
                auto* replacement = f.controller.createMotion(old->getID());
                require(replacement && replacement != old, "Duplicate fixture reused instance");
                if (duplicate == 2) require(f.controller.startMotion(old->getID(), 0.f), "Replacement activation failed");
            }
            require(f.controller.deprecated() == 1, "Missing deprecated owner");
        }
        f.seed();
    };
    prepare(actual); prepare(reference); compare(actual, reference);
    stage = "purge";
    actual.controller.purge(false); reference.controller.purge(true); compare(actual, reference);
    if (!duplicate)
    {
        require(actual.controller.loaded() == (count > 32 ? active : count), "Purge threshold or active retention changed");
        if (count > 32)
        {
            std::vector<std::array<U32, 4>> expected;
            for (unsigned i = count; i > active; --i) expected.push_back({4, i - 1, 0, 0});
            require(actual.character.events == expected, "Inactive motions were not destroyed in UUID order");
        }
    }
    stage = "blend";
    actual.controller.applyBlend(); reference.controller.applyBlend(); compare(actual, reference);
    // Repeated cleanup and both ordinary/hidden entry points must retain the same state.
    stage = "repeated purge";
    actual.controller.purge(false); reference.controller.purge(true); compare(actual, reference);
    for (unsigned mode = 0; mode < 2; ++mode)
    {
        stage = mode ? "minimal update" : "normal update";
        Clock::set(101.125 + mode);
        if (mode) actual.controller.updateMotionsMinimal(); else actual.controller.updateMotions();
        Clock::set(101.125 + mode);
        if (mode) reference.controller.updateMotionsMinimal(); else reference.controller.updateMotions();
        compare(actual, reference);
    }
    stage = "teardown";
    actual.controller.clear(); reference.controller.clear(); compare(actual, reference);
    const auto destroyed = std::count_if(actual.character.events.begin(), actual.character.events.end(),
                                       [](const auto& event) { return event[0] == 4; });
    require(destroyed == actual.character.created, "Teardown did not destroy every initialized motion exactly once");
    ++cases;
}
int run()
{
    LLVector4a::initClass();
    Controller registry;
    for (unsigned i = 0; i < 64; ++i)
        require(registry.registerMotion(id(i), create), "Registry rejected motion");
    for (unsigned i = 0; i < 2; ++i)
        require(registry.registerMotion(id(100 + i), createLoading), "Loading registry rejected motion");
    for (unsigned count : {0u, 1u, 31u, 32u, 33u, 64u})
        for (unsigned active : {0u, count / 2, count}) runCase(count, active, 0);
    for (unsigned count : {32u, 33u, 34u})
        for (unsigned active : {0u, 1u, count})
            for (unsigned duplicate : {1u, 2u, 3u}) runCase(count, active, duplicate);
    std::cout << checks << " purge checks passed across " << cases << " controller lifecycle cases\n";
    return 0;
}
}
int main() { return MotionPurgeTest::run(); }
