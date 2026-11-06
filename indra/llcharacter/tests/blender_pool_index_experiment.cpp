// Test-only storage experiment. No production pool or replacement allocator in timing mode.
#include "linden_common.h"
#include "llpose.h"
#include "llsimdmath.h"
#include "sampling_allocation_probe.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace BlenderPoolIndex
{
using Entry = std::pair<LLJoint*, LLJointStateBlender*>;
size_t checks = 0;
void require(bool ok) { ++checks; if (!ok) { std::cerr << "Pool index experiment failed at " << checks << '\n'; std::exit(1); } }

struct Tree
{
    std::map<LLJoint*, LLJointStateBlender*> entries;
    static constexpr const char* name = "tree";
    LLJointStateBlender* find(LLJoint* key) const
    {
        const auto it = entries.find(key);
        return it == entries.end() ? nullptr : it->second;
    }
    std::pair<LLJointStateBlender*, bool> insert(Entry entry)
    {
        const auto [it, added] = entries.emplace(entry);
        return {it->second, added};
    }
    size_t capacityBytes() const { return 0; } // Node bytes are measured by the allocation probe.
};

struct Flat
{
    std::vector<Entry> entries;
    static constexpr const char* name = "flat";
    auto lower(LLJoint* key) const
    {
        return std::lower_bound(entries.begin(), entries.end(), key,
            [](const Entry& entry, LLJoint* value) { return std::less<LLJoint*>{}(entry.first, value); });
    }
    LLJointStateBlender* find(LLJoint* key) const
    {
        const auto it = lower(key);
        return it == entries.end() || it->first != key ? nullptr : it->second;
    }
    std::pair<LLJointStateBlender*, bool> insert(Entry entry)
    {
        const auto it = lower(entry.first);
        if (it != entries.end() && it->first == entry.first) return {it->second, false};
        entries.insert(it, entry);
        return {entry.second, true};
    }
    size_t capacityBytes() const { return entries.capacity() * sizeof(Entry); }
};

struct Objects
{
    std::vector<std::unique_ptr<LLJoint>> joints;
    std::vector<std::unique_ptr<LLJointStateBlender>> blenders;
    std::vector<Entry> entries;
    std::vector<LLJoint*> misses;
    Objects()
    {
        for (unsigned i = 0; i < 2048; ++i)
            joints.push_back(std::make_unique<LLJoint>(i % LL_CHARACTER_MAX_ANIMATED_JOINTS));
        std::vector<LLJoint*> pointers;
        for (const auto& joint : joints) pointers.push_back(joint.get());
        std::sort(pointers.begin(), pointers.end(), std::less<LLJoint*>{});
        for (unsigned i = 0; i < 1024; ++i)
        {
            blenders.push_back(std::make_unique<LLJointStateBlender>());
            entries.emplace_back(pointers[2 * i], blenders.back().get());
            misses.push_back(pointers[2 * i + 1]);
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
            { return std::less<LLJoint*>{}(a.first, b.first); });
    }
};

std::vector<Entry> ordered(const Objects& objects, unsigned count, unsigned order)
{
    std::vector<Entry> result(objects.entries.begin(), objects.entries.begin() + count);
    if (order == 1) std::reverse(result.begin(), result.end());
    if (order == 2)
    {
        uint32_t state = 0x82341u;
        for (size_t i = result.size(); i > 1; --i)
        {
            state = state * 1664525u + 1013904223u;
            std::swap(result[i - 1], result[state % i]);
        }
    }
    return result;
}
const char* orderName(unsigned order) { return order == 0 ? "ascending" : order == 1 ? "descending" : "shuffled"; }

template<class Index> void fill(Index& index, const std::vector<Entry>& input)
{
    for (const auto& entry : input) require(index.insert(entry) == std::make_pair(entry.second, true));
}

int correctness(const Objects& objects)
{
    for (unsigned count : {0u, 1u, 3u, 32u, 128u, 256u, 1024u}) for (unsigned order = 0; order < 3; ++order)
    {
        const auto input = ordered(objects, count, order);
        Tree tree; Flat flat;
        fill(tree, input); fill(flat, input);
        require(tree.entries.size() == flat.entries.size());
        auto reference = tree.entries.begin();
        for (const auto& entry : flat.entries)
        {
            require(reference != tree.entries.end() && reference->first == entry.first && reference->second == entry.second);
            ++reference;
            require(tree.find(entry.first) == flat.find(entry.first));
            // A different value must not replace the first canonical entry.
            const Entry duplicate{entry.first, objects.blenders.back().get()};
            require(tree.insert(duplicate) == std::make_pair(entry.second, false));
            require(flat.insert(duplicate) == std::make_pair(entry.second, false));
        }
        for (auto* key : objects.misses) require(!tree.find(key) && !flat.find(key));
    }
    std::cout << "Pool index: " << checks << " correctness checks passed\n";
    return 0;
}

template<class Index> void memory(const std::vector<Entry>& input, unsigned count, unsigned order)
{
#if FS_SAMPLING_ALLOCATION_PROBE
    FSSamplingAlloc::Stats cold, warm, end; size_t capacity = 0;
    {
        FSSamplingAlloc::Scope probe;
        {
            Index index; fill(index, input);
            cold = FSSamplingAlloc::snapshot(); capacity = index.capacityBytes();
            for (const auto& entry : input) require(index.find(entry.first) == entry.second);
            warm = FSSamplingAlloc::snapshot();
        }
        end = FSSamplingAlloc::snapshot();
    }
    require(end.live == end.baseline);
    require(warm.calls == cold.calls);
    std::cout << Index::name << ',' << count << ',' << orderName(order) << ',' << sizeof(Index)
        << ',' << cold.calls << ',' << cold.requested << ',' << cold.live - cold.baseline
        << ',' << cold.peak - cold.baseline << ',' << capacity << ',' << end.live - end.baseline << '\n';
#endif
}

template<class Index> void failures(const std::vector<Entry>& input)
{
#if FS_SAMPLING_ALLOCATION_PROBE
    unsigned failures = 0;
    for (int fault = 0; fault < 40; ++fault)
    {
        FSSamplingAlloc::Stats end;
        {
            FSSamplingAlloc::Scope probe;
            {
                Index index;
                FSSamplingAlloc::failureCountdown.store(fault);
                try { for (const auto& entry : input) index.insert(entry); }
                catch (const std::bad_alloc&) { ++failures; }
                FSSamplingAlloc::failureCountdown.store(-1);
                // Failed growth must preserve every admitted key/value and allow retry.
                for (const auto& entry : index.entries) require(index.find(entry.first) == entry.second);
                for (const auto& entry : input) index.insert(entry);
                for (const auto& entry : input) require(index.find(entry.first) == entry.second);
                require(index.entries.size() == input.size());
            }
            end = FSSamplingAlloc::snapshot();
        }
        require(end.live == end.baseline);
    }
    require(failures > 0);
    std::cerr << Index::name << ": " << failures << " allocation failures exercised\n";
#endif
}

volatile uintptr_t observed = 0;
// Keep repeated batches as actual lookup work without a per-lookup volatile store.
template<class Index>
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
uintptr_t lookupBatch(const Index& index, const std::vector<LLJoint*>& keys, unsigned offset)
{
    uintptr_t checksum = 0;
    for (unsigned i = 0; i < 1024; ++i)
        checksum += reinterpret_cast<uintptr_t>(index.find(keys[(i + offset) & 1023]));
    return checksum;
}
template<class Index> void timing(const Objects& objects, const std::vector<Entry>& input, unsigned count, unsigned order, unsigned sample)
{
    Index index; fill(index, input);
    for (unsigned query = 0; query < 3; ++query)
    {
        std::vector<LLJoint*> keys;
        for (unsigned i = 0; i < 1024; ++i)
            keys.push_back(count && (query == 0 || (query == 2 && i % 4))
                ? input[(query == 2 ? i % std::min(count, 3u) : i % count)].first : objects.misses[i % std::max(count, 1u)]);
        uintptr_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned repeat = 0; repeat < 64; ++repeat)
            checksum += lookupBatch(index, keys, repeat);
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        observed = checksum;
        std::cout << Index::name << ",lookup," << count << ',' << orderName(order) << ','
            << (query == 0 ? "hits" : query == 1 ? "misses" : "shared_mixed") << ',' << sample
            << ",64,65536," << ns << ',' << checksum << ',' << index.capacityBytes() << '\n';
    }
    const auto start = std::chrono::steady_clock::now();
    uintptr_t checksum = 0;
    for (unsigned repeat = 0; repeat < 64; ++repeat)
    {
        Index cold;
        for (const auto& entry : input) checksum += reinterpret_cast<uintptr_t>(cold.insert(entry).first);
        observed = checksum;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << Index::name << ",cold_build_destroy," << count << ',' << orderName(order) << ",all," << sample
        << ",64," << 64 * count << ',' << ns << ',' << checksum << ',' << index.capacityBytes() << '\n';
}

int run(int argc, char** argv)
{
    LLVector4a::initClass();
    Objects objects;
    const std::string mode = argc == 2 ? argv[1] : "";
    if (argc == 1) return correctness(objects);
    if (mode == "--allocations" && FS_SAMPLING_ALLOCATION_PROBE)
    {
#if TRACY_ENABLE
        std::cerr << "Global allocation injection requires Tracy OFF\n";
        return 0;
#endif
        std::cout << "backend,count,order,object_bytes,new_calls,requested_bytes,live_bytes,peak_bytes,capacity_bytes,teardown_live\n";
        for (unsigned count : {0u, 1u, 3u, 32u, 128u, 256u, 1024u}) for (unsigned order = 0; order < 3; ++order)
        {
            const auto input = ordered(objects, count, order);
            memory<Tree>(input, count, order); memory<Flat>(input, count, order);
        }
        const auto input = ordered(objects, 32, 2);
        failures<Tree>(input); failures<Flat>(input);
        std::cerr << checks << " allocation/growth/retry checks passed\n";
        return 0;
    }
    if (mode != "--benchmark" || FS_SAMPLING_ALLOCATION_PROBE) return 1;
    std::cout << "backend,phase,count,order,query,sample,iterations,operations,elapsed_ns,checksum,capacity_bytes\n";
    for (unsigned count : {0u, 1u, 3u, 32u, 128u, 256u, 1024u}) for (unsigned order = 0; order < 3; ++order)
    {
        const auto input = ordered(objects, count, order);
        for (unsigned sample = 0; sample < 6; ++sample)
        {
            if (sample % 4 == 0 || sample % 4 == 3)
            { timing<Tree>(objects, input, count, order, sample); timing<Flat>(objects, input, count, order, sample); }
            else
            { timing<Flat>(objects, input, count, order, sample); timing<Tree>(objects, input, count, order, sample); }
        }
    }
    return 0;
}
}
int main(int argc, char** argv) { return BlenderPoolIndex::run(argc, argv); }
