#include "linden_common.h"
#include "current_motion_fixture.h"
#include "llsimdmath.h"
#include <cstdlib>
#include <iostream>

namespace ActiveBlenderController
{
using namespace FSCurrentMotion;
unsigned checks = 0;
void require(bool ok) { if (!ok) { std::cerr << "Blender controller invariant failed\n"; std::exit(1); } ++checks; }
LLUUID idleId(unsigned i)
{ LLUUID id("34c9f0ab-4dc1-4b38-8877-000000000000"); id.mData[15] = U8(i + 1); return id; }
struct Scene
{
    struct Seed { Seed() { Clock::set(100.0); } } seed;
    Character character;
    std::vector<LLUUID> ids;
    explicit Scene(unsigned count): character(count)
    {
        auto& controller = character.getMotionController(); controller.updateMotions();
        for (unsigned i = 0; i < 6; ++i)
        {
            LLUUID id; id.generate(); ids.push_back(id); auto data = asset(count);
            data->mBasePriority = data->mMaxPriority = i % 2 ? LLJoint::HIGH_PRIORITY : LLJoint::LOW_PRIORITY;
            LLKeyframeDataCache::addKeyframeData(id, data); require(controller.startMotion(id, 0.f));
        }
    }
    void update(unsigned frame)
    {
        auto& c = character.getMotionController(); Clock::set(100.0 + (frame + 1) / 64.0);
        if (frame == 8) c.setTimeStep(.125f);
        if (frame == 24) c.pauseAllMotions();
        if (frame == 32) c.unpauseAllMotions();
        if (frame == 44) { c.clearBlenders(); c.setTimeStep(0.f); }
        if (frame == 48) require(c.stopMotionLocally(ids[0], true));
        if (frame == 50) require(c.startMotion(ids[0], 0.f));
        if (frame == 52) c.deactivateAllMotions();
        if (frame == 54) { require(c.startMotion(ids[0], 0.f)); require(c.startMotion(ids[1], 0.f)); }
        if (frame == 58) c.setTimeStep(.25f);
        if (frame == 64) c.setTimeStep(0.f);
        if (frame == 66) for (unsigned i = 0; i < 34; ++i) require(c.createMotion(idleId(i)) != nullptr);
        if (frame >= 36 && frame < 44) c.updateMotionsMinimal(); else c.updateMotions(frame % 3 == 0);
    }
    std::vector<U32> snapshot()
    {
        auto& c = character.getMotionController(); std::vector<U32> result;
        auto scalar = [&](F32 v) { U32 bits; std::memcpy(&bits, &v, sizeof(bits)); result.push_back(bits); };
        result.push_back(c.isPaused()); result.push_back(U32(c.getActiveMotions().size())); scalar(c.getAnimTime());
        for (const auto& id : ids)
        {
            auto* motion = static_cast<LLKeyframeMotion*>(character.findMotion(id)); result.push_back(motion != nullptr);
            if (motion)
            {
                auto state = pose(*motion); result.insert(result.end(), state.begin(), state.end());
                scalar(motion->getPose()->getWeight()); scalar(motion->getFadeWeight()); result.push_back(motion->isStopped());
            }
        }
        for (auto& joint : character.joints)
        {
            require(joint->getPosition().isFinite() && joint->getScale().isFinite());
            for (F32 v : joint->getPosition().mV) scalar(v);
            for (F32 v : joint->getRotation().mQ) scalar(v);
            for (F32 v : joint->getScale().mV) scalar(v);
            for (F32 v : joint->getWorldPosition().mV) scalar(v);
            for (F32 v : joint->getWorldRotation().mQ) scalar(v);
        }
        return result;
    }
};
int run(bool trace)
{
    LLVector4a::initClass(); LLMotionController registry;
    for (unsigned i = 0; i < 34; ++i) require(registry.registerMotion(idleId(i), LLNullMotion::create));
    for (unsigned count : {3u, 16u, 32u})
    {
        { Scene scene(count);
          for (unsigned frame = 0; frame < 72; ++frame)
          {
              scene.update(frame); const auto state = scene.snapshot(); require(!state.empty());
              if (trace) { std::cout << "frame," << count << ',' << frame; for (U32 word : state) std::cout << ',' << word; std::cout << '\n'; }
          } }
        LLKeyframeDataCache::clear();
    }
    std::cout << checks << " controller path checks across 216 frames\n";
    std::cout << "sizes," << sizeof(LLJointStateBlender) << ',' << sizeof(LLPoseBlender) << ',' << sizeof(LLMotionController) << '\n';
    return 0;
}
}
int main(int argc, char** argv)
{ return argc == 1 || (argc == 2 && std::string(argv[1]) == "--trace") ? ActiveBlenderController::run(argc == 2) : 1; }
