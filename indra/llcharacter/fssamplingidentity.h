#ifndef FS_SAMPLING_IDENTITY_H
#define FS_SAMPLING_IDENTITY_H
#include <atomic>
#include <limits>
#include <type_traits>

namespace FSSampling
{
// Zero is permanently unavailable after exhaustion; never reuse a valid token.
template<class UInt> UInt takeIdentity(std::atomic<UInt>& next) noexcept
{
    static_assert(std::is_unsigned_v<UInt> && std::numeric_limits<UInt>::digits == 64);
    UInt value = next.load(std::memory_order_relaxed);
    while (value)
    {
        const UInt following = value == std::numeric_limits<UInt>::max() ? 0 : value + 1;
        if (next.compare_exchange_weak(value, following, std::memory_order_relaxed)) return value;
    }
    return 0;
}
template<class UInt> void advanceGeneration(UInt& value) noexcept
{
    static_assert(std::is_unsigned_v<UInt> && std::numeric_limits<UInt>::digits == 64);
    if (value) value = value == std::numeric_limits<UInt>::max() ? 0 : value + 1;
}
}
#endif
