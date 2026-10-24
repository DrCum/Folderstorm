// Independent test-only fixture; the preceding index experiment stays frozen.
#include "linden_common.h"
#include "llpose.h"
#include "llsimdmath.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace BlenderPoolHint
{
using Entry = std::pair<LLJoint*, LLJointStateBlender*>;
size_t checks = 0;
void require(bool ok) { ++checks; if (!ok) { std::cerr << "Pool hint failed at " << checks << '\n'; std::exit(1); } }
struct Objects
{
    std::vector<std::unique_ptr<LLJoint>> joints;
    std::vector<std::unique_ptr<LLJointStateBlender>> blenders;
    std::vector<Entry> entries;
    std::vector<LLJoint*> misses;
    Objects()
    {
        std::vector<LLJoint*> pointers;
        for (unsigned i = 0; i < 2048; ++i)
        {
            joints.push_back(std::make_unique<LLJoint>(i % LL_CHARACTER_MAX_ANIMATED_JOINTS));
            pointers.push_back(joints.back().get());
        }
        std::sort(pointers.begin(), pointers.end(), std::less<LLJoint*>{});
        for (unsigned i = 0; i < 1024; ++i)
        {
            blenders.push_back(std::make_unique<LLJointStateBlender>());
            entries.emplace_back(pointers[2 * i], blenders.back().get());
            misses.push_back(pointers[2 * i + 1]);
        }
    }
};
using Pool = std::map<LLJoint*, LLJointStateBlender*>;
struct Tree
{
    const Pool& pool;
    LLJointStateBlender* find(LLJoint* key) const
    {
        const auto it = pool.find(key);
        return it == pool.end() ? nullptr : it->second;
    }
};

template<size_t Slots, bool Count = false, bool Collision = false> struct Hint
{
    const Pool& pool;
    mutable std::array<Entry, Slots> hints{};
    mutable size_t hits = 0, misses = 0;
    size_t slot(LLJoint* key) const
    {
        static_assert(Slots && !(Slots & (Slots - 1)));
        return Collision ? 0 : (reinterpret_cast<uintptr_t>(key) >> 4) & (Slots - 1);
    }
    LLJointStateBlender* find(LLJoint* key) const
    {
        auto& entry = hints[slot(key)];
        if (entry.first == key && entry.second)
        {
            if constexpr (Count) ++hits;
            return entry.second;
        }
        if constexpr (Count) ++misses;
        const auto it = pool.find(key);
        if (it == pool.end()) return nullptr;
        entry = {key, it->second};
        return it->second;
    }
};

std::vector<LLJoint*> queries(const Objects& objects, unsigned count, unsigned pattern)
{
    std::vector<LLJoint*> result;
    uint32_t state = 0x82341u;
    for (unsigned i = 0; i < 1024; ++i)
    {
        state = state * 1664525u + 1013904223u;
        if (!count || pattern == 3 || (pattern == 2 && i % 4 == 0))
            result.push_back(objects.misses[i % std::max(count, 1u)]);
        else
            result.push_back(objects.entries[(pattern == 1 ? state % count : pattern == 2 ? i % std::min(count, 3u) : i % count)].first);
    }
    return result;
}
const char* patternName(unsigned pattern)
{
    return pattern == 0 ? "cyclic" : pattern == 1 ? "shuffled" : pattern == 2 ? "shared_mixed" : pattern == 3 ? "absent" : "forced_collision";
}
template<size_t Slots, bool Collision> void correctness(const Objects& objects, const Pool& pool, unsigned count, unsigned pattern)
{
    Tree reference{pool}; Hint<Slots, true, Collision> hint{pool};
    const auto keys = queries(objects, count, pattern);
    for (unsigned repeat = 0; repeat < 3; ++repeat)
        for (auto* key : keys) require(reference.find(key) == hint.find(key));
    require(hint.hits + hint.misses == 3072);
    for (const auto& entry : hint.hints)
        require(!entry.second || (pool.find(entry.first) != pool.end() && pool.find(entry.first)->second == entry.second));
    std::cout << Slots << ',' << count << ',' << patternName(pattern) << ',' << hint.hits << ',' << hint.misses
        << ',' << sizeof(hint.hints) << '\n';
}
volatile uintptr_t observed = 0;
template<class Index>
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
uintptr_t batch(const Index& index, const std::vector<LLJoint*>& keys, unsigned offset)
{
    uintptr_t checksum = 0;
    for (unsigned i = 0; i < 1024; ++i)
        checksum += reinterpret_cast<uintptr_t>(index.find(keys[(i + offset) & 1023]));
    return checksum;
}
template<class Index> void measure(const Index& index, const std::vector<LLJoint*>& keys, unsigned count, unsigned pattern, unsigned slots, unsigned sample, unsigned candidate)
{
    observed = batch(index, keys, 0);
    uintptr_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned repeat = 0; repeat < 64; ++repeat) checksum += batch(index, keys, repeat);
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    observed = checksum;
    std::cout << candidate << ',' << slots << ',' << count << ',' << patternName(pattern) << ',' << sample << ",65536," << ns << ',' << checksum << '\n';
}
template<size_t Slots, bool Collision> void pair(const Pool& pool, const std::vector<LLJoint*>& keys, unsigned count, unsigned pattern, unsigned sample)
{
    Tree reference{pool}; Hint<Slots, false, Collision> hint{pool};
    if (sample % 4 == 0 || sample % 4 == 3)
    { measure(reference, keys, count, pattern, 0, sample, Slots); measure(hint, keys, count, pattern, Slots, sample, Slots); }
    else
    { measure(hint, keys, count, pattern, Slots, sample, Slots); measure(reference, keys, count, pattern, 0, sample, Slots); }
}
int run(int argc, char** argv)
{
    LLVector4a::initClass(); Objects objects;
    const bool timing = argc == 2 && std::string(argv[1]) == "--benchmark";
    if (argc != 1 && !timing) return 1;
    std::cout << (timing ? "candidate_slots,slots,count,pattern,sample,operations,elapsed_ns,checksum\n" : "slots,count,pattern,hits,misses,cache_bytes\n");
    for (unsigned count : {0u, 1u, 3u, 32u, 128u, 256u, 1024u})
    {
        Pool pool;
        for (unsigned i = 0; i < count; ++i) pool.emplace(objects.entries[i]);
        for (unsigned pattern = 0; pattern < 5; ++pattern)
        {
            const auto keys = queries(objects, count, pattern);
            if (!timing)
            {
                if (pattern == 4)
                { correctness<1,true>(objects,pool,count,pattern); correctness<16,true>(objects,pool,count,pattern); correctness<64,true>(objects,pool,count,pattern); correctness<256,true>(objects,pool,count,pattern); }
                else
                { correctness<1,false>(objects,pool,count,pattern); correctness<16,false>(objects,pool,count,pattern); correctness<64,false>(objects,pool,count,pattern); correctness<256,false>(objects,pool,count,pattern); }
            }
            else for (unsigned sample = 0; sample < 6; ++sample)
            {
                if (pattern == 4)
                { pair<1,true>(pool,keys,count,pattern,sample); pair<16,true>(pool,keys,count,pattern,sample); pair<64,true>(pool,keys,count,pattern,sample); pair<256,true>(pool,keys,count,pattern,sample); }
                else
                { pair<1,false>(pool,keys,count,pattern,sample); pair<16,false>(pool,keys,count,pattern,sample); pair<64,false>(pool,keys,count,pattern,sample); pair<256,false>(pool,keys,count,pattern,sample); }
            }
        }
    }
    if (!timing) std::cerr << checks << " hint/canonical/collision checks passed\n";
    return 0;
}
}
int main(int argc, char** argv) { return BlenderPoolHint::run(argc, argv); }
