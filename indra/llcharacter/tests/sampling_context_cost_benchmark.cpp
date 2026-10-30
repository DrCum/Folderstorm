// Opt-in context-only timing. Link the actual guard as a separate object.
#include "fssamplingcontext.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#pragma float_control(precise, on, push)
#pragma fenv_access(on)
#endif

namespace SamplingContextCost
{
#if defined(_MSC_VER) && defined(_M_X64)
volatile unsigned observable = 1;
__declspec(noinline) unsigned baseline() { return observable; }
__declspec(noinline) unsigned capture()
{ return FSSampling::NumericContext::capture().isSupported() ? observable : 0; }
__declspec(noinline) unsigned guarded(const FSSampling::NumericContext& context)
{ FSSampling::ScopedNumericContext scope(context); return scope.isActive() ? observable : 0; }
struct Row
{
    const char* stage;
    unsigned sample;
    std::int64_t ns;
    std::uint64_t checksum;
};
constexpr unsigned ITERATIONS = 2000000;
template<class Function> Row measure(const char* stage, unsigned sample, Function call)
{
    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < ITERATIONS; ++i) checksum += call();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    if (checksum != ITERATIONS) std::abort();
    return {stage, sample, ns, checksum};
}
#endif
int run()
{
    if (!FSSampling::NumericContext::capture().isSupported())
    { std::cerr << "Owned numeric context unsupported; timing unavailable\n"; return 1; }
#if defined(_MSC_VER) && defined(_M_X64)
    const unsigned caller = _mm_getcsr();
    const unsigned owner = _MM_MASK_MASK | _MM_FLUSH_ZERO_ON;
    const unsigned worker_mode = _MM_MASK_MASK | _MM_ROUND_UP | _MM_DENORMALS_ZERO_MASK | _MM_EXCEPT_MASK;
    _mm_setcsr(owner);
    const auto context = FSSampling::NumericContext::capture();
    std::vector<Row> owner_rows, worker_rows;
    owner_rows.reserve(18); worker_rows.reserve(18);
    // Alternate order; observed function calls prevent dead-code removal.
    for (unsigned sample = 0; sample < 9; ++sample)
    {
        auto plain = [&] { owner_rows.push_back(measure("baseline_owner", sample, baseline)); };
        auto read = [&] { owner_rows.push_back(measure("capture_owner", sample, capture)); };
        if (sample % 2) { read(); plain(); } else { plain(); read(); }
    }
    std::thread worker([&]
    {
        const unsigned previous = _mm_getcsr();
        for (unsigned sample = 0; sample < 9; ++sample)
        {
            // Keep allocation/result bookkeeping outside the deliberately altered mode.
            Row plain{}, scoped{};
            _mm_setcsr(worker_mode);
            if (sample % 2)
            {
                scoped = measure("guard_worker", sample, [&] { return guarded(context); });
                plain = measure("baseline_worker", sample, baseline);
            }
            else
            {
                plain = measure("baseline_worker", sample, baseline);
                scoped = measure("guard_worker", sample, [&] { return guarded(context); });
            }
            const bool preserved = _mm_getcsr() == worker_mode;
            _mm_setcsr(previous);
            if (!preserved) std::abort();
            worker_rows.push_back(plain); worker_rows.push_back(scoped);
        }
    });
    worker.join();
    _mm_setcsr(caller);
    std::cout << "meta,iterations," << ITERATIONS << ",samples,9,owner_mxcsr," << std::hex << owner
              << ",worker_mxcsr," << worker_mode << std::dec << ",separate_guard_object,1\n"
              << "stage,sample,iterations,elapsed_ns,checksum\n";
    for (const auto& rows : {&owner_rows, &worker_rows})
        for (const auto& row : *rows)
            std::cout << row.stage << ',' << row.sample << ',' << ITERATIONS << ',' << row.ns << ',' << row.checksum << '\n';
#endif
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--benchmark") return SamplingContextCost::run();
    std::cout << "Opt in with --benchmark. Context-only timings are not viewer FPS.\n";
    return argc == 1 ? 0 : 1;
}
#if defined(_MSC_VER) && defined(_M_X64)
#pragma fenv_access(off)
#pragma float_control(pop)
#endif
