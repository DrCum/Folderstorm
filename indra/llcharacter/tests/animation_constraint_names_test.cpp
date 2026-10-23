#include "linden_common.h"
#include "animation_serialization_fixture.h"
#include "lldatapacker.h"
#include <array>
#include <cstdlib>
#include <iostream>

namespace AnimationConstraintNamesTest
{
using AnimationSerializationTest::Probe;
unsigned checks = 0;
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
const std::array<std::string, 7> names = {"0123456789abcde", "mPelvis", "", "0123456789abcdef",
                                        "long_constraint_volume_name_for_truncation", "Z", "abcdefghijklmno"};
class Character final : public FSKeyframeTests::Character
{
public:
    Character() { for (size_t i = 0; i < names.size(); ++i) mVolumes[i].setName(names[i]); }
    unsigned source = 0, target = 0;
    LLJoint* findCollisionVolume(S32 id) override { return &mVolumes[id == 0 ? source : target]; }
private:
    std::array<LLJoint, names.size()> mVolumes;
};
struct Field
{
    std::array<U8, 16> bytes{};
    S32 offset = 0;
    bool written = false;
};
class Packer final : public LLDataPackerBinaryBuffer
{
public:
    Packer(U8* buffer, S32 size): LLDataPackerBinaryBuffer(buffer, size) {}
    std::array<Field, 2> fields;
    unsigned count = 0;
    bool reject = false;
    void begin(bool failure) { reset(); count = 0; reject = failure; }
    bool packBinaryDataFixed(const U8* value, S32 size, const char* name) override
    {
        require(count < 2 && size == 16, "Constraint-name field count/width changed");
        require(std::string(name) == (count ? "target_volume" : "source_volume"), "Constraint-name order changed");
        auto& field = fields[count++]; field.offset = getCurrentSize();
        std::memcpy(field.bytes.data(), value, 16);
        field.written = !(reject && count == 1) && LLDataPackerBinaryBuffer::packBinaryDataFixed(value, size, name);
        return field.written;
    }
};
std::array<U8, 16> expected(const std::string& name)
{
    std::array<U8, 16> result{};
    std::memcpy(result.data(), name.data(), std::min<size_t>(15, name.size()));
    return result;
}
void checkField(const Field& field, const std::string& name, const U8* buffer)
{
    const auto wanted = expected(name);
    require(field.bytes == wanted, "Constraint-name terminator/padding contains nonzero trailing bytes");
    if (field.written)
    {
        std::array<U8, 16> decoded{};
        LLDataPackerBinaryBuffer reader(const_cast<U8*>(buffer) + field.offset, 16);
        require(reader.unpackBinaryDataFixed(decoded.data(), 16, "name") && decoded == wanted,
                "Actual packed name bytes changed");
        require(std::string(reinterpret_cast<const char*>(decoded.data())) == name.substr(0, 15),
                "Decoded/truncated constraint name changed");
    }
}
int run()
{
    Character character;
    auto data = AnimationSerializationTest::asset(3);
    auto* constraint = data->mConstraints.front(); constraint->mTargetConstraintVolume = 1;
    Probe motion(character, data);
    const auto before = AnimationSerializationTest::snapshot(*data);
    std::array<U8, 16384> buffer{};
    Packer packer(buffer.data(), static_cast<S32>(buffer.size()));
    std::vector<U8> first;
    for (unsigned source = 0; source < names.size(); ++source)
        for (unsigned target = 0; target < names.size(); ++target)
            for (bool ground : {false, true}) for (bool reject : {false, true})
            {
                character.source = source; character.target = target;
                constraint->mConstraintTargetType = ground ? CONSTRAINT_TARGET_TYPE_GROUND : CONSTRAINT_TARGET_TYPE_BODY;
                const auto sourceName = names[source], targetName = ground ? std::string("GROUND") : names[target];
                for (unsigned repeat = 0; repeat < 4; ++repeat)
                {
                    packer.begin(reject);
                    require(motion.serialize(packer) == !reject, "Pack failure/success result changed");
                    require(packer.count == 2 || (reject && packer.count == 1), "Missing source/target constraint field");
                    checkField(packer.fields[0], sourceName, buffer.data());
                    if (packer.count == 2) checkField(packer.fields[1], targetName, buffer.data());
                    if (!reject)
                    {
                        require(packer.fields[1].offset == packer.fields[0].offset + 16 + 12, "Constraint field layout changed");
                        std::vector<U8> packet(buffer.begin(), buffer.begin() + packer.getCurrentSize());
                        if (!repeat) first = packet;
                        else require(packet == first, "Repeated constraint serialization is not deterministic");
                        require(motion.getFileSize() == packet.size(), "Constraint packet size changed");
                    }
                    require(before == AnimationSerializationTest::snapshot(*data), "Constraint export modified live curve data");
                    require(constraint->mConstraintTargetType == (ground ? CONSTRAINT_TARGET_TYPE_GROUND : CONSTRAINT_TARGET_TYPE_BODY),
                            "Constraint export changed its target kind");
                }
            }
    std::cout << checks << " constraint-name padding, decoding, layout and failure checks passed\n";
    return 0;
}
}
int main() { return AnimationConstraintNamesTest::run(); }
