#include "linden_common.h"
#include "llmotion.h"
#include "llsimdmath.h"
#include "sampling_allocation_probe.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#ifndef FS_ACTIVE_BLENDER_VECTOR
#define FS_ACTIVE_BLENDER_VECTOR 1
#endif

namespace ActiveBlenderCost
{
void require(bool ok) { if (!ok) { std::cerr << "Blender cost fixture failed\n"; std::exit(1); } }
class Motion : public LLNullMotion
{
public:
    explicit Motion(const LLUUID& id): LLNullMotion(id) {}
    void joint(LLJoint& joint)
    {
        LLPointer<LLJointState> state = new LLJointState;
        state->setJoint(&joint); state->setUsage(7); state->setWeight(.5f);
        state->setPosition(LLVector3(1.f, 2.f, 3.f)); addJointState(state);
    }
};
class Blender : public LLPoseBlender
{
public:
    size_t retainedPointerBytes() const
    {
#if FS_ACTIVE_BLENDER_VECTOR
        return mActiveBlenders.capacity() * sizeof(LLJointStateBlender*);
#else
        return 0; // List nodes are transient; its sentinel is counted by the cold probe.
#endif
    }
};
struct Fixture
{
    std::vector<std::unique_ptr<LLJoint>> joints;
    std::vector<std::unique_ptr<Motion>> motions;
    Blender blender;
    Fixture(unsigned count, unsigned overlap)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            auto joint = std::make_unique<LLJoint>(i % LL_CHARACTER_MAX_ANIMATED_JOINTS);
            joint->setName("joint" + std::to_string(i));
            joints.push_back(std::move(joint));
        }
        for (unsigned i = 0; i < overlap; ++i)
        {
            auto motion = std::make_unique<Motion>(LLUUID::null);
            for (auto& joint : joints) motion->joint(*joint);
            motions.push_back(std::move(motion));
        }
        cycle();
    }
    void cycle()
    {
        for (auto& motion : motions) require(blender.addMotion(motion.get()));
        blender.clearBlenders();
    }
};
int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    std::cout << "joints,motions,iterations,new_calls,requested_bytes,live_delta\n";
    for (unsigned count : {3u, 32u, 128u, 256u}) for (unsigned overlap : {1u, 8u})
    {
        Fixture fixture(count, overlap); FSSamplingAlloc::Stats stats;
        { FSSamplingAlloc::Scope probe; for (unsigned i = 0; i < 32; ++i) fixture.cycle(); stats = FSSamplingAlloc::snapshot(); }
        require(stats.live == stats.baseline);
        std::cout << count << ',' << overlap << ",32," << stats.calls << ',' << stats.requested
                  << ',' << stats.live - stats.baseline << '\n';
    }
#endif
    return 0;
}
int timing()
{
    std::cout << "joints,motions,sample,iterations,elapsed_ns\n";
    for (unsigned count : {3u, 32u, 128u, 256u}) for (unsigned overlap : {1u, 8u})
    {
        Fixture fixture(count, overlap);
        for (unsigned sample = 0; sample < 9; ++sample)
        {
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 1000; ++i) fixture.cycle();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            std::cout << count << ',' << overlap << ',' << sample << ",1000," << ns << '\n';
        }
    }
    return 0;
}
int memory()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    std::cout << "joints,motions,cold_calls,cold_requested,cold_live,peak_live,retained_pointer_capacity_bytes,warm_calls,warm_requested,teardown_live_delta\n";
    for (unsigned count : {3u, 32u, 128u, 256u}) for (unsigned overlap : {1u, 8u})
    {
        FSSamplingAlloc::Stats cold, warm, end; size_t capacity = 0;
        { FSSamplingAlloc::Scope probe;
          { Fixture fixture(count, overlap); cold = FSSamplingAlloc::snapshot(); capacity = fixture.blender.retainedPointerBytes();
            for (unsigned i = 0; i < 32; ++i) fixture.cycle(); warm = FSSamplingAlloc::snapshot(); }
          end = FSSamplingAlloc::snapshot(); }
        require(end.live == end.baseline);
        std::cout << count << ',' << overlap << ',' << cold.calls << ',' << cold.requested << ',' << cold.live - cold.baseline
                  << ',' << end.peak - end.baseline << ',' << capacity << ',' << warm.calls - cold.calls
                  << ',' << warm.requested - cold.requested << ',' << end.live - end.baseline << '\n';
    }
#endif
    return 0;
}
}
int main(int argc, char** argv)
{
    LLVector4a::initClass();
    if (argc == 2 && std::string(argv[1]) == "--allocations" && FS_SAMPLING_ALLOCATION_PROBE) return ActiveBlenderCost::allocations();
    if (argc == 2 && std::string(argv[1]) == "--memory" && FS_SAMPLING_ALLOCATION_PROBE) return ActiveBlenderCost::memory();
    if (argc == 2 && std::string(argv[1]) == "--benchmark" && !FS_SAMPLING_ALLOCATION_PROBE) return ActiveBlenderCost::timing();
    return argc == 1 ? 0 : 1;
}
