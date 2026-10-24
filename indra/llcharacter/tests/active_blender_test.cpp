#include "linden_common.h"
#include "active_blender_reference.h"
#include "sampling_allocation_probe.h"
#include "llsimdmath.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>
#ifndef FS_ACTIVE_BLENDER_VECTOR
#define FS_ACTIVE_BLENDER_VECTOR 1
#endif

namespace ActiveBlenderTest
{
unsigned checks = 0, cases = 0;
void require(bool ok, const char* message)
{ if (!ok) { std::cerr << message << " (case " << cases << ")\n"; std::exit(1); } ++checks; }
void scalar(std::vector<U32>& out, F32 value)
{ U32 bits; std::memcpy(&bits, &value, sizeof(bits)); out.push_back(bits); }
void transform(std::vector<U32>& out, LLJoint& joint)
{
    for (F32 v : joint.getPosition().mV) scalar(out, v);
    for (F32 v : joint.getRotation().mQ) scalar(out, v);
    for (F32 v : joint.getScale().mV) scalar(out, v);
}
template<class Base> class Blender : public Base
{
public:
    std::vector<LLJoint*> order()
    {
        std::vector<LLJointStateBlender*> active;
#if FS_ACTIVE_BLENDER_VECTOR
        if constexpr (std::is_base_of_v<LLPoseBlender, Base>)
            active.assign(this->mActiveBlenders.rbegin(), this->mActiveBlenders.rend());
        else
#endif
            active.assign(this->mActiveBlenders.begin(), this->mActiveBlenders.end());
        std::vector<LLJoint*> result;
        for (auto* item : active)
            for (const auto& entry : this->mJointStateBlenderPool)
                if (entry.second == item) result.push_back(entry.first);
        return result;
    }
    LLJoint* cache(LLJoint* key)
    {
        auto found = this->mJointStateBlenderPool.find(key);
        return found == this->mJointStateBlenderPool.end() ? nullptr : &found->second->mJointCache;
    }
    void coldCollection()
    {
        require(this->mActiveBlenders.empty(), "Fault setup requires an empty collection");
#if FS_ACTIVE_BLENDER_VECTOR
        if constexpr (std::is_base_of_v<LLPoseBlender, Base>)
            std::vector<LLJointStateBlender*>().swap(this->mActiveBlenders);
#endif
    }
};
struct State;
struct Record { State* state = nullptr; unsigned id = 0; };
struct State : public LLJointState
{
    Record& record;
    std::vector<unsigned>& deaths;
    State(Record& r, std::vector<unsigned>& d): record(r), deaths(d) { record.state = this; }
    ~State() override { deaths.push_back(record.id); record.state = nullptr; }
};
class Motion : public LLNullMotion
{
public:
    std::vector<LLPointer<State>> states;
    LLJoint::JointPriority priority;
    LLMotionBlendType type;
    Motion(unsigned i): LLNullMotion(LLUUID::null),
        priority(i % 2 ? LLJoint::HIGH_PRIORITY : LLJoint::LOW_PRIORITY),
        type(i % 3 == 2 ? ADDITIVE_BLEND : NORMAL_BLEND) {}
    LLJoint::JointPriority getPriority() override { return priority; }
    LLMotionBlendType getBlendType() override { return type; }
    void state(const LLPointer<State>& value) { addJointState(value); states.push_back(value); }
};
template<class Base> struct World
{
    std::vector<unsigned> deaths;
    std::vector<std::unique_ptr<Record>> records;
    std::vector<std::unique_ptr<LLJoint>> joints;
    std::vector<std::unique_ptr<Motion>> motions;
    std::unique_ptr<Blender<Base>> blender = std::make_unique<Blender<Base>>();
    World(unsigned count, unsigned motionCount, unsigned pattern)
    {
        for (unsigned j = 0; j < count; ++j)
        {
            auto joint = std::make_unique<LLJoint>(j % LL_CHARACTER_MAX_ANIMATED_JOINTS);
            joint->setName("joint" + std::to_string(j));
            joint->setPosition(LLVector3(-.125f * (j + 1), .25f, -.5f));
            if (j) joints[(j - 1) / 2]->addChild(joint.get());
            joints.push_back(std::move(joint));
        }
        for (unsigned i = 0; i < motionCount; ++i)
        {
            auto motion = std::make_unique<Motion>(i);
            for (unsigned j = 0; j < count; ++j)
            {
                if (pattern && (i + j) % 2) continue;
                auto record = std::make_unique<Record>(); record->id = i * count + j + 1;
                LLPointer<State> state = new State(*record, deaths); state->setJoint(joints[j].get());
                state->setUsage(7); const F32 v = F32(i * 3 + j + 1) * .0625f;
                state->setPosition(LLVector3(v, -v, v * 2));
                state->setRotation(LLQuaternion(v, LLVector3::z_axis));
                state->setScale(LLVector3(1 + v, 2 + v, 3 + v));
                state->setPriority((i + j) % 2 ? LLJoint::USE_MOTION_PRIORITY : LLJoint::MEDIUM_PRIORITY);
                state->setUsage((i * 3 + j) % 8); motion->state(state);
                state->setWeight((i + j) % 5 ? .125f * ((i + j) % 4 + 1) : 0.f);
                records.push_back(std::move(record));
            }
            motions.push_back(std::move(motion));
        }
    }
    void add(unsigned i) { require(blender->addMotion(motions.at(i).get()), "Motion admission failed"); }
    void addAll() { for (unsigned i = 0; i < motions.size(); ++i) add(i); }
    std::vector<U32> snapshot()
    {
        std::vector<U32> out;
        auto order = blender->order(); out.push_back(U32(order.size()));
        for (auto* key : order)
        {
            auto found = std::find_if(joints.begin(), joints.end(), [key](const auto& joint) { return joint.get() == key; });
            require(found != joints.end(), "Active pool key was lost"); out.push_back(U32(found - joints.begin()));
        }
        for (auto& joint : joints)
        {
            transform(out, *joint);
            for (F32 v : joint->getWorldPosition().mV) scalar(out, v);
            for (F32 v : joint->getWorldRotation().mQ) scalar(out, v);
            auto* cache = blender->cache(joint.get()); out.push_back(cache != nullptr);
            if (cache) transform(out, *cache);
        }
        for (const auto& record : records)
        { out.push_back(record->state != nullptr); out.push_back(record->state ? record->state->getNumRefs() : 0); }
        out.insert(out.end(), deaths.begin(), deaths.end()); return out;
    }
};
using Actual = World<LLPoseBlender>;
using Reference = World<FSActiveBlenderReference::ReferenceBlender>;
void compare(Actual& a, Reference& b)
{ require(a.snapshot() == b.snapshot(), "Blender order, transforms, cache or state lifetimes differ from frozen parent"); }
void matrix()
{
    for (unsigned joints : {0u, 1u, 3u, 8u, 32u})
        for (unsigned motions : {0u, 1u, 4u, 9u}) for (unsigned pattern : {0u, 1u})
        {
            Actual a(joints, motions, pattern); Reference b(joints, motions, pattern); compare(a, b);
            for (unsigned cycle = 0; cycle < 4; ++cycle)
            {
                for (unsigned i = 0; i < motions; ++i)
                {
                    const unsigned index = cycle % 2 ? motions - i - 1 : i;
                    a.add(index); b.add(index); compare(a, b);
                }
                if (motions) { a.add(0); b.add(0); compare(a, b); }
                a.blender->blendAndCache(cycle % 2); b.blender->blendAndCache(cycle % 2); compare(a, b);
                if (motions) { a.add(0); b.add(0); compare(a, b); }
                for (F32 u : {0.f, .25f, .75f, 1.f})
                { a.blender->interpolate(u); b.blender->interpolate(u); compare(a, b); }
                if (cycle % 2) { a.blender->clearBlenders(); b.blender->clearBlenders(); }
                else { a.blender->blendAndApply(); b.blender->blendAndApply(); }
                compare(a, b);
                a.blender->clearBlenders(); b.blender->clearBlenders();
                a.blender->interpolate(.5f); b.blender->interpolate(.5f); compare(a, b);
            }
            ++cases;
        }
}
void observableOrder()
{
    for (unsigned operation = 0; operation < 4; ++operation)
    {
        Actual a(3, 1, 0); Reference b(3, 1, 0);
        auto prepare = [operation](auto& world)
        {
            for (auto& state : world.motions[0]->states) { state->setUsage(7); state->setWeight(1.f); }
            world.addAll();
            if (operation < 2)
            {
                // Aliased targets expose actual consumer order without production hooks.
                for (auto& state : world.motions[0]->states) state->setJoint(world.joints[2].get());
                if (operation == 1) world.blender->blendAndCache(true);
            }
            else world.motions.clear(); // Only pooled references keep the states alive.
        };
        prepare(a); prepare(b); compare(a, b);
        if (operation == 0) { a.blender->blendAndApply(); b.blender->blendAndApply(); }
        if (operation == 1) { a.blender->interpolate(.5f); b.blender->interpolate(.5f); }
        if (operation == 2) { a.blender->clearBlenders(); b.blender->clearBlenders(); }
        if (operation == 3) { a.blender->blendAndApply(); b.blender->blendAndApply(); }
        compare(a, b);
        if (operation >= 2) require(a.deaths == std::vector<unsigned>({3, 2, 1}), "Actual consumer did not release states in reverse admission order");
        ++cases;
    }
    Actual a(2, 1, 0); Reference b(2, 1, 0);
    auto cacheAlias = [](auto& world)
    {
        auto& states = world.motions[0]->states;
        states[0]->setUsage(LLJointState::POS); states[0]->setWeight(1.f);
        states[1]->setUsage(LLJointState::ROT); states[1]->setWeight(1.f);
        world.addAll(); states[1]->setJoint(world.blender->cache(world.joints[0].get()));
        world.blender->blendAndCache(true);
    };
    cacheAlias(a); cacheAlias(b); compare(a, b);
    require(a.blender->cache(a.joints[1].get())->getPosition() == LLVector3::zero, "Cache reset order changed");
    ++cases;
}
void destruction()
{
    Actual a(8, 9, 0); Reference b(8, 9, 0); a.addAll(); b.addAll();
    a.motions.clear(); b.motions.clear(); compare(a, b);
    a.blender.reset(); b.blender.reset();
    for (const auto& record : a.records) require(!record->state, "Destroyed pool retained a state");
    for (const auto& record : b.records) require(!record->state, "Reference teardown retained a state");
    // Pool destruction follows pointer-key order, which differs between owners.
    auto ad = a.deaths, bd = b.deaths; std::sort(ad.begin(), ad.end()); std::sort(bd.begin(), bd.end());
    require(ad == bd && ad.size() == a.records.size(), "Pool teardown lost or duplicated state destruction"); ++cases;
}
int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    Actual a(3, 1, 0); Reference b(3, 1, 0);
    a.addAll(); b.addAll(); a.blender->clearBlenders(); b.blender->clearBlenders();
    auto fail = [](auto& world)
    {
        world.blender->coldCollection(); bool caught = false;
        { FSSamplingAlloc::Scope probe; FSSamplingAlloc::failureCountdown.store(0);
          try { world.add(0); } catch (const std::bad_alloc&) { caught = true; }
          FSSamplingAlloc::failureCountdown.store(-1); }
        require(caught, "Membership insertion fault was not exercised");
        require(world.blender->order().empty(), "Failed insertion published membership");
        world.addAll();
    };
    fail(a); fail(b); compare(a, b);
    a.blender->clearBlenders(); b.blender->clearBlenders(); compare(a, b);
    std::cout << checks << " collection insertion-failure checks passed\n";
#endif
    return 0;
}
int run()
{
    LLVector4a::initClass(); matrix(); observableOrder(); destruction();
    std::cout << checks << " active-blender checks passed across " << cases << " cases\n";
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--allocations" && FS_SAMPLING_ALLOCATION_PROBE)
    { LLVector4a::initClass(); return ActiveBlenderTest::allocations(); }
    return argc == 1 ? ActiveBlenderTest::run() : 1;
}
