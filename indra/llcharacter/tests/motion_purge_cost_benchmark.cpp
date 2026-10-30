#include "linden_common.h"
#include "keyframe_test_character.h"
#include "sampling_allocation_probe.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#ifndef FS_PURGE_PARENT_CONTROL
#define FS_PURGE_PARENT_CONTROL 0
#endif

namespace MotionPurgeCost
{
void require(bool condition) { if (!condition) { std::cerr << "Purge fixture invariant failed\n"; std::exit(1); } }
class Controller : public LLMotionController
{
public:
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void purge() { purgeExcessMotions(); }
    size_t loaded() const { return mLoadedMotions.size(); }
};
LLMotion* create(const LLUUID& id) { return new LLNullMotion(id); }
struct Fixture
{
    FSKeyframeTests::Character character;
    Controller controller;
    explicit Fixture(unsigned count)
    {
        controller.setCharacter(&character);
        for (unsigned i = 0; i < count; ++i)
        {
            LLUUID id; id.generate(); require(controller.registerMotion(id, create));
            require(controller.createMotion(id) != nullptr);
        }
        require(controller.loaded() == count);
    }
};
int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    std::cout << "loaded,iterations,new_calls,requested_bytes,live_delta\n";
    for (unsigned count : {0u, 1u, 32u})
    {
        Fixture fixture(count); FSSamplingAlloc::Stats stats;
        { FSSamplingAlloc::Scope probe;
          for (unsigned i = 0; i < 32; ++i) fixture.controller.purge();
          stats = FSSamplingAlloc::snapshot(); }
        require(fixture.controller.loaded() == count && stats.live == stats.baseline);
#if !FS_PURGE_PARENT_CONTROL
        require(stats.calls == 0 && stats.requested == 0);
#endif
        std::cout << count << ",32," << stats.calls << ',' << stats.requested << ',' << stats.live - stats.baseline << '\n';
    }
#endif
    return 0;
}
int timing()
{
    std::cout << "loaded,sample,iterations,elapsed_ns\n";
    for (unsigned count : {0u, 1u, 32u})
    {
        Fixture fixture(count);
        for (unsigned sample = 0; sample < 9; ++sample)
        {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 100000; ++i) fixture.controller.purge();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            require(fixture.controller.loaded() == count);
            std::cout << count << ',' << sample << ",100000," << ns << '\n';
        }
    }
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--allocations" && FS_SAMPLING_ALLOCATION_PROBE) return MotionPurgeCost::allocations();
    if (argc == 2 && std::string(argv[1]) == "--benchmark" && !FS_SAMPLING_ALLOCATION_PROBE) return MotionPurgeCost::timing();
    std::cout << "Purge component probes are opt-in; keep allocations and timings in separate executables.\n";
    return argc == 1 ? 0 : 1;
}
