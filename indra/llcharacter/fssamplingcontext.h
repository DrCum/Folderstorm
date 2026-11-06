#ifndef FS_SAMPLING_CONTEXT_H
#define FS_SAMPLING_CONTEXT_H

#include <cstdint>

namespace FSSampling
{
// Owned semantic controls only. Sticky exception flags are not query identity.
// The initial contract is native MSVC x64, nearest rounding, masked exceptions.
class NumericContext
{
    friend class ScopedNumericContext;
public:
    NumericContext() = default;
    static NumericContext capture() noexcept;
    bool isSupported() const noexcept { return mControls != 0; }
    bool operator==(const NumericContext& other) const noexcept { return mControls == other.mControls; }
private:
    explicit NumericContext(std::uint32_t controls): mControls(controls) {}
    std::uint32_t mControls = 0;
};

// Save the complete worker MXCSR, establish owned controls with clear status,
// then restore the exact prior worker controls/status, including on early return.
class ScopedNumericContext
{
public:
    explicit ScopedNumericContext(const NumericContext& context) noexcept;
    ~ScopedNumericContext() noexcept;
    ScopedNumericContext(const ScopedNumericContext&) = delete;
    ScopedNumericContext& operator=(const ScopedNumericContext&) = delete;
    bool isActive() const noexcept { return mActive; }
private:
    std::uint32_t mPrevious = 0;
    bool mRestore = false;
    bool mActive = false;
};
}

#endif
