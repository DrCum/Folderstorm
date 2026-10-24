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
void require(bool ok) { if (!ok) { std::cerr << "Pool insertion ownership invariant failed\n"; std::exit(1); } }
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
};
int run()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    LLVector4a::initClass(); unsigned failures = 0, leaked = 0;
    std::cout << "fail_after,threw,pooled,active,new_calls,requested_bytes,live_delta,state_refs_restored\n";
    for (int fail = 0; fail < 8; ++fail)
    {
        LLJoint joint(0); joint.setName("joint");
        LLPointer<LLJointState> state = new LLJointState; state->setJoint(&joint); state->setUsage(7);
        Motion motion; motion.state(state); const auto refs = state->getNumRefs();
        auto blender = std::make_unique<Blender>();
        bool caught = false; size_t pooled = 0, active = 0; FSSamplingAlloc::Stats stats;
        { FSSamplingAlloc::Scope probe; FSSamplingAlloc::failureCountdown.store(fail);
          try { require(blender->addMotion(&motion)); } catch (const std::bad_alloc&) { caught = true; }
          FSSamplingAlloc::failureCountdown.store(-1);
          pooled = blender->pooled(); active = blender->active(); blender.reset(); stats = FSSamplingAlloc::snapshot(); }
        require(state->getNumRefs() == refs && stats.live >= stats.baseline);
        if (caught) ++failures; if (stats.live != stats.baseline) ++leaked;
        std::cout << fail << ',' << caught << ',' << pooled << ',' << active << ',' << stats.calls << ','
                  << stats.requested << ',' << stats.live - stats.baseline << ',' << (state->getNumRefs() == refs) << '\n';
    }
    require(failures > 0);
#if FS_EXPECT_SAFE_BLENDER_POOL
    require(leaked == 0);
#endif
    std::cout << failures << " cold allocation failures, " << leaked << " retained-allocation cases\n";
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
