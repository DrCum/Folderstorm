/**
 * @file test_fsavatarpalette.cpp
 * @brief Exact layout/guard checks and an opt-in native packing benchmark.
 */

#include "../fsavatarpalette.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#define FS_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define FS_NOINLINE __attribute__((noinline))
#else
#define FS_NOINLINE
#endif

namespace
{
struct alignas(16) Matrix { float values[16]; };
using Kernel = void (*)(const Matrix*, std::size_t, float*);
volatile std::uint64_t observed = 0;
unsigned comparisons = 0;

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

// Preserve the previous twelve assignments as the benchmark/reference path.
FS_NOINLINE void scalarReference(const Matrix* matrices, std::size_t count, float* output)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        const float* m = matrices[i].values;
        const auto idx = i * 12;
        output[idx + 0] = m[0]; output[idx + 1] = m[1]; output[idx + 2] = m[2]; output[idx + 3] = m[12];
        output[idx + 4] = m[4]; output[idx + 5] = m[5]; output[idx + 6] = m[6]; output[idx + 7] = m[13];
        output[idx + 8] = m[8]; output[idx + 9] = m[9]; output[idx + 10] = m[10]; output[idx + 11] = m[14];
    }
}

FS_NOINLINE void packed(const Matrix* matrices, std::size_t count, float* output)
{
    for (std::size_t i = 0; i < count; ++i)
        FSAvatarPalette::packMatrix(matrices[i].values, output + i * 12);
}

void check(const std::vector<Matrix>& input)
{
    // This independently specified lane map also checks the scalar reference.
    constexpr unsigned lanes[] = {0, 1, 2, 12, 4, 5, 6, 13, 8, 9, 10, 14};
    constexpr std::uint32_t sentinel = 0x6a5a4a3a;
    const auto before = input;
    for (std::size_t offset = 0; offset < 4; ++offset)
    {
        const std::size_t prefix = 8 + offset;
        std::vector<std::uint32_t> expected(prefix + input.size() * 12 + 8, sentinel);
        for (std::size_t i = 0; i < input.size(); ++i)
            for (std::size_t lane = 0; lane < 12; ++lane)
                std::memcpy(&expected[prefix + i * 12 + lane], &input[i].values[lanes[lane]], 4);
        for (auto kernel : {scalarReference, packed})
        {
            std::vector<float> output(expected.size());
            for (auto& value : output) std::memcpy(&value, &sentinel, 4);
            kernel(input.data(), input.size(), output.data() + prefix);
            require(std::memcmp(output.data(), expected.data(), expected.size() * 4) == 0,
                    "Palette bytes or guard regions changed");
            ++comparisons;
        }
    }
    require(input.empty() || std::memcmp(input.data(), before.data(), input.size() * sizeof(Matrix)) == 0,
            "Packing modified its input");
}

void correctness()
{
    static_assert(sizeof(float) == 4 && sizeof(Matrix) == 64, "Expected 32-bit matrix lanes");
    const std::array<Matrix, 3> examples = {{
        {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}},
        {{1,0,0,0, 0,1,0,0, 0,0,1,0, 7,-3,9,1}},
        {{-2,3,4,5, 6,-7,8,9, 10,11,-12,13, 14,15,16,17}}
    }};
    for (const auto& example : examples) check(std::vector<Matrix>{example});

    constexpr std::uint32_t patterns[] = {
        0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff,
        0x00800000, 0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc01234,
        0xffc05678, 0x7f801234, 0xff805678, 0xffffffff
    };
    std::mt19937 random(0x46535041);
    for (std::size_t count : std::array<std::size_t, 10>{0,1,2,4,15,32,64,110,216,4096})
    {
        std::vector<Matrix> input(count);
        for (std::size_t i = 0; i < count; ++i)
            for (unsigned lane = 0; lane < 16; ++lane)
                std::memcpy(&input[i].values[lane], &patterns[(i * 16 + lane) % std::size(patterns)], 4);
        check(input);
        for (auto& matrix : input)
            for (auto& value : matrix.values)
            {
                const auto bits = static_cast<std::uint32_t>(random());
                std::memcpy(&value, &bits, 4);
            }
        check(input);
    }

    // The input boundary accepts naturally aligned float arrays at any SIMD offset.
    for (std::size_t offset = 0; offset < 4; ++offset)
    {
        std::array<float, 20> unaligned{};
        std::memcpy(unaligned.data() + offset, examples[2].values, 64);
        std::array<float, 12> expected{}, actual{};
        scalarReference(&examples[2], 1, expected.data());
        FSAvatarPalette::packMatrix(unaligned.data() + offset, actual.data());
        require(std::memcmp(actual.data(), expected.data(), 48) == 0, "Unaligned input packing mismatch");
        ++comparisons;
    }
    std::cout << comparisons << " exact palette layout/guard comparisons passed\n";
}

FS_NOINLINE double timed(Kernel kernel, const std::vector<Matrix>& input,
                        std::vector<float>& output, std::size_t count, std::size_t iterations)
{
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        const auto palette = i & 7;
        kernel(input.data() + palette * count, count, output.data() + palette * count * 12);
        std::atomic_signal_fence(std::memory_order_acq_rel);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::uint64_t checksum = 0;
    for (float value : output)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        checksum += bits;
    }
    observed = checksum;
    return std::chrono::duration<double, std::nano>(elapsed).count() /
           static_cast<double>(iterations * count);
}

void benchmark()
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int id[4]; char brand[49] = {};
    __cpuid(id, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(id[0]) >= 0x80000004u)
        for (unsigned i = 0; i < 3; ++i)
        {
            __cpuid(id, static_cast<int>(0x80000002u + i));
            std::memcpy(brand + i * 16, id, 16);
        }
    std::cout << brand << "; MSVC " << _MSC_FULL_VER << '\n';
#endif
    std::cout << "count,legacy_median_ns_per_matrix,packed_median_ns_per_matrix\n";
    std::mt19937 random(0x46535041);
    for (std::size_t count : std::array<std::size_t, 7>{1,4,15,32,64,110,216})
    {
        std::vector<Matrix> input(count * 8);
        for (auto& matrix : input)
            for (auto& value : matrix.values)
                value = static_cast<float>(static_cast<int>(random() % 2049) - 1024) / 128.f;
        std::vector<float> output(count * 12 * 8);
        const auto iterations = std::max<std::size_t>(10000, 3000000 / count);
        std::array<std::vector<double>, 2> samples;
        const std::array<Kernel, 2> kernels = {scalarReference, packed};
        for (unsigned round = 0; round < 9; ++round)
            for (unsigned j = 0; j < 2; ++j)
            {
                const unsigned index = round % 2 ? 1 - j : j;
                samples[index].push_back(timed(kernels[index], input, output, count, iterations));
            }
        for (auto& values : samples) std::sort(values.begin(), values.end());
        std::cout << count << ',' << std::fixed << std::setprecision(3)
                  << samples[0][4] << ',' << samples[1][4] << '\n';
    }
    std::cout << "observed_checksum=" << observed << '\n';
}
}

int main(int argc, char** argv)
{
    correctness();
    if (argc == 2 && std::string(argv[1]) == "--benchmark") benchmark();
    else if (argc != 1) { std::cerr << "Usage: avatar_palette_packing [--benchmark]\n"; return 2; }
}
