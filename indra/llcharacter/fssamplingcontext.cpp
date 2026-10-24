#include "fssamplingcontext.h"

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__) && \
    !defined(__INTEL_COMPILER) && !defined(_M_ARM64EC) && \
    !defined(FS_TEST_SAMPLING_CONTEXT_UNSUPPORTED)
#include <immintrin.h>
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#define FS_NATIVE_SAMPLING_CONTEXT 1
#else
#define FS_NATIVE_SAMPLING_CONTEXT 0
#endif

FSSampling::NumericContext FSSampling::NumericContext::capture() noexcept
{
#if FS_NATIVE_SAMPLING_CONTEXT
    const unsigned controls = _mm_getcsr() &
        (_MM_MASK_MASK | _MM_ROUND_MASK | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);
    // The existing curve code is built precise and assumes nearest rounding.
    // Other owner modes use the existing direct path instead of a false guarantee.
    if ((controls & (_MM_MASK_MASK | _MM_ROUND_MASK)) == _MM_MASK_MASK)
        return NumericContext(controls);
#endif
    return {};
}

FSSampling::ScopedNumericContext::ScopedNumericContext(const NumericContext& context) noexcept
{
#if FS_NATIVE_SAMPLING_CONTEXT
    mPrevious = _mm_getcsr();
    mRestore = true;
    if (context.isSupported())
    {
        _mm_setcsr(context.mControls);
        mActive = _mm_getcsr() == context.mControls;
        if (!mActive) _mm_setcsr(mPrevious);
    }
#else
    (void)context;
#endif
}

FSSampling::ScopedNumericContext::~ScopedNumericContext() noexcept
{
#if FS_NATIVE_SAMPLING_CONTEXT
    if (mRestore) _mm_setcsr(mPrevious);
#endif
}

#if FS_NATIVE_SAMPLING_CONTEXT
#pragma fenv_access(off)
#pragma float_control(pop)
#endif
