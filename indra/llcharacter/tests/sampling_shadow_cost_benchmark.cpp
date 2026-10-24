#include "linden_common.h"
#include "current_motion_fixture.h"
#include "fssamplingshadow.h"
#include "llsimdmath.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#ifndef FS_SHADOW_PARENT_CONTROL
#define FS_SHADOW_PARENT_CONTROL 0
#endif

namespace SamplingShadowCost
{
using namespace FSCurrentMotion;
void require(bool ok) { if (!ok) { std::cerr << "Shadow cost fixture invariant failed\n"; std::exit(1); } }
struct Seed { Seed() { Clock::set(100.0); } };
struct Runner
{
    Seed seed;
    Character character;
    LLUUID id;
    explicit Runner(unsigned count): character(count)
    {
        id.generate(); LLKeyframeDataCache::addKeyframeData(id, asset(count));
        auto& controller = character.getMotionController(); controller.updateMotions();
        require(controller.startMotion(id, 0.f)); Clock::set(100.125);
        for (unsigned i = 0; i < 8; ++i) controller.updateMotions();
    }
    LLKeyframeMotion& motion() { return *static_cast<LLKeyframeMotion*>(character.findMotion(id)); }
    void update(bool shadow)
    {
#if !FS_SHADOW_PARENT_CONTROL
        if (shadow)
        {
            FSSampling::ShadowScope scope(character.getMotionController(), true);
            character.getMotionController().updateMotions();
            return;
        }
#endif
        character.getMotionController().updateMotions();
    }
};
void measure(Runner& runner, bool shadow, unsigned count, unsigned sample)
{
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000; ++i) runner.update(shadow);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << (shadow ? "shadow" : "direct") << ',' << count << ',' << sample << ",1000," << elapsed << '\n';
}
int run()
{
    LLVector4a::initClass();
    std::cout << "stage,joints,sample,iterations,elapsed_ns\n";
    for (unsigned count : {3u, 32u, 128u})
    {
        Runner direct(count), observed(count);
        for (unsigned sample = 0; sample < 9; ++sample)
        {
#if FS_SHADOW_PARENT_CONTROL
            measure(direct, false, count, sample);
#else
            if (sample % 2) { measure(observed, true, count, sample); measure(direct, false, count, sample); }
            else { measure(direct, false, count, sample); measure(observed, true, count, sample); }
            require(pose(direct.motion()) == pose(observed.motion()));
#endif
        }
        LLKeyframeDataCache::clear();
    }
#if !FS_SHADOW_PARENT_CONTROL
    const auto totals = FSSampling::ShadowScope::totals();
    require(totals.predicted == 27000 && totals.matched == totals.predicted &&
            !totals.mismatched && !totals.unused && totals.firstMatched == totals.matched);
#endif
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--benchmark")
    {
        if (!FSSampling::NumericContext::capture().isSupported())
        {
            std::cout << "SKIP shadow cost: owned numeric context unsupported; direct playback remains available\n";
            return 0;
        }
        return SamplingShadowCost::run();
    }
    std::cout << "Opt in with --benchmark; generated controller costs only, no viewer FPS/overlap claim.\n";
    return argc == 1 ? 0 : 1;
}
