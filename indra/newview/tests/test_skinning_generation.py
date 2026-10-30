"""Generate production skinning tests and opt-in native SIMD benchmarks."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]

LEGACY = r"""
void legacy(LLMatrix4a* mat, S32 count, const LLMeshSkinInfo* skin, LLVOAvatar* avatar)
{
    initJointNums(const_cast<LLMeshSkinInfo*>(skin), avatar);
    if (skin->mInvBindMatrix.size() < count) return;
    LLMatrix4a world[LL_CHARACTER_MAX_ANIMATED_JOINTS];
    for (S32 j = 0; j < count; ++j)
    {
        LLJoint* joint = avatar->getJoint(skin->mJointNums[j]);
        if (joint) world[j] = joint->getWorldMatrix4a();
        else
        {
            mat[j] = skin->mInvBindMatrix[j];
            dump_avatar_and_skin_state("initSkinningMatrixPalette joint not found", avatar, skin);
        }
    }
    const LLMatrix4a* invBind = &(skin->mInvBindMatrix[0]);
    const LLMatrix4a* w = world;
    LLMatrix4a* m = mat;
    LLMatrix4a* end = m + count;
    while (m < end) matMulUnsafe(*(invBind++), *(w++), *(m++));
}
"""

BENCHMARK = r"""
#include "linden_common.h"
#include "llmath.h"
#include "llmatrix4a.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#define FS_NOINLINE __declspec(noinline)
#else
#define FS_NOINLINE __attribute__((noinline))
#endif
#undef LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR
#define DEBUG_SKINNING 0
namespace SkinningBenchmark
{
constexpr int LL_CHARACTER_MAX_ANIMATED_JOINTS = @JOINT_CAP@;
struct LLJoint
{
    LLMatrix4a world;
    const LLMatrix4a& getWorldMatrix4a() const { return world; }
};
struct LLMeshSkinInfo
{
    std::vector<LLMatrix4a> mInvBindMatrix;
    std::vector<S32> mJointNums;
};
struct LLVOAvatar
{
    std::vector<LLJoint> joints;
    LLJoint* getJoint(S32 id) { return id >= 0 && id < static_cast<S32>(joints.size()) ? &joints[id] : nullptr; }
};
void initJointNums(LLMeshSkinInfo*, LLVOAvatar*) {}
void dump_avatar_and_skin_state(const std::string&, LLVOAvatar*, const LLMeshSkinInfo*) {}
FS_NOINLINE @LEGACY@
FS_NOINLINE @PRODUCTION@
using Kernel = void (*)(LLMatrix4a*, S32, const LLMeshSkinInfo*, LLVOAvatar*);
volatile std::uint64_t observed = 0;
FS_NOINLINE double timed(Kernel kernel, std::array<LLMeshSkinInfo, 8>& skins,
                        std::array<LLVOAvatar, 8>& avatars, std::vector<LLMatrix4a>& output,
                        S32 count, std::size_t iterations)
{
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        const auto batch = i & 7;
        kernel(output.data() + batch * count, count, &skins[batch], &avatars[batch]);
        std::atomic_signal_fence(std::memory_order_acq_rel);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::uint64_t checksum = 0;
    for (const auto& matrix : output)
        for (unsigned i = 0; i < 16; ++i)
        {
            std::uint32_t bits; std::memcpy(&bits, matrix.getF32ptr() + i, 4); checksum += bits;
        }
    observed = checksum;
    return std::chrono::duration<double, std::nano>(elapsed).count() /
           static_cast<double>(iterations * count);
}
int run(int argc, char** argv)
{
    if (argc != 2 || std::string(argv[1]) != "--benchmark")
    {
        std::cout << "Use --benchmark explicitly; this times actual SIMD math with typed lookup boundaries.\n";
        return argc == 1 ? 0 : 2;
    }
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int id[4]; char brand[49] = {};
    __cpuid(id, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(id[0]) >= 0x80000004u)
        for (unsigned i = 0; i < 3; ++i)
        {
            __cpuid(id, static_cast<int>(0x80000002u + i)); std::memcpy(brand + i * 16, id, 16);
        }
    std::cout << brand << "; MSVC " << _MSC_FULL_VER << '\n';
#endif
    std::cout << "count,legacy_median_ns_per_matrix,production_median_ns_per_matrix\n";
    std::mt19937 random(0x46534745);
    for (S32 count : {1,4,15,32,64,110,LL_CHARACTER_MAX_ANIMATED_JOINTS})
    {
        std::array<LLMeshSkinInfo, 8> skins;
        std::array<LLVOAvatar, 8> avatars;
        std::vector<LLMatrix4a> output(static_cast<std::size_t>(count) * 8), expected(output.size());
        for (unsigned batch = 0; batch < 8; ++batch)
        {
            auto& skin = skins[batch]; auto& avatar = avatars[batch];
            skin.mInvBindMatrix.resize(count); avatar.joints.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS);
            for (S32 i = 0; i < count; ++i) skin.mJointNums.push_back((i * 7) % LL_CHARACTER_MAX_ANIMATED_JOINTS);
            for (auto& matrix : skin.mInvBindMatrix)
                for (unsigned i = 0; i < 16; ++i)
                    matrix.getF32ptr()[i] = static_cast<float>(static_cast<int>(random() % 2049) - 1024) / 128.f;
            for (auto& joint : avatar.joints)
                for (unsigned i = 0; i < 16; ++i)
                    joint.world.getF32ptr()[i] = static_cast<float>(static_cast<int>(random() % 2049) - 1024) / 128.f;
            legacy(expected.data() + batch * count, count, &skin, &avatar);
            production(output.data() + batch * count, count, &skin, &avatar);
        }
        if (std::memcmp(output.data(), expected.data(), output.size() * sizeof(LLMatrix4a)) != 0)
        { std::cerr << "Native SIMD benchmark equivalence failed\n"; return 1; }
        const auto iterations = std::max<std::size_t>(10000, 3000000 / count);
        std::array<std::vector<double>, 2> samples;
        const std::array<Kernel, 2> kernels = {legacy, production};
        for (unsigned round = 0; round < 9; ++round)
            for (unsigned j = 0; j < 2; ++j)
            {
                const auto index = round % 2 ? 1 - j : j;
                samples[index].push_back(timed(kernels[index], skins, avatars, output, count, iterations));
            }
        for (auto& values : samples) std::sort(values.begin(), values.end());
        std::cout << count << ',' << std::fixed << std::setprecision(3) << samples[0][4] << ',' << samples[1][4] << '\n';
    }
    std::cout << "observed_checksum=" << observed << '\n';
    return 0;
}
}
int main(int argc, char** argv) { return SkinningBenchmark::run(argc, argv); }
"""

HARNESS = r"""
#ifdef FS_NATIVE_SKINNING_MATH
#include "linden_common.h"
#include "llmath.h"
#include "llmatrix4a.h"
using NativeMatrix = LLMatrix4a;
#else
using S32 = int;
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#undef LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR
#define DEBUG_SKINNING 0
namespace SkinningFixture
{
constexpr int LL_CHARACTER_MAX_ANIMATED_JOINTS = @JOINT_CAP@;
unsigned uninitializedReads = 0, invalidIndices = 0, copies = 0, comparisons = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
struct TrackedMatrix
{
#ifdef FS_NATIVE_SKINNING_MATH
    NativeMatrix value;
    float* data() { return value.getF32ptr(); }
    const float* data() const { return value.getF32ptr(); }
#else
    std::array<float, 16> value{};
    float* data() { return value.data(); }
    const float* data() const { return value.data(); }
#endif
    bool initialized = false;
    TrackedMatrix() = default;
    TrackedMatrix(const TrackedMatrix& other) { *this = other; }
    TrackedMatrix& operator=(const TrackedMatrix& other)
    {
        if (other.initialized) std::memcpy(data(), other.data(), 64);
        initialized = other.initialized;
        ++copies;
        return *this;
    }
    void fill(float number)
    {
        std::fill(data(), data() + 16, number); initialized = true;
    }
};
// Keep the native math type available above; only the extracted function's
// matrix boundary is wrapped to detect undefined reads deterministically.
#define LLMatrix4a TrackedMatrix
void matMulUnsafe(const TrackedMatrix& a, const TrackedMatrix& b, TrackedMatrix& result)
{
    if (!a.initialized || !b.initialized)
    {
        ++uninitializedReads; result.initialized = false; return;
    }
#ifdef FS_NATIVE_SKINNING_MATH
    ::matMulUnsafe(a.value, b.value, result.value);
#else
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
        {
            float sum = a.data()[row * 4] * b.data()[column];
            for (unsigned k = 1; k < 4; ++k)
                sum += a.data()[row * 4 + k] * b.data()[k * 4 + column];
            result.data()[row * 4 + column] = sum;
        }
#endif
    result.initialized = true;
}
template<class T> struct Table : std::vector<T>
{
    T guard;
    S32 size() const { return static_cast<S32>(std::vector<T>::size()); }
    T& operator[](S32 index)
    {
        if (index < 0 || index >= size()) { ++invalidIndices; return guard; }
        return std::vector<T>::operator[](static_cast<std::size_t>(index));
    }
    const T& operator[](S32 index) const
    {
        if (index < 0 || index >= size()) { ++invalidIndices; return guard; }
        return std::vector<T>::operator[](static_cast<std::size_t>(index));
    }
};
struct LLJoint
{
    TrackedMatrix world;
    const TrackedMatrix& getWorldMatrix4a() const { return world; }
};
struct LLMeshSkinInfo
{
    Table<TrackedMatrix> mInvBindMatrix;
    std::vector<S32> mJointNums;
};
struct LLVOAvatar
{
    std::vector<LLJoint> joints;
    std::vector<bool> present;
    std::vector<int> lookedUp;
    unsigned initializations = 0, dumps = 0;
    LLJoint* getJoint(int index)
    {
        lookedUp.push_back(index);
        return index >= 0 && index < static_cast<int>(joints.size()) && present[index] ? &joints[index] : nullptr;
    }
};
void dump_avatar_and_skin_state(const std::string&, LLVOAvatar* avatar, const LLMeshSkinInfo*) { ++avatar->dumps; }
void initJointNums(LLMeshSkinInfo*, LLVOAvatar* avatar) { ++avatar->initializations; }
@LEGACY@
@PRODUCTION@

struct Fixture
{
    LLMeshSkinInfo skin;
    LLVOAvatar avatar;
    explicit Fixture(int count)
    {
        skin.mInvBindMatrix.resize(count);
        avatar.joints.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS);
        avatar.present.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS, true);
        for (int i = 0; i < count; ++i)
        {
            skin.mJointNums.push_back((i * 7) % LL_CHARACTER_MAX_ANIMATED_JOINTS);
            skin.mInvBindMatrix[i].fill(static_cast<float>(i + 1) / 16.f);
        }
        for (unsigned i = 0; i < avatar.joints.size(); ++i)
            avatar.joints[i].world.fill(static_cast<float>(i + 1) / 8.f);
    }
};
std::vector<TrackedMatrix> output(int count)
{
    std::vector<TrackedMatrix> result(count + 2);
    for (auto& matrix : result) matrix.fill(-1234.f);
    return result;
}
void reset(LLVOAvatar& avatar)
{
    avatar.lookedUp.clear(); avatar.initializations = avatar.dumps = 0;
    uninitializedReads = invalidIndices = copies = 0;
}
bool same(const TrackedMatrix& a, const TrackedMatrix& b)
{
    return a.initialized && b.initialized && std::memcmp(a.data(), b.data(), 64) == 0;
}
void guards(const std::vector<TrackedMatrix>& result)
{
    TrackedMatrix sentinel; sentinel.fill(-1234.f);
    require(same(result.front(), sentinel) && same(result.back(), sentinel), "Output guard changed");
}
void valid(Fixture& fixture, int count)
{
    auto old = output(count), current = output(count);
    reset(fixture.avatar);
    legacy(old.data() + 1, count, &fixture.skin, &fixture.avatar);
    require(uninitializedReads == 0 && invalidIndices == 0, "Invalid valid-data reference fixture");
    const auto oldLookups = fixture.avatar.lookedUp;
    const auto oldCopies = copies;
    reset(fixture.avatar);
    production(current.data() + 1, count, &fixture.skin, &fixture.avatar);
    require(uninitializedReads == 0 && invalidIndices == 0, "Production read invalid matrix/table data");
    require(fixture.avatar.lookedUp == oldLookups && fixture.avatar.initializations == 1 && fixture.avatar.dumps == 0,
            "Valid lookup/initialization/diagnostic behavior changed");
    require(oldCopies == static_cast<unsigned>(count) && copies == 0, "World-matrix copies were not eliminated");
    for (int i = 0; i < count + 2; ++i) require(same(old[i], current[i]), "Valid matrix output changed");
    guards(current); ++comparisons;
}
void missing(Fixture& fixture, int count)
{
    auto result = output(count);
    reset(fixture.avatar);
    legacy(result.data() + 1, count, &fixture.skin, &fixture.avatar);
    require(uninitializedReads > 0, "Legacy missing-joint defect was not reproduced");
    result = output(count);
    reset(fixture.avatar);
    production(result.data() + 1, count, &fixture.skin, &fixture.avatar);
    require(uninitializedReads == 0, "Production read an uninitialized joint world matrix");
    require(invalidIndices == 0, "Production indexed invalid data");
    unsigned expectedDumps = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto id = fixture.skin.mJointNums[i];
        TrackedMatrix expected;
        if (fixture.avatar.present[id]) matMulUnsafe(fixture.skin.mInvBindMatrix[i], fixture.avatar.joints[id].world, expected);
        else { expected = fixture.skin.mInvBindMatrix[i]; ++expectedDumps; }
        require(same(result[i + 1], expected), "Missing-joint fallback or valid neighbor changed");
    }
    require(fixture.avatar.dumps == expectedDumps && fixture.avatar.lookedUp.size() == static_cast<std::size_t>(count) &&
            fixture.avatar.initializations == 1, "Missing-joint diagnostics/lookups changed");
    guards(result); ++comparisons;
}
int run()
{
    Fixture broken(3);
    broken.avatar.present[broken.skin.mJointNums[1]] = false;
    missing(broken, 3); // Fails deterministically on the unchanged old production body.

    for (int count : {1, 4, 15, 32, 64, 110, LL_CHARACTER_MAX_ANIMATED_JOINTS})
    {
        Fixture fixture(count);
        valid(fixture, count);
        for (int index : {0, count / 2, count - 1})
        {
            fixture.avatar.present[fixture.skin.mJointNums[index]] = false;
            missing(fixture, count);
            fixture.avatar.present[fixture.skin.mJointNums[index]] = true;
        }
        std::fill(fixture.avatar.present.begin(), fixture.avatar.present.end(), false);
        missing(fixture, count);
        std::fill(fixture.avatar.present.begin(), fixture.avatar.present.end(), true);
        std::fill(fixture.skin.mJointNums.begin(), fixture.skin.mJointNums.end(), 0);
        valid(fixture, count); // Repeated joint IDs.
        fixture.skin.mInvBindMatrix.resize(count - 1);
        auto result = output(count);
        reset(fixture.avatar);
        production(result.data() + 1, count, &fixture.skin, &fixture.avatar);
        require(fixture.avatar.initializations == 1 && fixture.avatar.lookedUp.empty() && invalidIndices == 0,
                "Short inverse-bind rejection changed");
        for (const auto& matrix : result) require(matrix.data()[0] == -1234.f, "Short input wrote output");
        ++comparisons;
    }
    // An empty/negative request must not form &empty_vector[0] or touch a joint.
    Fixture empty(0);
    for (int count : {0, -1})
    {
        reset(empty.avatar);
        production(nullptr, count, &empty.skin, &empty.avatar);
        require(empty.avatar.initializations == 0 && empty.avatar.lookedUp.empty() && invalidIndices == 0,
                "Empty request was not a defined no-op");
        ++comparisons;
    }
    std::mt19937 random(0x46534745);
    for (int iteration = 0; iteration < 400; ++iteration)
    {
        const int count = 1 + static_cast<int>(random() % 110);
        Fixture fixture(count);
        for (auto& matrix : fixture.skin.mInvBindMatrix)
            for (unsigned i = 0; i < 16; ++i)
                matrix.data()[i] = static_cast<float>(static_cast<int>(random() % 2049) - 1024) / 128.f;
        for (auto& joint : fixture.avatar.joints)
            for (unsigned i = 0; i < 16; ++i)
                joint.world.data()[i] = static_cast<float>(static_cast<int>(random() % 2049) - 1024) / 128.f;
        valid(fixture, count);
        fixture.avatar.present[fixture.skin.mJointNums[count / 2]] = false;
        missing(fixture, count);
        fixture.avatar.present[fixture.skin.mJointNums[count / 2]] = true;
        fixture.avatar.joints[0].world.fill(-2.f);
        valid(fixture, count);
    }
    std::cout << comparisons << " production skinning-generation comparisons passed; legacy uninitialized read reproduced\n";
    return 0;
}
}
int main() { return SkinningFixture::run(); }
"""


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for match in re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]', text[brace:], re.S):
        token = match.group()
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                return text[start:brace + match.end()]
    raise ValueError("Unterminated production function")


def generate(source: Path, mode: str = "tracked") -> str:
    text = source.read_text(encoding="utf-8")
    body = function(text, "void LLSkinningUtil::initSkinningMatrixPalette(")
    body = body.replace("LLSkinningUtil::initSkinningMatrixPalette", "production", 1)
    cap = re.search(r"LL_CHARACTER_MAX_ANIMATED_JOINTS\s*=\s*(\d+)",
                    (ROOT.parent / "llcharacter/lljoint.h").read_text(encoding="utf-8"))
    if cap is None:
        raise ValueError("Cannot locate production joint cap")
    template = HARNESS if mode == "tracked" else BENCHMARK
    return template.replace("@JOINT_CAP@", cap.group(1)).replace("@LEGACY@", LEGACY).replace("@PRODUCTION@", body)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "llskinningutil.cpp")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("tracked", "benchmark"), default="tracked")
    arguments = parser.parse_args()
    arguments.output.write_text(generate(arguments.source, arguments.mode), encoding="utf-8")
