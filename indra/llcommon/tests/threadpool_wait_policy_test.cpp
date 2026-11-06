// Private production-path tests. No scheduler-install override or viewer startup.
#include "scheduler_probe.h"
#include "llevents.h"
#include "llsdutil.h"
#include "llcontrol.h"
#include <boost/fiber/fiber.hpp>
#if LL_WINDOWS
#include <mmsystem.h>
#endif

namespace FSWaitPolicyTest
{
using namespace FSSchedulerProbe;
using Policy = LL::ThreadPoolBase::WaitPolicy;
class Controls
{
    const std::thread::id owner = std::this_thread::get_id();
    LLSD widths = LLSD::emptyMap();
    LLTempBoundListener connection;
public:
    std::atomic<unsigned> queries{0}, workerQueries{0};
    Controls(): connection(LLEventPumps::instance().obtain("LLViewerControl").listen(
        "FSPrivateWaitPolicy", [this](const LLSD& request) {
            LLSD reply;
            if (std::this_thread::get_id() != owner)
            {
                ++workerQueries;
                reply["error"] = "Private worker unexpectedly queried settings";
            }
            else
            {
                require(request["op"].asString() == "get" && request["group"].asString() == "Global" &&
                        request["key"].asString() == "ThreadPoolSizes", "Unexpected private control request");
                ++queries; reply["value"] = widths;
            }
            LLEventPumps::instance().obtain(request["reply"].asString()).post(reply);
            return false;
        })) {}
    void set(const std::string& name, const LLSD& width)
    {
        if (width.isUndefined()) widths.erase(name);
        else widths[name] = width;
    }
};
std::string nextName()
{
    static unsigned next = 0;
    return "FSPrivateProductionWait-" + std::to_string(++next);
}
template<class Queue = LL::WorkQueue>
class Pool final : public LL::ThreadPoolUsing<Queue>
{
    using Base = LL::ThreadPoolUsing<Queue>;
    std::shared_ptr<std::atomic<unsigned>> errors = std::make_shared<std::atomic<unsigned>>(0);
public:
    Pool(const std::string& name, unsigned fallback, Policy request, unsigned capacity = 256, bool automatic = false):
        Base(name, fallback, capacity, automatic, request) {}
    ~Pool() override { this->close(); }
    template<class Fn> LL::WorkQueueBase::Work checked(Fn fn)
    {
        return [fn = std::move(fn), state = errors]() noexcept {
            try { fn(); } catch (...) { ++*state; }
        };
    }
    void noErrors() { require(errors->load() == 0, "Production-policy callback threw"); }
};
template<class Queue> void warm(Pool<Queue>& pool)
{
    const auto width = pool.getWidth();
    auto arrived = std::make_shared<std::atomic<unsigned>>(0);
    auto ready = std::make_shared<std::promise<void>>(); auto entered = ready->get_future();
    auto release = std::make_shared<std::promise<void>>(); auto gate = release->get_future().share();
    for (unsigned i = 0; i < width; ++i)
        require(pool.getQueue().post(pool.checked([=] {
            if (arrived->fetch_add(1) + 1 == width) ready->set_value();
            gate.wait();
        })), "Production-policy warm admission failed");
    await(entered, "Not all actual production workers reached warmup"); release->set_value();
    std::this_thread::sleep_for(20ms);
}
Policy expected([[maybe_unused]] Policy requested, [[maybe_unused]] unsigned width)
{
#if LL_WINDOWS
    if (requested == Policy::EventWaitSingleWorker && width == 1)
        return Policy::EventWaitSingleWorker;
#endif
    return Policy::PlatformDefault;
}
void selection(Controls& controls, unsigned fallback, const LLSD& overrideWidth,
               unsigned actualWidth, Policy request)
{
    const auto name = nextName(); controls.set(name, overrideWidth);
    const auto before = controls.queries.load();
    Pool<> pool(name, fallback, request);
    require(controls.queries.load() == before + 1, "Constructor did not resolve width exactly once");
    require(pool.getWaitPolicy() == expected(request, actualWidth), "Effective production wait policy is wrong");
    // A later control change must not resize or switch a constructed/running pool.
    controls.set(name, actualWidth == 1 ? 3 : 1);
    require(pool.getWaitPolicy() == expected(request, actualWidth), "Constructed policy changed with settings");
    if (actualWidth == 0)
    {
        require(pool.getWidth() == 0, "Unstarted zero-width pool acquired threads");
        pool.close(); return;
    }
    pool.start(); require(pool.getWidth() == actualWidth, "Resolved width changed before start"); warm(pool);
    constexpr unsigned count = 128;
    auto hits = std::make_shared<std::array<std::atomic<unsigned>, count>>();
    auto order = std::make_shared<std::atomic<unsigned>>(0), wrong = std::make_shared<std::atomic<unsigned>>(0);
    auto sum = std::make_shared<std::atomic<U64>>(0); const auto mainThread = std::this_thread::get_id();
    std::array<std::weak_ptr<unsigned>, count> leases;
    for (unsigned i = 0; i < count; ++i)
    {
        auto payload = std::make_shared<unsigned>(i); leases[i] = payload;
        require(pool.getQueue().post(pool.checked([=] {
            if (std::this_thread::get_id() == mainThread || (actualWidth == 1 && order->fetch_add(1) != *payload)) ++*wrong;
            ++(*hits)[*payload]; sum->fetch_add(work(*payload));
        })), "Production-policy burst rejected");
    }
    auto signal = std::make_shared<std::promise<bool>>(); auto result = signal->get_future();
    require(pool.getQueue().post(pool.checked([=] {
        const auto worker = std::this_thread::get_id(); std::array<unsigned, 2> steps{};
        std::array<bool, 2> progress{}; bool valid = true;
        auto body = [&](unsigned who) {
            for (unsigned n = 0; n < 16; ++n) { ++steps[who]; boost::this_fiber::yield(); }
            progress[who] = steps[1 - who] > 0;
            const auto due = Clock::now() + 2ms; boost::this_fiber::sleep_until(due);
            valid = valid && Clock::now() >= due && std::this_thread::get_id() == worker;
        };
        boost::fibers::fiber a([&] { body(0); }), b([&] { body(1); }); a.join(); b.join();
        signal->set_value(valid && progress[0] && progress[1] && steps[0] == 16 && steps[1] == 16);
    })), "Production-policy fiber admission failed");
    require(await(result, "Production-policy fibers stalled"), "Production-policy fiber progress/affinity changed");
    pool.close(); pool.noErrors(); U64 wanted = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        wanted += work(i); require((*hits)[i].load() == 1, "Production-policy burst lost/duplicated work");
        require(leases[i].expired(), "Production-policy payload survived join");
    }
    require(sum->load() == wanted && wrong->load() == 0, "Production-policy checksum, owner or FIFO changed");
    require(!pool.getQueue().tryPost([] {}), "Closed production-policy pool accepted work");
    require(controls.queries.load() == before + 1 && controls.workerQueries.load() == 0,
            "Running production pool re-queried control state");
}
void admissionAndShutdown(Controls& controls, Policy request)
{
    const auto name = nextName(); controls.set(name, 1);
    Pool<> pool(name, 3, request, 4, true); pool.start(); warm(pool);
    auto entered = std::make_shared<std::promise<void>>(), release = std::make_shared<std::promise<void>>();
    auto ready = entered->get_future(); auto gate = release->get_future().share();
    require(pool.getQueue().post(pool.checked([=] { entered->set_value(); gate.wait(); })), "Admission blocker rejected");
    await(ready, "Admission blocker did not start");
    auto done = std::make_shared<std::atomic<unsigned>>(0);
    for (unsigned i = 0; i < 4; ++i)
        require(pool.getQueue().tryPost(pool.checked([=] { ++*done; })), "Known-free production slot rejected");
    std::weak_ptr<unsigned> rejected;
    {
        auto payload = std::make_shared<unsigned>(9); rejected = payload;
        require(!pool.getQueue().tryPost(pool.checked([payload, done] { done->fetch_add(*payload); })),
                "Full production-policy queue accepted extra work");
    }
    require(rejected.expired(), "Rejected production payload was retained");
    release->set_value();
    // Exercise the actual automatic-shutdown listener in this private process.
    LLEventPumps::instance().obtain("LLApp").post(llsd::map("status", "stopping"));
    pool.noErrors(); require(done->load() == 4, "Automatic shutdown did not drain accepted work");
    require(!pool.getQueue().tryPost([] {}), "Automatic shutdown left queue open");
}
void timed(Controls& controls, Policy request)
{
    const auto name = nextName(); controls.set(name, 1);
    Pool<LL::WorkSchedule> pool(name, 3, request, 16); pool.start(); warm(pool);
    auto first = std::make_shared<std::promise<Clock::time_point>>(); auto reached = first->get_future();
    const auto due = Clock::now() + 20ms;
    require(pool.getQueue().post(pool.checked([=] { first->set_value(Clock::now()); }), due), "Future production work rejected");
    require(await(reached, "Future production work needed an unrelated submission") >= due, "Future work ran early");
    auto late = std::make_shared<std::promise<Clock::time_point>>(), early = std::make_shared<std::promise<Clock::time_point>>();
    auto last = late->get_future(), next = early->get_future(); const auto lateDue = Clock::now() + 200ms;
    require(pool.getQueue().post(pool.checked([=] { late->set_value(Clock::now()); }), lateDue), "Late work rejected");
    std::this_thread::sleep_for(5ms); const auto earlyDue = Clock::now() + 20ms;
    require(earlyDue < lateDue, "Host stalled before earlier-head admission");
    require(pool.getQueue().post(pool.checked([=] { early->set_value(Clock::now()); }), earlyDue), "Earlier future head rejected");
    const auto earlyTime = await(next, "Earlier production head stalled"), lateTime = await(last, "Late production head stalled");
    require(earlyTime >= earlyDue && lateTime >= lateDue && earlyTime <= lateTime, "Production deadlines/order changed");
    auto count = std::make_shared<std::atomic<unsigned>>(0); const auto drainDue = Clock::now() + 10ms;
    require(pool.getQueue().post(pool.checked([=] { if (Clock::now() >= drainDue) ++*count; }), drainDue), "Timed drain rejected");
    pool.close(); pool.noErrors(); require(count->load() == 1, "Close did not wait for and drain future work");
}
void producerClose(Controls& controls, Policy request)
{
    constexpr unsigned count = 128;
    const auto name = nextName(); controls.set(name, 1);
    Pool<> pool(name, 3, request, 64); pool.start(); warm(pool);
    auto hits = std::make_shared<std::array<std::atomic<unsigned>, count>>();
    std::array<bool, count> accepted{}; std::array<std::weak_ptr<unsigned>, count> leases;
    auto boundary = std::make_shared<std::promise<void>>(), release = std::make_shared<std::promise<void>>();
    auto reached = boundary->get_future(); auto gate = release->get_future().share();
    std::thread producer([&] {
        for (unsigned i = 0; i < count; ++i)
        {
            if (i == count / 2) { boundary->set_value(); gate.wait(); }
            auto payload = std::make_shared<unsigned>(i); leases[i] = payload;
            auto callback = pool.checked([payload, hits] { ++(*hits)[*payload]; });
            accepted[i] = i == 0 ? pool.getQueue().post(callback) : pool.getQueue().tryPost(callback);
        }
    });
    await(reached, "Production producer did not reach close boundary"); release->set_value();
    pool.close(); producer.join(); pool.noErrors(); unsigned admitted = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        admitted += accepted[i];
        require((*hits)[i].load() == unsigned(accepted[i]), "Close race lost, duplicated or ran rejected work");
        require(leases[i].expired(), "Close-race payload survived producer/worker join");
    }
    require(admitted > 0 && !pool.getQueue().tryPost([] {}), "Production close/admission coverage missing");
}
void startupControl()
{
    LLControlGroup group("FSPrivateStartupWaitControls");
    LLPointer<LLControlVariable> control = group.declareBOOL("FSPrivateStartupWait", false,
        "Private startup Boolean parser test", LLControlVariable::PERSIST_NO);
    require(!control->getValue().asBoolean() && !control->isPersisted(), "Private startup control default/persistence wrong");
    control->setValue("true", false); // Same unsaved path as tempSetControl.
    require(control->getValue().asBoolean() && !control->getSaveValue().asBoolean(), "Startup true override was not temporary");
    control->setValue("false", false);
    require(!control->getValue().asBoolean() && !control->getSaveValue().asBoolean(), "Startup false override did not restore default");
}
int correctness()
{
    Controls controls; Watchdog watchdog;
    startupControl();
    {
        LL::ThreadPool legacy(nextName());
        require(legacy.getWaitPolicy() == Policy::PlatformDefault, "Legacy constructor defaults changed");
    }
    for (auto policy : {Policy::PlatformDefault, Policy::EventWaitSingleWorker})
    {
        for (unsigned width : {1u, 3u, 8u})
        {
            watchdog.phase("production selection/burst/fibers"); selection(controls, width, LLSD(), width, policy);
        }
        watchdog.phase("automatic shutdown/admission"); admissionAndShutdown(controls, policy);
        watchdog.phase("production scheduled deadlines"); timed(controls, policy);
        watchdog.phase("production producer/close race"); producerClose(controls, policy);
    }
    for (const auto& test : {std::array<unsigned, 3>{3, 1, 1}, {1, 3, 3}, {1, 0, 0}, {0, 1, 1}})
    {
        watchdog.phase("configured-width override"); selection(controls, test[0], LLSD::Integer(test[1]), test[2], Policy::EventWaitSingleWorker);
    }
    watchdog.phase("noninteger override fallback"); selection(controls, 1, LLSD("invalid"), 1, Policy::EventWaitSingleWorker);
    require(controls.workerQueries.load() == 0, "Production worker read saved controls");
    std::cout << checks << " production wait-policy, width-snapshot, queue/fiber/lifetime/shutdown checks passed\n";
    return 0;
}
int delivery([[maybe_unused]] Policy request)
{
#if LL_WINDOWS
    Watchdog watchdog;
    struct TimerPeriod
    {
        TimerPeriod() { require(timeBeginPeriod(1) == TIMERR_NOERROR, "Requested timer period unavailable"); }
        ~TimerPeriod() { timeEndPeriod(1); }
    } timer;
    Controls controls; const auto name = nextName(); controls.set(name, 1);
    Pool<> pool(name, 3, request, 8); controls.set(name, 8);
    pool.start(); require(pool.getWidth() == 1, "Delivery probe lost its constructor width snapshot"); warm(pool);
    const auto effective = pool.getWaitPolicy() == Policy::EventWaitSingleWorker ? "event-single" : "platform-default";
    const auto requested = request == Policy::EventWaitSingleWorker ? "event" : "default";
    std::cout << "requested,effective,width,sample,wall_ns,admitted,status\n";
    for (unsigned sample = 0; sample < 64; ++sample)
    {
        std::this_thread::sleep_for(5ms);
        auto signal = std::make_shared<std::promise<Clock::time_point>>(); auto reached = signal->get_future();
        const auto begin = Clock::now();
        const bool admitted = pool.getQueue().tryPost(pool.checked([=] { signal->set_value(Clock::now()); }));
        if (!admitted)
        {
            std::cout << requested << ',' << effective << ",1," << sample << ",0,0,rejected\n";
            continue; // Preserve a rejected observation; never retry-spin.
        }
        const auto called = await(reached, "Actual production delivery timed out");
        std::cout << requested << ',' << effective << ",1," << sample << ','
            << std::chrono::duration_cast<std::chrono::nanoseconds>(called - begin).count() << ",1,ok\n";
    }
    pool.close(); pool.noErrors();
    require(controls.queries.load() == 1 && controls.workerQueries.load() == 0, "Delivery path re-queried controls");
#else
    std::cout << "SKIP Windows-only delivery observation\n";
#endif
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--describe")
    {
        std::cout << "Private actual-constructor policy/control tests. Optional --wake default|event observes delivery only.\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--wake")
    {
        const std::string mode = argv[2]; if (mode != "default" && mode != "event") return 1;
        try { return FSWaitPolicyTest::delivery(mode == "event" ?
            FSWaitPolicyTest::Policy::EventWaitSingleWorker : FSWaitPolicyTest::Policy::PlatformDefault); }
        catch (const std::exception& error) { FSSchedulerProbe::fail(error.what()); }
    }
    if (argc != 1) return 1;
    try { return FSWaitPolicyTest::correctness(); }
    catch (const std::exception& error) { FSSchedulerProbe::fail(error.what()); }
}
