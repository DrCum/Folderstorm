// Private, bounded regression checks for the actual scheduled queue and pool.
#include "linden_common.h"
#include "threadpool.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
unsigned checks = 0, failures = 0;
void check(bool passed, const char* message)
{
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}
[[noreturn]] void stalled(const char* message)
{
    std::cerr << "Scheduled queue test stalled: " << message << std::endl;
    std::_Exit(2); // A blocked worker destructor must not hide a failed test.
}
class Watchdog
{
    std::atomic<bool> done{false};
    std::thread worker{[this] {
        const auto until = Clock::now() + 15s;
        while (!done.load())
        {
            if (Clock::now() >= until) stalled("15-second overall budget");
            std::this_thread::sleep_for(20ms);
        }
    }};
public:
    ~Watchdog() { done.store(true); worker.join(); }
};
template<class T> T await(std::future<T>& future)
{
    if (future.wait_for(3s) != std::future_status::ready) stalled("callback delivery");
    return future.get();
}
class Pool final : public LL::ThreadPoolUsing<LL::WorkSchedule>
{
    using Base = LL::ThreadPoolUsing<LL::WorkSchedule>;
    std::shared_ptr<std::atomic<unsigned>> errors = std::make_shared<std::atomic<unsigned>>(0);
public:
    explicit Pool(unsigned width): Base("FSPrivateDeadline-" + std::to_string(next()), width, 128, false)
    {
        check(LL::ThreadPoolBase::getConfiguredWidth(getQueue().getKey(), width) == width,
              "private pool width override");
    }
    ~Pool() override { close(); }
    static unsigned next() { static unsigned value = 0; return ++value; }
    template<class Fn> LL::WorkQueueBase::Work checked(Fn fn)
    {
        return [fn = std::move(fn), state = errors]() noexcept {
            try { fn(); } catch (...) { ++*state; }
        };
    }
    void noErrors() { check(errors->load() == 0, "callback exception"); }
};
void directDeadlines()
{
    LL::ThreadSafeSchedule<int> queue;
    int value = -1;
    queue.push(Clock::now() - 1s, 11);
    // The outer timed mutex may reject an already expired lock attempt.
    const bool expiredReady = queue.tryPopUntil(Clock::now() - 1ms, value);
    check(!expiredReady || value == 11, "expired ready attempt returned correct item");
    if (!expiredReady) check(queue.tryPop(value) && value == 11, "expired lock attempt leaves ready item intact");
    check(!queue.tryPopFor(5ms, value), "empty timed pop");

    const auto due = Clock::now() + 100ms;
    queue.push(due, 22);
    const bool consumed = queue.tryPopUntil(Clock::now() + 5ms, value);
    check(!consumed, "caller deadline must not consume a later item");
    check(queue.size() == 1, "timed-out future item must remain queued");
    std::cout << "caller_limit_consumed_future=" << consumed << '\n';
    if (!consumed)
    {
        check(!queue.tryPopUntil(Clock::now() - 1ms, value), "expired limit with future head");
        check(queue.tryPopUntil(due + 1s, value) && value == 22 && Clock::now() >= due,
              "future item eventually due within caller limit");
    }
    queue.close();
    check(!queue.tryPopFor(1s, value), "closed empty queue is done");
}
void emptyToFuture()
{
    Pool pool(1);
    pool.start();
    auto warm = std::make_shared<std::promise<bool>>(); auto warmed = warm->get_future();
    check(pool.getQueue().post(pool.checked([warm] { warm->set_value(true); })), "warm admission");
    check(await(warmed), "warm completion");
    std::this_thread::sleep_for(20ms);
    auto signal = std::make_shared<std::promise<Clock::time_point>>(); auto reached = signal->get_future();
    const auto due = Clock::now() + 20ms;
    check(pool.getQueue().post(pool.checked([signal] { signal->set_value(Clock::now()); }), due),
          "empty-to-future admission");
    const bool independent = reached.wait_for(200ms) == std::future_status::ready;
    check(independent, "future item needs no unrelated submission");
    if (!independent) check(pool.getQueue().post([] {}), "diagnostic rescue admission");
    check(await(reached) >= due, "empty-to-future callback must not run early");
    std::cout << "empty_future_without_poke=" << independent << '\n';
    pool.close(); pool.noErrors();
}
void earlierHead()
{
    Pool pool(1);
    auto late = std::make_shared<std::promise<Clock::time_point>>(); auto last = late->get_future();
    auto early = std::make_shared<std::promise<Clock::time_point>>(); auto first = early->get_future();
    const auto lateDue = Clock::now() + 400ms;
    check(pool.getQueue().post(pool.checked([late] { late->set_value(Clock::now()); }), lateDue),
          "initial future head admission");
    pool.start(); std::this_thread::sleep_for(30ms);
    const auto earlyDue = Clock::now() + 20ms;
    check(earlyDue < lateDue, "host stalled before earlier-head admission");
    check(pool.getQueue().post(pool.checked([early] { early->set_value(Clock::now()); }), earlyDue),
          "earlier future head admission");
    const bool independent = first.wait_for(150ms) == std::future_status::ready;
    check(independent, "earlier future head must shorten previous wait");
    if (!independent) check(pool.getQueue().post([] {}), "earlier-head diagnostic rescue admission");
    const auto earlyTime = await(first), lateTime = await(last);
    check(earlyTime >= earlyDue && lateTime >= lateDue && earlyTime < lateTime,
          "future heads execute in order without running early");
    std::cout << "earlier_head_without_poke=" << independent << '\n';
    pool.close(); pool.noErrors();
}
void timedClose(unsigned width)
{
    constexpr unsigned count = 32;
    Pool pool(width);
    auto hits = std::make_shared<std::array<std::atomic<unsigned>, count>>();
    auto early = std::make_shared<std::atomic<unsigned>>(0);
    std::array<std::weak_ptr<unsigned>, count> leases;
    // All workers must reach the empty queue before the future work is admitted.
    auto arrived = std::make_shared<std::atomic<unsigned>>(0);
    auto warm = std::make_shared<std::promise<bool>>(); auto warmed = warm->get_future();
    auto release = std::make_shared<std::promise<void>>(); auto gate = release->get_future().share();
    pool.start(); check(pool.getWidth() == width, "actual pool width");
    for (unsigned i = 0; i < width; ++i)
        check(pool.getQueue().post(pool.checked([=] {
            if (arrived->fetch_add(1) + 1 == width) warm->set_value(true);
            gate.wait();
        })), "parallel warm admission");
    check(await(warmed), "parallel warm completion"); release->set_value();
    std::this_thread::sleep_for(20ms);
    const auto due = Clock::now() + 30ms;
    for (unsigned i = 0; i < count; ++i)
    {
        auto payload = std::make_shared<unsigned>(i); leases[i] = payload;
        const auto itemDue = due + std::chrono::milliseconds(i % 4);
        check(pool.getQueue().post(pool.checked([=] {
            if (Clock::now() < itemDue) ++*early;
            ++(*hits)[*payload];
        }), itemDue), "timed close admission");
    }
    pool.close(); pool.noErrors();
    check(early->load() == 0, "close must wait for future work rather than execute early");
    for (unsigned i = 0; i < count; ++i)
    {
        check((*hits)[i].load() == 1, "close drains each accepted item exactly once");
        check(leases[i].expired(), "drained callbacks release captured payloads after join");
    }
    check(!pool.getQueue().tryPost([] {}), "closed schedule rejects new work");
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--describe")
    {
        std::cout << "Scheduled queue deadline regressions; --repro skips known-old timed close stall\n";
        return 0;
    }
    const bool reproduction = argc == 2 && std::string(argv[1]) == "--repro";
    if (argc != 1 && !reproduction) return 2;
    Watchdog watchdog;
    try
    {
        directDeadlines(); emptyToFuture(); earlierHead();
        if (!reproduction) for (unsigned width : {1u, 3u, 8u}) timedClose(width);
    }
    catch (const std::exception& error) { stalled(error.what()); }
    std::cout << checks << " scheduled queue checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}
