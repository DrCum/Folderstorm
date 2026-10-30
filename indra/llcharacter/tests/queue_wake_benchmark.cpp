// Opt-in, isolated pool measurement. Does not change the viewer's scheduler.
#include "linden_common.h"
#include "threadpool.h"
#include <boost/fiber/algo/round_robin.hpp>
#include <boost/fiber/operations.hpp>
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if LL_WINDOWS
#include <mmsystem.h>
#endif

namespace QueueWakeBenchmark
{
using Clock = std::chrono::steady_clock;
class MeasuredPool : public LL::ThreadPool
{
public:
    using LL::ThreadPool::ThreadPool;
    U64 cpuTicks()
    {
        U64 total = 0;
#if LL_WINDOWS
        for (auto& worker : mThreads)
        {
            FILETIME created, exited, kernel, user;
            if (!GetThreadTimes(worker.second.native_handle(), &created, &exited, &kernel, &user))
                throw std::runtime_error("GetThreadTimes failed");
            total += (U64(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
            total += (U64(user.dwHighDateTime) << 32) | user.dwLowDateTime;
        }
#endif
        return total;
    }
};
class EventPool final : public MeasuredPool
{
public:
    using MeasuredPool::MeasuredPool;
    ~EventPool() override { close(); }
    void run() override
    {
        boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();
        MeasuredPool::run();
    }
};
struct TimerPeriod
{
    U32 period = 0;
    TimerPeriod()
    {
#if LL_WINDOWS
        TIMECAPS caps;
        if (timeGetDevCaps(&caps, sizeof(caps)) == TIMERR_NOERROR)
        {
            const U32 requested = std::max<U32>(caps.wPeriodMin, std::min<U32>(1, caps.wPeriodMax));
            if (timeBeginPeriod(requested) == TIMERR_NOERROR) period = requested;
        }
#endif
    }
    ~TimerPeriod()
    {
#if LL_WINDOWS
        if (period) timeEndPeriod(period);
#endif
    }
};
template<class Pool> void measure(const char* name, const std::string& timer)
{
    Pool pool(name, 1, 4, false);
    pool.start();
    if (pool.getWidth() != 1) throw std::runtime_error("Test pool width override: use exactly one worker");
    std::vector<double> values;
    constexpr unsigned samples = 64;
    for (unsigned i = 0; i < samples; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto complete = std::make_shared<std::promise<Clock::time_point>>();
        auto ready = complete->get_future();
        const auto begin = Clock::now();
        // Known-empty private queue, nonblocking admission even for this probe.
        if (!pool.getQueue().tryPost([complete] { complete->set_value(Clock::now()); }))
            throw std::runtime_error("Probe admission rejected; rerun rather than biasing by silently dropping it");
        if (ready.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            throw std::runtime_error("Probe callback timeout");
        values.push_back(std::chrono::duration<double, std::micro>(ready.get() - begin).count());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto cpuStart = pool.cpuTicks();
    const auto idleStart = Clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto cpuElapsed = pool.cpuTicks() - cpuStart;
    const auto idleWall = std::chrono::duration<double, std::milli>(Clock::now() - idleStart).count();
    pool.close(); // Always drain/join through the pool, never externally close its queue first.
    std::sort(values.begin(), values.end());
    auto percentile = [&](unsigned p) { return values[(p * samples + 99) / 100 - 1]; };
    std::cout << name << ",timer=" << timer << ",workers=" << pool.getWidth() << ",samples=" << samples
              << ",min_us=" << values.front() << ",median_us=" << percentile(50)
              << ",p95_us=" << percentile(95) << ",p99_us=" << percentile(99) << ",max_us=" << values.back() << '\n';
    std::cout << name << ",idle_wall_ms=" << idleWall;
#if LL_WINDOWS
    std::cout << ",idle_thread_cpu_ms=" << cpuElapsed / 10000. << ",idle_core_percent=" << cpuElapsed / 100. / idleWall;
#else
    (void)cpuElapsed;
    std::cout << ",idle_thread_cpu_ms=unavailable,idle_core_percent=unavailable";
#endif
    std::cout << '\n';
}
int run()
{
    std::cout << std::fixed << std::setprecision(3)
              << "Isolated wake probe: 64 callbacks after 5ms requested idle, 2s requested idle CPU sample.\n"
              << "CPU counters can quantize short samples to zero; no FPS or sustained-load inference.\n";
    measure<MeasuredPool>("FSWakeCurrentDefault", "default");
    measure<EventPool>("FSWakeStockDefault", "default");
    TimerPeriod timer;
    std::cout << "Explicit timer-period request accepted_ms=" << timer.period << '\n';
    const auto context = timer.period ? "requested" + std::to_string(timer.period) + "ms" : "request_unavailable";
    measure<MeasuredPool>("FSWakeCurrentTimer", context);
    measure<EventPool>("FSWakeStockTimer", context);
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc != 2 || std::string(argv[1]) != "--benchmark")
    {
        std::cout << "Opt in with --benchmark. Runs private pools only; viewer unchanged.\n";
        return argc == 1 ? 0 : 1;
    }
    return QueueWakeBenchmark::run();
}
