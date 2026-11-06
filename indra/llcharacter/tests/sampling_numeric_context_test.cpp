#include "fssamplingcontext.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#endif

namespace SamplingNumericTest
{
unsigned checks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
int run()
{
    using FSSampling::NumericContext;
    using FSSampling::ScopedNumericContext;
    require(!NumericContext{}.isSupported(), "Uncaptured numeric context is supported");
    if (!NumericContext::capture().isSupported())
    {
        ScopedNumericContext invalid(NumericContext{});
        require(!invalid.isActive(), "Unsupported platform established a context");
        std::cout << "Unsupported numeric-context contract rejects owned sampling; platform math checks not run\n";
        return 0;
    }
#if defined(_MSC_VER) && defined(_M_X64)
    const unsigned original = _mm_getcsr();
    constexpr unsigned DENORMALS = _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK;
    std::array<NumericContext, 4> contexts;
    const unsigned denormals[] = {0, _MM_FLUSH_ZERO_MASK, _MM_DENORMALS_ZERO_MASK, DENORMALS};
    for (unsigned i = 0; i < contexts.size(); ++i)
    {
        _mm_setcsr(_MM_MASK_MASK | denormals[i]);
        contexts[i] = NumericContext::capture();
        require(contexts[i].isSupported(), "Supported nearest/masked owner mode rejected");
        _mm_setcsr(_MM_MASK_MASK | denormals[i] | _MM_EXCEPT_MASK);
        require(contexts[i] == NumericContext::capture(), "Sticky status changed semantic query identity");
        if (i) require(!(contexts[0] == contexts[i]), "Different denormal controls compare equal");
    }
    _mm_setcsr(original);
    std::thread worker([&]
    {
        const unsigned worker_original = _mm_getcsr();
        for (unsigned owner = 0; owner < contexts.size(); ++owner)
            for (unsigned rounding : {unsigned(_MM_ROUND_NEAREST), unsigned(_MM_ROUND_DOWN), unsigned(_MM_ROUND_UP), unsigned(_MM_ROUND_TOWARD_ZERO)})
                for (unsigned denormal : denormals)
                    for (unsigned status : {0u, 1u, 2u, 0x3fu})
                    {
                        const unsigned previous = _MM_MASK_MASK | rounding | denormal | status;
                        _mm_setcsr(previous);
                        {
                            ScopedNumericContext guard(contexts[owner]);
                            require(guard.isActive(), "Valid owned context not established");
                            require(_mm_getcsr() == (_MM_MASK_MASK | denormals[owner]), "Owned controls/status not established exactly");
                            _mm_setcsr(_mm_getcsr() | _MM_EXCEPT_MASK); // Simulate flags raised by the job.
                        }
                        require(_mm_getcsr() == previous, "Worker controls/status leaked across sampling jobs");
                    }
        const unsigned prior = _MM_MASK_MASK | _MM_ROUND_UP | _MM_FLUSH_ZERO_MASK | _MM_EXCEPT_MASK;
        _mm_setcsr(prior);
        try
        {
            ScopedNumericContext guard(contexts[0]);
            require(guard.isActive(), "Exception-path context not established");
            throw std::runtime_error("test unwind");
        }
        catch (const std::runtime_error&) {}
        require(_mm_getcsr() == prior, "Exception unwind lost the prior worker context");
        {
            ScopedNumericContext invalid(NumericContext{});
            require(!invalid.isActive() && _mm_getcsr() == prior, "Rejected descriptor changed worker controls");
        }
        require(_mm_getcsr() == prior, "Rejected descriptor did not preserve worker status");
        _mm_setcsr(worker_original);
    });
    worker.join();
    require(_mm_getcsr() == original, "Worker test changed the owner context");
    for (unsigned rounding : {unsigned(_MM_ROUND_DOWN), unsigned(_MM_ROUND_UP), unsigned(_MM_ROUND_TOWARD_ZERO)})
    {
        _mm_setcsr(_MM_MASK_MASK | rounding);
        const bool rejected = !NumericContext::capture().isSupported();
        _mm_setcsr(original); require(rejected, "Unsupported owner rounding accepted");
    }
    _mm_setcsr(_MM_MASK_MASK & ~_MM_MASK_INVALID);
    const bool rejected = !NumericContext::capture().isSupported();
    _mm_setcsr(original); require(rejected, "Unmasked owner exception accepted");
    {
        ScopedNumericContext guard(contexts[1]);
        require(guard.isActive(), "Same-thread use failed");
    }
    require(_mm_getcsr() == original, "Same-thread use changed caller controls/status");
    std::cout << checks << " numeric-context checks across 256 reused-worker mode/status cases passed\n";
#endif
    return 0;
}
}
int main() { return SamplingNumericTest::run(); }
#if defined(_MSC_VER) && defined(_M_X64)
#pragma fenv_access(off)
#pragma float_control(pop)
#endif
