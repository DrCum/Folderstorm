#include "linden_common.h"
#include "animation_serialization_fixture.h"
#include "lldatapacker.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#ifndef FS_GENERATE_SERIALIZATION_PACKETS
#define FS_GENERATE_SERIALIZATION_PACKETS 0
#endif

namespace AnimationSerializationTest
{
unsigned checks = 0;
void require(bool value, const char* text)
{
    if (!value) { std::cerr << text << '\n'; std::exit(1); }
    ++checks;
}
std::vector<U8> packet(const Probe& motion)
{
    std::vector<U8> buffer(16384);
    LLDataPackerBinaryBuffer packer(buffer.data(), static_cast<S32>(buffer.size()));
    require(motion.serialize(packer), "Generated fixture could not serialize");
    buffer.resize(packer.getCurrentSize()); return buffer;
}
#if FS_GENERATE_SERIALIZATION_PACKETS
int run()
{
    Character character;
    auto baseline = asset(2); Probe baselineMotion(character, baseline);
    const auto before = snapshot(*baseline);
    baselineMotion.getFileSize();
    require(before != snapshot(*baseline), "Fixture capture must use the unmodified serializer at the pinned baseline");
    std::cout << "// Frozen first-serialize bytes: Folderstorm 556481e3fa14556d65ee911ebdfff1bdca4a818b.\n"
              << "// Generated assets only; no sizing call before the unmodified serializer.\n"
              << "static constexpr const char* SERIALIZATION_PACKETS[] = {\n";
    for (unsigned which = 0; which < 5; ++which)
    {
        Probe motion(character, asset(which));
        std::cout << "    \"";
        for (U8 byte : packet(motion)) std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
        std::cout << "\",\n";
    }
    std::cout << "};\n";
    std::cout << "// Unmodified baseline getFileSize changed source keys: 1\n";
    return 0;
}
#else
#include "animation_serialization_packets.inc"
std::vector<U8> golden(unsigned which)
{
    const std::string hex = SERIALIZATION_PACKETS[which];
    std::vector<U8> bytes;
    for (size_t i = 0; i < hex.size(); i += 2) bytes.push_back(static_cast<U8>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return bytes;
}
std::vector<U32> pose(Probe::Asset& data, F32 time)
{
    std::vector<U32> bits;
    auto append = [&](const F32* value, size_t lanes)
    { for (size_t i = 0; i < lanes; ++i) { U32 raw; std::memcpy(&raw, &value[i], sizeof(raw)); bits.push_back(raw); } };
    for (auto* joint : data.mJointMotionArray)
    {
        LLPointer<LLJointState> state = new LLJointState;
        state->setUsage(ALL); state->setScale(LLVector3(2.f, 3.f, 4.f));
        state->setPosition(LLVector3(101.f, -101.f, .125f)); state->setRotation(LLQuaternion());
        joint->update(state.get(), time, data.mDuration);
        append(state->getScale().mV, 3); append(state->getRotation().mQ, 4); append(state->getPosition().mV, 3);
    }
    return bits;
}
class RejectPacker final : public LLDataPackerBinaryBuffer
{
public:
    RejectPacker(U8* buffer, S32 size): LLDataPackerBinaryBuffer(buffer, size) {}
    bool packU16(const U16, const char*) override { return false; }
};
void preserveSource()
{
    const LLUUID id("f5cf348e-d0ab-4337-b55b-e65b55cdad41");
    Character character;
    for (unsigned which = 0; which < 5; ++which) for (unsigned operation = 0; operation < 3; ++operation)
    {
        auto data = asset(which); Probe motion(character, data);
        LLKeyframeDataCache::addKeyframeData(id, data);
        const auto before = snapshot(*data);
        std::vector<std::vector<U32>> expected;
        for (F32 time : {-1.f, -0.f, .125f, .5f, 1.f, 2.f, 20.f}) expected.push_back(pose(*data, time));
        const auto bytes = golden(which);
        if (operation == 0) require(packet(motion) == bytes, "Wire bytes changed from frozen first serialization");
        if (operation == 1) require(motion.getFileSize() == bytes.size(), "Counting serialization changed packet size");
        if (operation == 2)
        {
            std::array<U8, 16384> buffer{}; RejectPacker rejected(buffer.data(), static_cast<S32>(buffer.size()));
            require(!motion.serialize(rejected), "Rejected pack operation lost its failure return");
        }
        require(before == snapshot(*data), "serialize/getFileSize changed live shared curve data");
        for (unsigned repeat = 0; repeat < 5; ++repeat)
        {
            require(motion.getFileSize() == bytes.size() && packet(motion) == bytes, "Repeated size/export changed wire bytes");
            require(before == snapshot(*data), "Repeated export changed keys or metadata");
        }
        unsigned i = 0;
        for (F32 time : {-1.f, -0.f, .125f, .5f, 1.f, 2.f, 20.f})
            require(pose(*data, time) == expected[i++], "Export changed actual sampled pose bytes");
        require(LLKeyframeDataCache::getKeyframeData(id) == data, "Serialization replaced the canonical cache owner");
        LLKeyframeDataCache::clear();
    }
}
bool exact(const std::vector<Probe::Row>& a, const std::vector<Probe::Row>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].usage != b[i].usage) return false;
        if ((a[i].usage & LLJointState::SCALE) && std::memcmp(a[i].scale.mV, b[i].scale.mV, sizeof(a[i].scale.mV))) return false;
        if ((a[i].usage & LLJointState::ROT) && std::memcmp(a[i].rotation.mQ, b[i].rotation.mQ, sizeof(a[i].rotation.mQ))) return false;
        if ((a[i].usage & LLJointState::POS) && std::memcmp(a[i].position.mV, b[i].position.mV, sizeof(a[i].position.mV))) return false;
    }
    return true;
}
struct Shared
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, start = false, correct = true;
    unsigned samples = 0;
};
void simultaneousReaders()
{
    Character character;
    auto data = asset(2); Probe motion(character, data);
    const auto before = snapshot(*data);
    const auto bytes = golden(2);
    Probe::Result reference;
    require(Probe::sample(request(data, .375f), reference), "Concurrent fixture reference did not sample");
    auto expected = std::make_shared<const std::vector<Probe::Row>>(reference.getJoints());
    reference.takeRequest();
    auto shared = std::make_shared<Shared>();
    std::thread worker([input = request(data, .375f), expected, shared]() mutable
    {
        {
            std::unique_lock lock(shared->mutex); shared->entered = true; shared->changed.notify_all();
            if (!shared->changed.wait_for(lock, std::chrono::seconds(5), [&] { return shared->start; }))
            { shared->correct = false; return; }
        }
        Probe::Result result;
        for (unsigned i = 0; i < 10000; ++i)
        {
            if (!Probe::sample(std::move(input), result) || !exact(result.getJoints(), *expected)) shared->correct = false;
            input = result.takeRequest(); ++shared->samples;
        }
    });
    {
        std::unique_lock lock(shared->mutex);
        require(shared->changed.wait_for(lock, std::chrono::seconds(5), [&] { return shared->entered; }), "Reader failed to reach start gate");
        shared->start = true; shared->changed.notify_all();
    }
    for (unsigned i = 0; i < 256; ++i)
        require(motion.getFileSize() == bytes.size() && packet(motion) == bytes, "Concurrent export changed encoded data");
    worker.join();
    require(shared->correct && shared->samples == 10000, "Concurrent reader observed changed animation data");
    require(before == snapshot(*data), "Concurrent export modified source curves");
}
int run()
{
    preserveSource(); simultaneousReaders();
    std::cout << checks << " read-only export/size/pose checks and 10000 concurrent owned samples passed\n";
    return 0;
}
#endif
}
int main() { return AnimationSerializationTest::run(); }
