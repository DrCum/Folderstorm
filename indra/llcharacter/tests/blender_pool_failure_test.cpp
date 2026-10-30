#include "linden_common.h"
#include "llmotion.h"
#include "llsimdmath.h"
#include "sampling_allocation_probe.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#ifndef FS_EXPECT_SAFE_BLENDER_POOL
#define FS_EXPECT_SAFE_BLENDER_POOL 1
#endif

namespace BlenderPoolFailure
{
unsigned checks = 0;
void require(bool ok) { if (!ok) { std::cerr << "Pool insertion ownership invariant failed\n"; std::exit(1); } ++checks; }
class Motion : public LLNullMotion
{
public:
    Motion(): LLNullMotion(LLUUID::null) {}
    void state(const LLPointer<LLJointState>& state) { addJointState(state); }
};
class Blender : public LLPoseBlender
{
public:
    size_t pooled() const { return mJointStateBlenderPool.size(); }
    size_t active() const { return mActiveBlenders.size(); }
    bool validPool() const
    { for (const auto& entry : mJointStateBlenderPool) if (!entry.second) return false; return true; }
    LLJointStateBlender* find(LLJoint* joint) const
    { auto found = mJointStateBlenderPool.find(joint); return found == mJointStateBlenderPool.end() ? nullptr : found->second; }
};
void reentry()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    // The hook must run during construction, before the pool's insertion starts.
    { auto warm = std::make_unique<LLJointStateBlender>(); }
    FSSamplingAlloc::Stats constructor;
    { FSSamplingAlloc::Scope probe;
      { auto candidate = std::make_unique<LLJointStateBlender>(); }
      constructor = FSSamplingAlloc::snapshot(); }
    require(constructor.live == constructor.baseline);
    if (!constructor.calls)
    {
        std::cout << "SKIP allocation reentry: joint-cache construction does not allocate through ordinary new\n";
        return;
    }
    LLJoint joint(0); joint.setName("joint");
    LLPointer<LLJointState> first = new LLJointState, second = new LLJointState;
    for (auto* state : {first.get(),second.get()})
    { state->setJoint(&joint); state->setUsage(7); state->setScale(LLVector3(1.f,1.f,1.f)); }
    first->setPosition(LLVector3(1.f,2.f,3.f)); second->setPosition(LLVector3(4.f,5.f,6.f));
    Motion outer, inner; outer.state(first); inner.state(second);
    first->setWeight(.75f); second->setWeight(.25f);
    const auto firstRefs = first->getNumRefs(), secondRefs = second->getNumRefs();
    auto blender = std::make_unique<Blender>();
    struct Context { Blender* blender; Motion* motion; LLJoint* joint; LLJointStateBlender* canonical = nullptr; bool called = false; };
    Context context{blender.get(),&inner,&joint}; FSSamplingAlloc::Stats stats;
    bool same = false, pose = false; size_t pooled = 0, active = 0;
    { FSSamplingAlloc::Scope probe;
      FSSamplingAlloc::allocationContext.store(&context);
      FSSamplingAlloc::beforeAllocation.store(+[](void* raw)
      {
          auto& c = *static_cast<Context*>(raw); c.called = true;
          require(c.blender->addMotion(c.motion)); c.canonical = c.blender->find(c.joint);
      });
      require(blender->addMotion(&outer));
      same = context.canonical == blender->find(&joint); pooled = blender->pooled(); active = blender->active();
      blender->blendAndApply(); pose = joint.getPosition() == LLVector3(1.75f,2.75f,3.75f);
      blender.reset(); stats = FSSamplingAlloc::snapshot(); }
    const bool refs = first->getNumRefs() == firstRefs && second->getNumRefs() == secondRefs;
    require(context.called && context.canonical && refs && pooled == 1 && stats.live >= stats.baseline);
#if FS_EXPECT_SAFE_BLENDER_POOL
    require(same && active == 1 && pose && stats.live == stats.baseline);
#endif
    std::cout << "kind,canonical_preserved,pooled,active,live_delta,refs_restored,pose_preserved\n"
              << "reentry," << same << ',' << pooled << ',' << active << ',' << stats.live - stats.baseline
              << ',' << refs << ',' << pose << '\n';
#endif
}
int run()
{
#if FS_SAMPLING_ALLOCATION_PROBE
#if defined(TRACY_ENABLE)
    std::cout << "SKIP global allocation failure injection with Tracy enabled; run this target with Tracy OFF\n";
    return 0;
#endif
    LLVector4a::initClass(); unsigned failures = 0, leaked = 0;
    std::cout << "fail_after,threw,initial_pooled,initial_active,retried,new_calls,requested_bytes,live_delta,state_refs_restored\n";
    for (int fail = 0; fail < 8; ++fail)
    {
        LLJoint joint(0); joint.setName("joint");
        LLPointer<LLJointState> state = new LLJointState; state->setJoint(&joint); state->setUsage(7);
        state->setPosition(LLVector3(1.f,2.f,3.f)); state->setScale(LLVector3(1.f,1.f,1.f));
        Motion motion; motion.state(state); const auto refs = state->getNumRefs();
        state->setWeight(1.f);
        auto blender = std::make_unique<Blender>();
        bool caught = false; size_t pooled = 0, active = 0; FSSamplingAlloc::Stats stats;
        { FSSamplingAlloc::Scope probe; FSSamplingAlloc::failureCountdown.store(fail);
          try { require(blender->addMotion(&motion)); } catch (const std::bad_alloc&) { caught = true; }
          FSSamplingAlloc::failureCountdown.store(-1);
          pooled = blender->pooled(); active = blender->active(); require(blender->validPool());
          if (caught) require(blender->addMotion(&motion));
          require(blender->pooled() == 1 && blender->active() == 1 && blender->validPool());
          blender->blendAndApply(); require(joint.getPosition() == LLVector3(1.f,2.f,3.f));
          require(state->getNumRefs() == refs && blender->active() == 0);
          require(blender->addMotion(&motion)); blender->clearBlenders();
          require(blender->addMotion(&motion)); require(blender->active() == 1 && blender->pooled() == 1);
          blender.reset(); stats = FSSamplingAlloc::snapshot(); }
        require(state->getNumRefs() == refs && stats.live >= stats.baseline);
        if (caught) ++failures; if (stats.live != stats.baseline) ++leaked;
        std::cout << fail << ',' << caught << ',' << pooled << ',' << active << ',' << caught << ',' << stats.calls << ','
                  << stats.requested << ',' << stats.live - stats.baseline << ',' << (state->getNumRefs() == refs) << '\n';
    }
    require(failures > 0);
#if FS_EXPECT_SAFE_BLENDER_POOL
    require(leaked == 0);
#endif
    reentry();
    std::cout << checks << " checks, " << failures << " cold allocation failures, " << leaked << " retained-allocation cases\n";
#endif
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--allocations" && FS_SAMPLING_ALLOCATION_PROBE) return BlenderPoolFailure::run();
    std::cout << "Cold pool failure accounting requires separate allocation instrumentation and --allocations.\n";
    return argc == 1 ? 0 : 1;
}
