#include "linden_common.h"
#include "keyframe_test_character.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <vector>

namespace InitialMotionMaskTest
{
unsigned checks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
class Character final : public FSKeyframeTests::Character
{
public:
    LLJoint joint{0};
    Character() { joint.setName("maskFixture"); }
    LLJoint* getRootJoint() override { return &joint; }
};
std::vector<unsigned> order;
class Capture final : public LLNullMotion
{
public:
    unsigned label, calls = 0;
    LLJoint::JointPriority priority;
    std::array<U8, LL_CHARACTER_MAX_ANIMATED_JOINTS> mask;
    Capture(const LLUUID& id, unsigned which, LLJoint::JointPriority rank): LLNullMotion(id), label(which), priority(rank) {}
    LLJoint::JointPriority getPriority() override { return priority; }
    bool canDeprecate() override { return false; }
    LLMotionInitStatus onInitialize(LLCharacter* character) override
    {
        LLPointer<LLJointState> state = new LLJointState;
        state->setJoint(character->getRootJoint()); state->setUsage(LLJointState::ROT);
        state->setRotation(LLQuaternion()); state->setPriority(priority); addJointState(state);
        return STATUS_SUCCESS;
    }
    bool onUpdate(F32, U8* input) override
    {
        std::copy_n(input, mask.size(), mask.begin()); ++calls; order.push_back(label); return true;
    }
};
LLMotion* older(const LLUUID& id) { return new Capture(id, 1, LLJoint::HIGH_PRIORITY); }
LLMotion* newer(const LLUUID& id) { return new Capture(id, 2, LLJoint::MEDIUM_PRIORITY); }
void zero(const Capture& motion)
{
    if (!std::all_of(motion.mask.begin(), motion.mask.end(), [](U8 v) { return v == 0; }))
        std::cerr << "First/cleared activation mask contains byte " << unsigned(motion.mask[0]) << '\n';
    require(std::all_of(motion.mask.begin(), motion.mask.end(), [](U8 v) { return v == 0; }),
            "Activation read a nonzero initial mask");
}
int run()
{
    const LLUUID first("fe000000-0000-0000-0000-0000000014d0"), second("fe000000-0000-0000-0000-0000000014d1");
    // Register once; the real registry and controller create every test motion.
    Character registrar;
    require(registrar.registerMotion(first, older) && registrar.registerMotion(second, newer), "Factory registration failed");
    for (unsigned poison : {0u, 1u, 0x55u, 0xaau, 0xffu})
        for (unsigned initial_update = 0; initial_update < 3; ++initial_update)
        {
            Character character;
            alignas(LLMotionController) unsigned char storage[sizeof(LLMotionController)];
            std::memset(storage, poison, sizeof(storage));
            auto* controller = new (storage) LLMotionController;
            controller->setCharacter(&character);
            if (initial_update == 1) controller->updateMotions();
            if (initial_update == 2) controller->updateMotionsMinimal();
            order.clear();
            require(controller->startMotion(first, 0.f), "First activation failed");
            auto* high = static_cast<Capture*>(controller->findMotion(first));
            require(high && high->calls == 1 && order == std::vector<unsigned>{1}, "Activation callback changed");
            zero(*high); // Fails on the original constructor with nonzero poisoned storage.
            require(controller->startMotion(second, 0.f), "Second activation failed");
            auto* medium = static_cast<Capture*>(controller->findMotion(second)); zero(*medium);
            require(order == std::vector<unsigned>({1, 2}), "Activation order changed");

            controller->updateMotions(); // Establish full pose weights using the existing update path.
            order.clear(); controller->updateMotions();
            require(order == std::vector<unsigned>({2, 1}), "Chronological update order changed");
            zero(*medium);
            require(high->mask[0] == (0xff >> (7 - LLJoint::MEDIUM_PRIORITY)), "Active higher motion's mask was lost");
            require(std::all_of(high->mask.begin() + 1, high->mask.end(), [](U8 v) { return v == 0; }),
                    "Unanimated joints acquired mask bits");
            require(controller->startMotion(first, 0.f), "Same-frame reactivation failed");
            require(high->mask[0] == (0xff >> (7 - LLJoint::HIGH_PRIORITY)),
                    "Reactivation cleared the existing current-frame mask");
            const unsigned calls = high->calls + medium->calls;
            controller->updateMotionsMinimal();
            require(calls == high->calls + medium->calls, "Hidden update unexpectedly sampled motions");
            require(controller->stopMotionLocally(first, true) && controller->stopMotionLocally(second, true), "Immediate stop failed");
            controller->updateMotionsMinimal();
            require(controller->startMotion(first, 0.f), "Reactivation failed"); zero(*high);
            controller->~LLMotionController();
        }
    std::cout << checks << " real-controller initial/current-mask checks passed across 15 poisoned-storage cases\n";
    return 0;
}
}
int main() { return InitialMotionMaskTest::run(); }
