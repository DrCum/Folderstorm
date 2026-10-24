// Explicit opt-in. Owned batching and independent current-state validation only;
// no viewer playback, settings, General pool or production scheduler changes.
#include "linden_common.h"
#include "llmath.h"
#include "llsimdmath.h"
#include "sampling_cost_fixture.h"
#include "threadpool.h"
#include "sampling_allocation_probe.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#if LL_WINDOWS
#include <mmsystem.h>
#endif

namespace SamplingCostBenchmark
{
using namespace FSSamplingCost;
using Status = Executor::Status;
std::atomic<U64> sink{0};
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void busy(unsigned iterations)
{
    U64 value = 0x465342454e4348;
    for (unsigned i = 0; i < iterations; ++i) { value ^= value >> 7; value = value * 1664525 + i + 1013904223; }
    sink.store(value, std::memory_order_relaxed);
}
unsigned overlapIterations(unsigned microseconds)
{
    if (!microseconds) return 0;
    const auto begin = Clock::now(); busy(200000);
    return static_cast<unsigned>(200000. * microseconds / std::max(1., micros(Clock::now() - begin)));
}
double mainCpu()
{
#if LL_WINDOWS
    FILETIME created, exited, kernel, user;
    require(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user), "GetThreadTimes(main) failed");
    return (((U64(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
            ((U64(user.dwHighDateTime) << 32) | user.dwLowDateTime)) / 10000.;
#else
    return std::numeric_limits<double>::quiet_NaN();
#endif
}
class Pool final : public LL::ThreadPool
{
#if LL_WINDOWS
    std::vector<HANDLE> handles;
#endif
public:
    using LL::ThreadPool::ThreadPool;
    ~Pool() override
    {
        close();
#if LL_WINDOWS
        for (auto handle : handles) CloseHandle(handle);
#endif
    }
    void startMeasured()
    {
        start();
#if LL_WINDOWS
        for (auto& worker : mThreads)
        {
            HANDLE copy;
            require(DuplicateHandle(GetCurrentProcess(), worker.second.native_handle(), GetCurrentProcess(),
                                    &copy, 0, false, DUPLICATE_SAME_ACCESS), "DuplicateHandle(worker) failed");
            handles.push_back(copy);
        }
#endif
    }
    double cpu() const
    {
#if LL_WINDOWS
        U64 ticks = 0;
        for (auto handle : handles)
        {
            FILETIME created, exited, kernel, user;
            require(GetThreadTimes(handle, &created, &exited, &kernel, &user), "GetThreadTimes(worker) failed");
            ticks += ((U64(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
                     ((U64(user.dwHighDateTime) << 32) | user.dwLowDateTime);
        }
        return ticks / 10000.;
#else
        return std::numeric_limits<double>::quiet_NaN();
#endif
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
            const U32 request = std::max<U32>(caps.wPeriodMin, std::min<U32>(1, caps.wPeriodMax));
            if (timeBeginPeriod(request) == TIMERR_NOERROR) period = request;
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
struct Completion
{
    std::mutex mutex;
    std::condition_variable changed;
    void signal() { std::lock_guard lock(mutex); changed.notify_all(); }
};
enum class Mode { Direct, Owned, Barrier, Wait };
const char* name(Mode mode)
{
    switch (mode) { case Mode::Direct: return "direct"; case Mode::Owned: return "owned";
                    case Mode::Barrier: return "barrier"; default: return "wait"; }
}
enum Stage { Total, Prepare, Submit, Wait, Validate, Apply, Fallback, Overlap, StageCount };
const char* stageNames[] = {"total", "prepare", "submit", "wait", "validate", "apply", "fallback", "overlap"};
struct Frame
{
    std::array<double, StageCount> us{};
    U64 accepted = 0, rejected = 0, ready = 0, fallbackMotions = 0, cancelled = 0, abandoned = 0,
        lateDiscard = 0, failed = 0, stale = 0, usedJoints = 0;
};
struct Summary
{
    Config config;
    Mode mode;
    unsigned workers, slots, cold, overlap, samples, contention;
    std::vector<Frame> frames;
    std::vector<double> wake, sample;
    size_t occupiedHigh = 0, queuedHigh = 0, payloadCapacityHigh = 0;
    U64 sampledJoints = 0;
    double mainMs = 0, workerMs = 0, windowMs = 0;
    FSSamplingAlloc::Stats warmAlloc, endAlloc;
};
std::array<double, 4> distribution(std::vector<double> values)
{
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    auto p = [&](unsigned percentile) { return values[(percentile * values.size() + 99) / 100 - 1]; };
    return {p(50), p(95), p(99), values.back()};
}
Summary trial(Config config, Mode mode, unsigned workers, unsigned slots, unsigned samples,
              unsigned cold, unsigned overlap, unsigned contention)
{
    constexpr unsigned warmup = 8;
    Fixture fixture(config);
    const unsigned groups = (config.motions + config.batch - 1) / config.batch;
    const bool queued = mode == Mode::Wait || mode == Mode::Barrier;
    Summary result{config, mode, workers, slots, cold, overlap, samples, contention};
    result.frames.reserve(samples);
    // Fixed, owned observation window; no worker captures live scene/state buffers.
    // Trace allocations are harness storage, deliberately outside the allocation scope.
    std::vector<std::shared_ptr<Trace>> traces;
    if (mode != Mode::Direct)
    {
        traces.reserve((warmup + samples) * groups);
        for (unsigned i = 0; i < (warmup + samples) * groups; ++i) traces.push_back(std::make_shared<Trace>());
    }
    FSSamplingAlloc::Scope allocationScope;
    std::vector<BatchRequest> inputs(mode == Mode::Direct ? 0 : groups);
    std::vector<BatchResult> outputs(mode == Mode::Direct ? 0 : groups);
    std::vector<std::optional<Executor::Ticket>> tickets(mode == Mode::Direct ? 0 : groups);
    std::vector<Executor::Ticket> outstanding;
    outstanding.reserve(slots);
    auto completion = mode == Mode::Wait ? std::make_shared<Completion>() : nullptr;
    std::unique_ptr<Pool> pool;
    std::unique_ptr<Executor> executor;
    if (queued)
    {
        pool = std::make_unique<Pool>("FSSamplingCostPrivate", workers, 1024 * 1024, false);
        pool->startMeasured();
        require(pool->getWidth() == workers, "Pool width override would invalidate the comparison");
        Executor::Submit submitter = FSSamplingTest::queueSubmitter(pool->getQueue().getWeak(), workers);
        if (mode == Mode::Wait)
        {
            // Test-only completion wait includes this wrapper's allocation/notify cost.
            // Same-frame barrier mode uses the unwrapped real tryPost adapter.
            const auto destination = pool->getQueue().getWeak();
            submitter = [destination, completion, workers](const Executor::Work& work)
            {
                if (!workers) return false;
                auto live = destination.lock();
                return live && live->tryPost([work, completion] { work(); completion->signal(); });
            };
        }
        executor = std::make_unique<Executor>(slots, std::move(submitter));
    }
    double cpuBegin = 0, workerBegin = 0;
    Clock::time_point window;
    for (unsigned index = 0; index < warmup + samples; ++index)
    {
        if (index == warmup)
        {
            // Test-only warm-up drain outside all measured frames. Do not charge
            // old warm-up work to measured CPU/counts or reuse an in-flight trace.
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            while (executor && std::any_of(outstanding.begin(), outstanding.end(), [&](const auto& ticket)
                       { return executor->status(ticket) != Status::Expired; }))
            {
                require(Clock::now() < deadline, "Warm-up acknowledgement timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            outstanding.clear();
            result.warmAlloc = FSSamplingAlloc::snapshot();
            cpuBegin = mainCpu(); workerBegin = pool ? pool->cpu() : 0; window = Clock::now();
        }
        if (cold) std::this_thread::sleep_for(std::chrono::milliseconds(cold));
        Frame frame;
        const unsigned currentFrame = index + 1;
        const auto begin = Clock::now();
        if (mode != Mode::Direct)
        {
            fixture.prepareCurrent(currentFrame);
            for (unsigned g = 0; g < groups; ++g)
            {
                const unsigned first = g * config.batch, count = std::min(config.batch, config.motions - first);
                fixture.prepareBatch(inputs[g], first, count, traces[index * groups + g]);
            }
            frame.us[Prepare] = micros(Clock::now() - begin);
            const auto submitBegin = Clock::now();
            for (unsigned g = 0; g < groups; ++g)
            {
                inputs[g].trace->queued = Clock::now();
                if (mode == Mode::Owned)
                    require(BatchSampler::sample(std::move(inputs[g]), outputs[g]), "Serial owned sample failed");
                else
                {
                    tickets[g] = executor->trySubmit(inputs[g]);
                    trackAdmission(*executor, outstanding, tickets[g]);
                    if (tickets[g]) ++frame.accepted;
                    else ++frame.rejected;
                    result.occupiedHigh = std::max(result.occupiedHigh, outstanding.size());
                    size_t queuedNow = 0;
                    for (const auto& ticket : outstanding) if (executor->status(ticket) == Status::Queued) ++queuedNow;
                    result.queuedHigh = std::max(result.queuedHigh, queuedNow);
                }
            }
            frame.us[Submit] = micros(Clock::now() - submitBegin);
        }
        if (mode == Mode::Direct)
        {
            const auto applyBegin = Clock::now(); fixture.direct(currentFrame, 0, config.motions);
            frame.us[Apply] = micros(Clock::now() - applyBegin);
        }
        const auto overlapBegin = Clock::now(); busy(overlap);
        frame.us[Overlap] = micros(Clock::now() - overlapBegin);
        if (mode == Mode::Wait)
        {
            const auto waitBegin = Clock::now();
            std::unique_lock lock(completion->mutex);
            const bool completed = completion->changed.wait_until(lock, Clock::now() + std::chrono::seconds(5), [&]
            {
                for (const auto& ticket : tickets) if (ticket)
                {
                    const auto status = executor->status(*ticket);
                    if (status == Status::Queued || status == Status::Running) return false;
                }
                return true;
            });
            require(completed, "Explicit benchmark completion timeout (never a playback wait)");
            frame.us[Wait] = micros(Clock::now() - waitBegin);
        }
        if (mode != Mode::Direct) for (unsigned g = 0; g < groups; ++g)
        {
            const unsigned first = g * config.batch, count = std::min(config.batch, config.motions - first);
            const auto validateBegin = Clock::now();
            Status status = mode == Mode::Owned ? Status::Ready :
                (tickets[g] ? executor->take(*tickets[g], outputs[g]) : Status::Expired);
            const bool usable = status == Status::Ready && fixture.validate(outputs[g], first, count);
            if (status == Status::Ready && !usable) ++frame.stale;
            frame.us[Validate] += micros(Clock::now() - validateBegin);
            if (usable)
            {
                const auto applyBegin = Clock::now(); fixture.apply(outputs[g], first);
                inputs[g] = outputs[g].recycle();
                frame.us[Apply] += micros(Clock::now() - applyBegin);
                ++frame.ready; frame.usedJoints += count * config.joints;
            }
            else
            {
                const auto fallbackBegin = Clock::now();
                if (tickets[g] && status != Status::Ready && status != Status::Failed)
                {
                    const auto cancelled = executor->cancel(*tickets[g]);
                    if (cancelled == Status::Queued) ++frame.cancelled;
                    if (cancelled == Status::Running) ++frame.abandoned;
                    if (cancelled == Status::Ready) ++frame.lateDiscard;
                }
                if (status == Status::Failed) ++frame.failed;
                fixture.direct(currentFrame, first, count);
                if (status == Status::Ready) inputs[g] = outputs[g].recycle();
                frame.fallbackMotions += count;
                frame.us[Fallback] += micros(Clock::now() - fallbackBegin);
            }
        }
        frame.us[Total] = micros(Clock::now() - begin);
        // Make all frames observable outside timing without running the oracle inside CPU totals.
        U32 bits; std::memcpy(&bits, fixture.states[0][0]->getPosition().mV, sizeof(bits));
        sink.store(bits, std::memory_order_relaxed);
        if (index >= warmup) result.frames.push_back(frame);
    }
    if (executor) executor->close();
    if (pool) pool->close(); // Drain/discard and join before inspecting trace fields.
    result.mainMs = mainCpu() - cpuBegin; result.workerMs = pool ? pool->cpu() - workerBegin : 0;
    result.windowMs = micros(Clock::now() - window) / 1000.;
    result.endAlloc = FSSamplingAlloc::snapshot(); allocationScope.stop();
    for (unsigned i = warmup * groups; i < traces.size(); ++i)
    {
        const auto& trace = *traces[i];
        if (!trace.sampled) continue;
        result.wake.push_back(micros(trace.started - trace.queued));
        result.sample.push_back(micros(trace.finished - trace.started));
        result.sampledJoints += trace.joints;
        result.payloadCapacityHigh = std::max(result.payloadCapacityHigh, trace.payloadCapacityBytes);
    }
    // A fresh independently updated serial fixture verifies final pose/metadata outside measurement.
    Fixture expected(config); expected.direct(warmup + samples, 0, config.motions);
    require(fixture.exact(expected), "Complete-cost trial produced a different final pose");
    return result;
}
void print(const Summary& value)
{
    auto row = [&](const char* stage, const std::vector<double>& values)
    {
        const auto data = distribution(values);
        std::cout << "stage," << name(value.mode) << ',' << value.config.motions << ',' << value.config.joints << ','
                  << value.config.keys << ',' << value.config.batch << ',' << value.config.seek << ',' << value.workers << ','
                  << value.slots << ',' << value.cold << ',' << value.overlap << ',' << value.contention << ',' << stage << ',' << values.size();
        for (auto number : data) std::cout << ',' << number;
        std::cout << '\n';
    };
    Frame total;
    for (const auto& frame : value.frames)
    {
        total.accepted += frame.accepted; total.rejected += frame.rejected; total.ready += frame.ready;
        total.fallbackMotions += frame.fallbackMotions; total.cancelled += frame.cancelled; total.abandoned += frame.abandoned;
        total.lateDiscard += frame.lateDiscard; total.failed += frame.failed; total.stale += frame.stale; total.usedJoints += frame.usedJoints;
    }
    if (!FS_SAMPLING_ALLOCATION_PROBE) for (unsigned stage = 0; stage < StageCount; ++stage)
    {
        std::vector<double> times;
        for (const auto& frame : value.frames) times.push_back(frame.us[stage]);
        row(stageNames[stage], times);
    }
    if (!FS_SAMPLING_ALLOCATION_PROBE) { row("dispatch_to_sample", value.wake); row("batch_sample", value.sample); }
    std::cout << "counts," << name(value.mode) << ',' << value.config.motions << ',' << value.config.joints << ','
              << value.config.keys << ',' << value.config.batch << ',' << value.config.seek << ',' << value.workers << ','
              << value.slots << ',' << value.cold << ',' << value.overlap << ',' << value.contention << ',' << value.samples << ','
              << total.accepted << ',' << total.rejected << ',' << total.ready << ',' << total.fallbackMotions << ','
              << total.cancelled << ',' << total.abandoned << ',' << total.lateDiscard << ',' << total.failed << ',' << total.stale << ','
              << value.occupiedHigh << ',' << value.queuedHigh << ',' << value.sampledJoints << ','
              << value.sampledJoints - total.usedJoints << ',' << value.payloadCapacityHigh << ','
              << (FS_SAMPLING_ALLOCATION_PROBE ? -1 : value.mainMs) << ','
              << (FS_SAMPLING_ALLOCATION_PROBE ? -1 : value.workerMs) << ','
              << (FS_SAMPLING_ALLOCATION_PROBE ? -1 : value.windowMs) << '\n';
    if (FS_SAMPLING_ALLOCATION_PROBE)
        std::cout << "alloc," << name(value.mode) << ',' << value.config.motions << ',' << value.config.joints << ','
                  << value.config.keys << ',' << value.config.batch << ',' << value.workers << ',' << value.slots << ','
                  << value.samples << ',' << value.endAlloc.calls << ',' << value.endAlloc.requested << ','
                  << value.endAlloc.calls - value.warmAlloc.calls << ',' << value.endAlloc.requested - value.warmAlloc.requested << ','
                  << value.endAlloc.baseline << ',' << value.endAlloc.live << ',' << value.endAlloc.peak << '\n';
}
void comparisons(Config config, unsigned samples, unsigned cold, unsigned overlap, unsigned contention, bool reverse = false)
{
    const unsigned groups = (config.motions + config.batch - 1) / config.batch;
    std::array<std::pair<Mode, unsigned>, 6> order = {{{Mode::Direct, 0}, {Mode::Owned, 0}, {Mode::Barrier, 1},
                                                    {Mode::Wait, 1}, {Mode::Barrier, 2}, {Mode::Wait, 2}}};
    if (reverse) std::reverse(order.begin(), order.end());
    for (auto [mode, workers] : order)
        print(trial(config, mode, workers, workers ? groups : 0, samples, cold, overlap, contention));
}
void header(unsigned period, unsigned samples, bool reverse, const char* dataset)
{
    std::cout << std::fixed << std::setprecision(3)
              << "meta,dataset," << dataset << ",accepted_timer_ms," << period << ",storage_contiguous," << FS_CONTIGUOUS_ANIMATION_KEYS
              << ",samples," << samples << ",warmup,8,reverse_order," << reverse
              << ",current_scheduler,private_pools_only,owner_mode,LLVector4a_initClass\n"
              << "stage_header,mode,motions,joints,keys,batch,seek,workers,slots,cold_ms,overlap_iterations,contention_threads,stage,n,p50_us,p95_us,p99_us,max_us\n"
              << "counts_header,mode,motions,joints,keys,batch,seek,workers,slots,cold_ms,overlap_iterations,contention_threads,samples,accepted,rejected,ready_jobs,fallback_motions,cancelled_queued,abandoned_running,discarded_ready,failed,stale,observed_slots_high,observed_queued_high,sampled_joints,wasted_sampled_joints,max_batch_payload_capacity_bytes,main_cpu_ms,worker_cpu_ms,window_ms\n";
}
int cpuRun()
{
    TimerPeriod timer;
    constexpr unsigned samples = 4096;
    header(timer.period, samples, false, "cpu");
    const unsigned overlap = overlapIterations(2000);
    for (Config config : {Config{32, 64, 32, 8, false}, Config{64, 128, 32, 8, true}})
        for (unsigned work : {0u, overlap})
        {
            print(trial(config, Mode::Direct, 0, 0, samples, 0, work, 0));
            print(trial(config, Mode::Barrier, 2, (config.motions + config.batch - 1) / config.batch, samples, 0, work, 0));
        }
    return 0;
}
int run(bool smoke, bool reverse)
{
    TimerPeriod timer;
    header(timer.period, smoke ? 8 : 128, reverse, "timing");
    if (smoke) { comparisons({9, 17, 32, 8, true}, 8, 0, 0, 0, reverse); return 0; }
    for (Config config : {Config{8, 20, 1, 1, false}, Config{32, 64, 32, 8, false},
                          Config{32, 64, 128, 8, true}, Config{64, 128, 32, 8, true}})
        for (unsigned batch : {1u, 8u, 32u}) { config.batch = batch; comparisons(config, 128, 0, 0, 0, reverse); }
    const unsigned overlap = overlapIterations(2000);
    for (Config config : {Config{32, 64, 32, 8, false}, Config{64, 128, 32, 8, true}})
        for (unsigned cold : {0u, 5u}) comparisons(config, 128, cold, overlap, 0, reverse);
    comparisons({32, 64, 32, 8, false}, 128, 5, 0, 0, reverse);
    for (unsigned workers : {0u, 1u, 2u}) for (unsigned work : {0u, overlap})
        print(trial({32, 64, 32, 8, false}, Mode::Barrier, workers, 1, 128, 0, work, 0));
    std::atomic<bool> stop{false};
    std::vector<std::thread> load;
    for (unsigned i = 0; i < 2; ++i) load.emplace_back([&] { while (!stop.load(std::memory_order_relaxed)) busy(8192); });
    try
    {
        for (unsigned work : {0u, overlap}) comparisons({32, 64, 32, 8, false}, 128, 0, work, 2, reverse);
    }
    catch (...) { stop = true; for (auto& thread : load) thread.join(); throw; }
    stop = true; for (auto& thread : load) thread.join();
    return 0;
}
int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
    // Exercise the replacement routes before attributing any bytes to the sampler.
    {
        FSSamplingAlloc::Scope scope;
        void* ordinary = ::operator new(37);
        void* aligned = ::operator new(64, std::align_val_t(64));
        const auto allocated = FSSamplingAlloc::snapshot();
        require(allocated.calls == 2 && allocated.live - allocated.baseline == 101 &&
                reinterpret_cast<uintptr_t>(aligned) % 64 == 0, "Allocation probe self-check failed");
        ::operator delete(ordinary); ::operator delete(aligned, std::align_val_t(64));
        require(FSSamplingAlloc::snapshot().live == allocated.baseline, "Allocation probe failed to record frees");
    }
    TimerPeriod timer;
    std::cout << "meta,allocation_probe_only,timing_invalid,accepted_timer_ms," << timer.period
              << ",fixed_registry_bytes," << sizeof(FSSamplingAlloc::Cell) * FSSamplingAlloc::CAPACITY
              << ",owner_mode,LLVector4a_initClass\n"
              << "alloc_header,mode,motions,joints,keys,batch,workers,slots,samples,total_new_calls,total_requested_bytes,"
                 "postwarm_new_calls,postwarm_requested_bytes,tracked_live_baseline_bytes,tracked_live_end_bytes,tracked_peak_bytes\n";
    for (Config config : {Config{8, 20, 1, 1, false}, Config{32, 64, 32, 8, false}, Config{64, 128, 32, 32, true}})
    {
        const unsigned groups = (config.motions + config.batch - 1) / config.batch;
        print(trial(config, Mode::Direct, 0, 0, 32, 0, 0, 0));
        print(trial(config, Mode::Owned, 0, 0, 32, 0, 0, 0));
        print(trial(config, Mode::Barrier, 2, groups, 32, 0, 0, 0));
        print(trial(config, Mode::Wait, 2, groups, 32, 0, 0, 0));
        print(trial(config, Mode::Barrier, 0, 1, 32, 0, 0, 0));
    }
    return 0;
#else
    throw std::logic_error("Allocation probe requires its separate executable");
#endif
}
}
int main(int argc, char** argv)
{
    try
    {
        if (!FSSampling::NumericContext::capture().isSupported())
            throw std::runtime_error("Owned sampling numeric context unsupported; cost experiment unavailable");
        LLVector4a::initClass(); // Match viewer startup for every compared mode, including direct.
        if (FS_SAMPLING_ALLOCATION_PROBE)
        {
            if (argc == 2 && std::string(argv[1]) == "--allocations") return SamplingCostBenchmark::allocations();
            std::cout << "Allocation executable: use --allocations. Timing is deliberately unavailable.\n";
            return argc == 1 ? 0 : 1;
        }
        if (argc == 2 && std::string(argv[1]) == "--cpu") return SamplingCostBenchmark::cpuRun();
        if (argc != 2 || (std::string(argv[1]) != "--benchmark" && std::string(argv[1]) != "--benchmark-reverse" && std::string(argv[1]) != "--smoke"))
        {
            std::cout << "Opt in with --benchmark / --benchmark-reverse, --cpu (long aggregate) or --smoke (short launch check).\n";
            return argc == 1 ? 0 : 1;
        }
        return SamplingCostBenchmark::run(std::string(argv[1]) == "--smoke", std::string(argv[1]) == "--benchmark-reverse");
    }
    catch (const std::exception& error)
    {
        std::cerr << "Sampling benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
