// Focused storage/decoding checks. No viewer, account or rendering dependencies.
#include "fsanimationkeys.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
unsigned comparisons = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
struct Key
{
    inline static int live = 0;
    inline static int copyBudget = -1;
    std::array<std::uint32_t, 4> bits{};
    Key() { ++live; }
    Key(const Key& other) { copy(); bits = other.bits; ++live; }
    Key& operator=(const Key& other) { copy(); bits = other.bits; return *this; }
    ~Key() { --live; }
    static void copy()
    {
        if (copyBudget == 0) throw std::runtime_error("injected key copy failure");
        if (copyBudget > 0) --copyBudget;
    }
};
using Map = FSAnimationKeys::KeyMap<float, Key>;
using Input = std::vector<std::pair<float, Key>>;
static_assert(!std::is_copy_constructible_v<FSAnimationKeys::Builder<Map>>);
#if FS_CONTIGUOUS_ANIMATION_KEYS
static_assert(std::is_same_v<Map, boost::container::flat_map<float, Key>>);
#else
static_assert(std::is_same_v<Map, std::map<float, Key>>);
static_assert(sizeof(FSAnimationKeys::Builder<Map>) == sizeof(Map*), "Map builds need no scratch representation");
#endif

// Simulate parse exits around the actual builder, not the full asset decoder.
bool decode(Map& target, const Input& input, std::size_t available)
{
    FSAnimationKeys::Builder<Map> builder(target);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (i >= available || !std::isfinite(input[i].first)) return false;
        builder.set(input[i].first, input[i].second);
    }
    builder.commit();
    return true;
}
void compare(const Map& actual, const std::map<float, Key>& expected)
{
    require(actual.size() == expected.size(), "Unique key count changed");
    auto entry = actual.begin();
    for (const auto& item : expected)
    {
        require(entry->first == item.first && entry->second.bits == item.second.bits, "Sorted/duplicate key data changed");
        ++entry; ++comparisons;
    }
}
void loadCases()
{
    std::mt19937 random(0x46534b59);
    for (unsigned count : {0, 1, 2, 4, 32, 128, 1024})
        for (unsigned order : {0, 1, 2})
        {
            Input input;
            for (unsigned i = 0; i < count; ++i)
            {
                Key key;
                key.bits = {static_cast<std::uint32_t>(random()), 0x80000000u, 0x7fc12345u, i};
                input.emplace_back(static_cast<float>(i) / 8.f, key);
                if (i % 7 == 0)
                {
                    key.bits[0] ^= 0xffffffffu;
                    input.emplace_back(input.back().first, key);
                }
            }
            if (order == 1) std::reverse(input.begin(), input.end());
            if (order == 2) std::shuffle(input.begin(), input.end(), random);
            std::map<float, Key> expected;
            for (const auto& item : input) expected[item.first] = item.second;
            Map target;
            require(decode(target, input, input.size()), "Valid keys rejected");
            compare(target, expected);
            const int baseline = Key::live;
            for (std::size_t available : {std::size_t(0), input.size() / 2})
            {
                Map partial;
                const bool success = decode(partial, input, available);
                require(success == input.empty(), "Truncated parse result changed");
#if FS_CONTIGUOUS_ANIMATION_KEYS
                require(partial.empty(), "Failed staging published partial keys");
#endif
            }
            require(Key::live == baseline, "Early parse exit leaked staged keys");
        }
}
void signedTimeAndFailureCases()
{
    Input input(4);
    input[0].first = -0.f; input[0].second.bits = {1, 2, 3, 4};
    input[1].first = 0.f; input[1].second.bits = {5, 6, 7, 8};
    input[2].first = -2.f; input[2].second.bits = {9, 10, 11, 12};
    input[3].first = 3.f; input[3].second.bits = {13, 14, 15, 16};
    std::map<float, Key> expected;
    for (const auto& item : input) expected[item.first] = item.second;
    Map target;
    require(decode(target, input, input.size()), "Signed timestamps rejected");
    compare(target, expected);
    auto zero = target.find(0.f);
    require(zero != target.end() && std::signbit(zero->first), "Equivalent zero timestamp lost original key bits");
    input[2].first = std::numeric_limits<float>::infinity();
    const int baseline = Key::live;
    {
        Map partial;
        require(!decode(partial, input, input.size()), "Non-finite timestamp accepted by fixture parser");
#if FS_CONTIGUOUS_ANIMATION_KEYS
        require(partial.empty(), "Invalid input published staged keys");
#endif
    }
    require(Key::live == baseline, "Invalid-input exit leaked keys");

#if FS_CONTIGUOUS_ANIMATION_KEYS
    {
        FSAnimationKeys::Builder<Map> builder(target);
        builder.set(-1.f, input.front().second);
        Key::copyBudget = 0;
        bool failed = false;
        try { builder.commit(); } catch (const std::runtime_error&) { failed = true; }
        Key::copyBudget = -1;
        require(failed, "Conversion failure was not exercised");
        compare(target, expected);
    }
    require(Key::live == baseline, "Failed conversion leaked staging or replaced destination");
#endif
}
void scratchRelease()
{
    Map target;
    Key key;
    const int baseline = Key::live;
    {
        FSAnimationKeys::Builder<Map> builder(target);
        for (unsigned i = 0; i < 128; ++i) builder.set(static_cast<float>(i), key);
        builder.commit();
        require(Key::live == baseline + 128, "Committed builder retained scratch key objects");
    }
    require(Key::live == baseline + 128, "Builder destruction damaged committed keys");
}
}
int main()
{
    loadCases(); signedTimeAndFailureCases(); scratchRelease();
    require(Key::live == 0, "Key objects leaked");
    std::cout << comparisons << " key storage comparisons passed (contiguous=" << FS_CONTIGUOUS_ANIMATION_KEYS << ")\n";
}
