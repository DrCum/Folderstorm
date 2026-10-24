#include "linden_common.h"
#include "current_motion_fixture.h"
#include "sampling_test_context.h"
#include "sampling_allocation_probe.h"
#include "llsimdmath.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

namespace CurrentMotionCost
{
using namespace FSCurrentMotion;
using Request = LLKeyframeMotion::SamplingRequest;
using Result = LLKeyframeMotion::SamplingResult;
void require(bool condition) { if (!condition) { std::cerr << "Owner cost fixture invariant failed\n"; std::exit(1); } }
struct Prepared
{
    Request input; Result output;
    void refresh(LLKeyframeMotion& motion)
    {
        require(motion.prepareOwnedSampling(.625f, input));
        require(LLKeyframeMotion::sampleKeyframes(std::move(input), output));
    }
};
int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    std::cout << "mode,joints,cold_prepare_new_calls,warm_prepare_new_calls,direct_apply_new_calls,owned_apply_new_calls,direct_bytes,owned_bytes\n";
    for (unsigned count : {3u, 32u, 128u})
    {
        Fixture f(count); Prepared prepared;
        FSSamplingAlloc::Stats cold, warm, direct, owned;
        { FSSamplingAlloc::Scope scope; require(f.candidate->prepareOwnedSampling(.625f, prepared.input)); cold = FSSamplingAlloc::snapshot(); }
        prepared.refresh(*f.candidate);
        U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
        for (unsigned i = 0; i < 8; ++i)
        {
            bool applied = false; f.reference->onUpdate(.625f, mask);
            f.candidate->updateWithOwnedSampling(.625f, mask, prepared.output, applied); require(applied);
        }
        prepared.input = prepared.output.takeRequest();
        { FSSamplingAlloc::Scope scope;
          for (unsigned i = 0; i < 32; ++i) require(f.candidate->prepareOwnedSampling(.625f, prepared.input));
          warm = FSSamplingAlloc::snapshot(); }
        prepared.refresh(*f.candidate);
        { FSSamplingAlloc::Scope scope;
          for (unsigned i = 0; i < 32; ++i) f.reference->onUpdate(.625f, mask);
          direct = FSSamplingAlloc::snapshot(); }
        { FSSamplingAlloc::Scope scope;
          for (unsigned i = 0; i < 32; ++i)
          { bool applied = false; f.candidate->updateWithOwnedSampling(.625f, mask, prepared.output, applied); require(applied); }
          owned = FSSamplingAlloc::snapshot(); }
        require(warm.calls == 0 && owned.calls == direct.calls && owned.requested == direct.requested);
        require(pose(*f.reference) == pose(*f.candidate));
        std::cout << "alloc," << count << ',' << cold.calls << ',' << warm.calls << ',' << direct.calls << ','
                  << owned.calls << ',' << direct.requested << ',' << owned.requested << '\n';
    }
    return 0;
#else
    return 1;
#endif
}
template<class Function> void measure(const char* stage, unsigned joints, unsigned sample, Function function)
{
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) function();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << stage << ',' << joints << ',' << sample << ",1000," << ns << '\n';
}
int timing()
{
    std::cout << "stage,joints,sample,iterations,elapsed_ns\n";
    for (unsigned count : {3u, 32u, 128u})
    {
        Fixture f(count); Prepared prepared; prepared.refresh(*f.candidate);
        U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
        for (unsigned i = 0; i < 9; ++i)
        {
            auto direct = [&] { measure("direct_update", count, i, [&] { f.reference->onUpdate(.625f, mask); }); };
            auto apply = [&] { measure("owned_apply", count, i, [&]
                { bool applied = false; f.candidate->updateWithOwnedSampling(.625f, mask, prepared.output, applied); require(applied); }); };
            if (i % 2) { apply(); direct(); } else { direct(); apply(); }
            Request reusable; require(f.candidate->prepareOwnedSampling(.625f, reusable));
            measure("owner_prepare", count, i, [&] { require(f.candidate->prepareOwnedSampling(.625f, reusable)); });
            require(pose(*f.reference) == pose(*f.candidate));
        }
    }
    return 0;
}
}
int main(int argc, char** argv)
{
    using namespace CurrentMotionCost;
    if (!FSSamplingTest::supportedContext("current motion cost")) return 0;
    LLVector4a::initClass();
    if (FS_SAMPLING_ALLOCATION_PROBE) return argc == 2 && std::string(argv[1]) == "--allocations" ? allocations() : 0;
    if (argc == 2 && std::string(argv[1]) == "--benchmark") return timing();
    std::cout << "Opt in with --benchmark; allocation probe is a separate executable. No viewer FPS claim.\n";
    return argc == 1 ? 0 : 1;
}
