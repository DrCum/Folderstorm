// Separate executable only. No replacement allocators in timing builds.
#ifndef FS_TEST_SAMPLING_ALLOCATION_PROBE_H
#define FS_TEST_SAMPLING_ALLOCATION_PROBE_H
#ifndef FS_SAMPLING_ALLOCATION_PROBE
#define FS_SAMPLING_ALLOCATION_PROBE 0
#endif
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#if FS_SAMPLING_ALLOCATION_PROBE && defined(_WIN32)
#include <malloc.h>
#endif
namespace FSSamplingAlloc
{
struct Stats { size_t calls = 0, requested = 0, live = 0, peak = 0, baseline = 0; };
#if FS_SAMPLING_ALLOCATION_PROBE
// Fixed instrumentation storage is excluded from requested-byte metrics. No
// allocation from inside the registry and no header read before foreign pointers.
struct Cell { void* pointer = nullptr; size_t bytes = 0; };
inline constexpr size_t CAPACITY = 65536;
inline Cell cells[CAPACITY];
inline std::atomic_flag locked = ATOMIC_FLAG_INIT;
inline bool enabled = false;
inline Stats stats;
inline std::atomic<int> failureCountdown{-1}; // Test-only fault injection; negative disables it.
// One-shot reentry callback, installed only by single-threaded allocation tests.
inline std::atomic<void (*)(void*)> beforeAllocation{nullptr};
inline std::atomic<void*> allocationContext{nullptr};
struct Lock
{
    Lock() { while (locked.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { locked.clear(std::memory_order_release); }
};
inline size_t hash(void* pointer) { return (reinterpret_cast<uintptr_t>(pointer) >> 4) % CAPACITY; }
inline void* deleted() { return reinterpret_cast<void*>(uintptr_t(1)); }
inline void add(void* pointer, size_t bytes)
{
    Lock lock;
    if (!enabled) return;
    const auto begin = hash(pointer);
    for (size_t n = 0; n < CAPACITY; ++n)
    {
        auto& cell = cells[(begin + n) % CAPACITY];
        if (!cell.pointer || cell.pointer == deleted())
        {
            cell = {pointer, bytes}; ++stats.calls; stats.requested += bytes; stats.live += bytes;
            if (stats.live > stats.peak) stats.peak = stats.live;
            return;
        }
    }
    std::abort(); // Instrumentation capacity exhausted; never silently lose allocations.
}
inline void remove(void* pointer)
{
    if (!pointer) return;
    Lock lock;
    const auto begin = hash(pointer);
    for (size_t n = 0; n < CAPACITY; ++n)
    {
        auto& cell = cells[(begin + n) % CAPACITY];
        if (!cell.pointer) return;
        if (cell.pointer == pointer) { stats.live -= cell.bytes; cell = {deleted(), 0}; return; }
    }
}
inline void* allocate(size_t bytes, size_t alignment = 0)
{
    if (auto callback = beforeAllocation.exchange(nullptr))
        callback(allocationContext.exchange(nullptr));
    int remaining = failureCountdown.load(std::memory_order_relaxed);
    while (remaining >= 0)
    {
        if (!remaining) throw std::bad_alloc();
        if (failureCountdown.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed)) break;
    }
    void* pointer = nullptr;
    if (!alignment) pointer = std::malloc(bytes ? bytes : 1);
#if defined(_WIN32)
    else pointer = _aligned_malloc(bytes ? bytes : 1, alignment);
#else
    else if (posix_memalign(&pointer, alignment, bytes ? bytes : 1)) pointer = nullptr;
#endif
    if (!pointer) throw std::bad_alloc();
    add(pointer, bytes); return pointer;
}
inline void release(void* pointer, bool aligned = false)
{
    remove(pointer);
#if defined(_WIN32)
    if (aligned) { _aligned_free(pointer); return; }
#else
    (void)aligned;
#endif
    std::free(pointer);
}
inline Stats snapshot() { Lock lock; return stats; }
struct Scope
{
    Scope()
    {
        Lock lock;
        if (enabled) std::abort();
        stats.calls = stats.requested = 0; stats.baseline = stats.peak = stats.live;
        enabled = true;
    }
    ~Scope() { stop(); }
    void stop() { Lock lock; enabled = false; beforeAllocation.store(nullptr); allocationContext.store(nullptr); }
};
#else
inline Stats snapshot() { return {}; }
struct Scope { void stop() {} };
#endif
}
#if FS_SAMPLING_ALLOCATION_PROBE
void* operator new(size_t bytes) { return FSSamplingAlloc::allocate(bytes); }
void* operator new[](size_t bytes) { return FSSamplingAlloc::allocate(bytes); }
void* operator new(size_t bytes, std::align_val_t alignment) { return FSSamplingAlloc::allocate(bytes, size_t(alignment)); }
void* operator new[](size_t bytes, std::align_val_t alignment) { return FSSamplingAlloc::allocate(bytes, size_t(alignment)); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { try { return ::operator new(bytes); } catch (...) { return nullptr; } }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { try { return ::operator new[](bytes); } catch (...) { return nullptr; } }
void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{ try { return ::operator new(bytes, alignment); } catch (...) { return nullptr; } }
void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{ try { return ::operator new[](bytes, alignment); } catch (...) { return nullptr; } }
void operator delete(void* pointer) noexcept { FSSamplingAlloc::release(pointer); }
void operator delete[](void* pointer) noexcept { FSSamplingAlloc::release(pointer); }
void operator delete(void* pointer, size_t) noexcept { FSSamplingAlloc::release(pointer); }
void operator delete[](void* pointer, size_t) noexcept { FSSamplingAlloc::release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { FSSamplingAlloc::release(pointer, true); }
void operator delete[](void* pointer, std::align_val_t) noexcept { FSSamplingAlloc::release(pointer, true); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { FSSamplingAlloc::release(pointer, true); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { FSSamplingAlloc::release(pointer, true); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { FSSamplingAlloc::release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { FSSamplingAlloc::release(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { FSSamplingAlloc::release(pointer, true); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { FSSamplingAlloc::release(pointer, true); }
#endif
#endif
