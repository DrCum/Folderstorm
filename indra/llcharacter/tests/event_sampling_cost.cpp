// Private complete-controller experiment. No viewer caller or scheduler override.
#include "linden_common.h"
#include "current_motion_fixture.h"
#include "sampling_cost_fixture.h"
#include "sampling_allocation_probe.h"
#include "sampling_test_context.h"
#include "threadpool.h"
#include "llevents.h"
#include "llsimdmath.h"
#include <array>
#include <atomic>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
#if LL_WINDOWS
#include <mmsystem.h>
#endif

namespace FSEventSampling
{
using TimePredict = bool (LLMotionController::*)(LLMotion&, const LLMotionController::SamplingClock&, F32&) const;
using MaskPredict = bool (LLMotionController::*)(const LLMotion&, U8 (&)[2][LL_CHARACTER_MAX_ANIMATED_JOINTS]) const;
using Clock = std::chrono::steady_clock;
using Policy = LL::ThreadPoolBase::WaitPolicy;
using Motion = LLKeyframeMotion;
using Batch = FSSamplingCost::BatchSampler;
std::atomic<unsigned> checks{0};
void require(bool value, const char* message)
{
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::_Exit(2); }
}
U64 nanos(Clock::duration duration)
{ return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(); }
U64 cycles()
{
#if LL_WINDOWS
    ULONG64 value = 0;
    require(QueryThreadCycleTime(GetCurrentThread(), &value), "Owner/worker cycle read failed");
    return value;
#else
    return 0;
#endif
}
template<class Future> void await(Future& future)
{ require(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Private control wait timed out"); future.get(); }
struct Measurement
{
    FSSamplingCost::Trace trace;
    U64 workerCycles = 0;
};
struct DeliveryGate
{
    std::promise<void> entered;
    std::shared_future<void> released;
    explicit DeliveryGate(std::shared_future<void> value): released(std::move(value)) {}
};
struct DeliveryControl
{
    std::promise<void> release;
    std::shared_ptr<DeliveryGate> data = std::make_shared<DeliveryGate>(release.get_future().share());
    std::future<void> entered = data->entered.get_future();
    bool opened = false;
    void open() { if (!opened) { opened = true; release.set_value(); } }
    ~DeliveryControl() { open(); } // Declare after the pool so unwinding cannot leave its worker held.
};
struct Request
{
    Batch::Request batch;
    std::shared_ptr<Measurement> measurement;
    std::shared_ptr<DeliveryGate> delivery; // Correctness controls own synchronization data only.
};
struct Result
{
    Batch::Result batch;
    std::shared_ptr<Measurement> measurement;
    void takeRequest() { batch.takeRequest(); measurement.reset(); }
    Request recycle() { return {batch.recycle(), std::move(measurement), {}}; }
};
struct Sampler
{
    using Request = FSEventSampling::Request;
    using Result = FSEventSampling::Result;
    static bool sample(Request input, Result& output)
    {
        output.measurement = std::move(input.measurement);
        const auto start = cycles();
        if (input.delivery)
        {
            input.delivery->entered.set_value();
            input.delivery->released.wait(); // Correctness gate; absent from timed requests.
        }
        const bool success = Batch::sample(std::move(input.batch), output.batch);
        output.measurement->workerCycles = cycles() - start;
        return success;
    }
};
using Executor = FSSamplingTest::BoundedExecutor<Sampler>;
using Status = Executor::Status;
enum class Mode { Direct, Owned, OwnedRetained, Default, Event, EventFirstRetained, EventLaterRetained, DefaultLaterRetained };
bool isOwned(Mode mode) { return mode == Mode::Owned || mode == Mode::OwnedRetained; }
bool isQueued(Mode mode) { return mode != Mode::Direct && !isOwned(mode); }
bool usesEventWait(Mode mode)
{ return mode == Mode::Event || mode == Mode::EventFirstRetained || mode == Mode::EventLaterRetained; }
bool retainsBindings(Mode mode)
{ return mode == Mode::OwnedRetained || mode == Mode::EventFirstRetained || mode == Mode::EventLaterRetained || mode == Mode::DefaultLaterRetained; }
bool checksLaterCallbacks(Mode mode) { return mode == Mode::EventLaterRetained || mode == Mode::DefaultLaterRetained; }
const char* name(Mode mode)
{
    switch (mode) { case Mode::Direct: return "direct"; case Mode::Owned: return "owned";
    case Mode::OwnedRetained: return "owned-retained";
    case Mode::EventFirstRetained: return "event-first-retained";
    case Mode::EventLaterRetained: return "event-later-retained";
    case Mode::DefaultLaterRetained: return "default-later-retained";
    case Mode::Default: return "default"; default: return "event"; }
}
Mode parseMode(const std::string& value)
{
    for (auto mode : {Mode::Direct, Mode::Owned, Mode::OwnedRetained, Mode::Default, Mode::Event,
                     Mode::EventFirstRetained, Mode::EventLaterRetained, Mode::DefaultLaterRetained}) if (value == name(mode)) return mode;
    throw std::runtime_error("Unknown mode");
}
class Controls
{
    LLTempBoundListener connection;
public:
    Controls(): connection(LLEventPumps::instance().obtain("LLViewerControl").listen("FSPrivateEventSampling",
        [](const LLSD& request) {
            LLSD response; response["value"] = LLSD::emptyMap();
            const auto name = request["reply"].asString();
            // Match General's packaged effective width without using its registered queue.
            response["value"]["FSEventSamplingPrivate"] = 1;
            LLEventPumps::instance().obtain(name).post(response); return false;
        })) {}
};
class Pool : public LL::ThreadPool
{
#if LL_WINDOWS
    HANDLE measured = nullptr;
#endif
public:
    explicit Pool(Mode mode): LL::ThreadPool("FSEventSamplingPrivate", 3, 1024 * 1024, false,
        usesEventWait(mode) ? Policy::EventWaitSingleWorker : Policy::PlatformDefault) {}
    void startMeasured()
    {
        start();
#if LL_WINDOWS
        require(mThreads.size() == 1 && DuplicateHandle(GetCurrentProcess(), mThreads.front().second.native_handle(),
            GetCurrentProcess(), &measured, 0, false, DUPLICATE_SAME_ACCESS), "Private worker handle unavailable");
#endif
    }
    U64 workerCycles() const
    {
#if LL_WINDOWS
        ULONG64 value = 0; require(QueryThreadCycleTime(measured, &value), "Pool cycle read failed"); return value;
#else
        return 0;
#endif
    }
    ~Pool() override
    {
        close();
#if LL_WINDOWS
        if (measured) CloseHandle(measured);
#endif
    }
};
struct TimerPeriod
{
    unsigned accepted = 0;
    TimerPeriod()
    {
#if LL_WINDOWS
        if (timeBeginPeriod(1) == TIMERR_NOERROR) accepted = 1;
#endif
    }
    ~TimerPeriod()
    {
#if LL_WINDOWS
        if (accepted) timeEndPeriod(accepted);
#endif
    }
};
LLUUID assetId(unsigned index)
{ LLUUID id("fe000000-0000-0000-0000-000000001450"); id.mData[15] = U8(index + 1); return id; }
struct Scene
{
    std::vector<std::unique_ptr<FSCurrentMotion::Character>> characters;
    explicit Scene(unsigned count)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            auto character = std::make_unique<FSCurrentMotion::Character>(32);
            auto& controller = character->getMotionController(); controller.updateMotions();
            for (unsigned motion = 0; motion < 4; ++motion)
                require(controller.startMotion(assetId(motion), i / 128.f + motion / 512.f), "Ordinary controller activation failed");
            characters.push_back(std::move(character));
        }
    }
    void update()
    { for (auto& character : characters) character->getMotionController().updateMotions(); }
    std::vector<U32> snapshot()
    {
        std::vector<U32> words;
        auto scalar = [&](F32 value) { U32 raw; std::memcpy(&raw, &value, sizeof(raw)); words.push_back(raw); };
        for (auto& character : characters)
        {
            auto& controller = character->getMotionController(); scalar(controller.getAnimTime());
            words.push_back(U32(controller.getActiveMotions().size())); words.push_back(controller.isPaused());
            for (auto* base : controller.getActiveMotions())
            {
                for (auto byte : base->getID().mData) words.push_back(byte);
                auto values = FSCurrentMotion::pose(*static_cast<Motion*>(base));
                words.insert(words.end(), values.begin(), values.end());
                scalar(base->getPose()->getWeight()); scalar(base->getFadeWeight());
                scalar(base->getStopTime()); words.push_back(base->isStopped());
            }
            for (auto& joint : character->joints)
            {
                for (F32 value : joint->getPosition().mV) scalar(value);
                for (F32 value : joint->getRotation().mQ) scalar(value);
                for (F32 value : joint->getScale().mV) scalar(value);
            }
            const auto* hand = static_cast<const LLHandMotion::eHandPose*>(character->getAnimationData("Hand Pose"));
            const auto* priority = character->getHandPoseAnimationPriority();
            words.push_back(hand ? U32(*hand) : ~U32(0)); words.push_back(priority ? U32(*priority) : ~U32(0));
        }
        return words;
    }
};
struct Assets
{
    std::array<std::shared_ptr<Motion::JointMotionList>, 4> values;
    Assets()
    {
        for (unsigned i = 0; i < values.size(); ++i)
        {
            auto data = FSSamplingCost::asset(32, 32, i);
            data->mDuration = 31.f / 30.f; data->mLoop = true; data->mLoopInPoint = .125f; data->mLoopOutPoint = .75f;
            data->mBasePriority = data->mMaxPriority = LLJoint::MEDIUM_PRIORITY;
            data->mHandPose = LLHandMotion::eHandPose(1 + i);
            for (unsigned j = 0; j < 32; ++j)
            {
                data->mJointMotionArray[j]->mJointName = "joint" + std::to_string(j);
                data->mJointMotionArray[j]->mPriority = LLJoint::MEDIUM_PRIORITY;
            }
            LLKeyframeDataCache::addKeyframeData(assetId(i), data); values[i] = std::move(data);
        }
    }
    ~Assets() { LLKeyframeDataCache::clear(); }
};
enum Stage { Capture, Submit, Gap, Consume, Apply, Fallback, Cleanup, StageCount };
struct Row
{
    U64 totalNs = 0, ownerCycles = 0;
    std::array<U64, StageCount> ns{};
    unsigned predicted = 0, masked = 0, excluded = 0, rejected = 0, accepted = 0;
    unsigned callbacks = 0, applied = 0, fallback = 0, stale = 0;
    unsigned readinessChecks = 0, pendingMisses = 0, failedBatches = 0, firstReadyCallback = 0;
};
struct Callback
{
    unsigned character;
    LLUUID motion;
    U32 timeBits;
    bool operator==(const Callback& other) const
    { return character == other.character && motion == other.motion && timeBits == other.timeBits; }
};
struct Frame;
thread_local Frame* active = nullptr;
std::atomic<U64> gapSink{0};
struct Frame
{
    struct Entry { LLMotionController* owner; LLUUID id; U64 instance; };
    Scene& scene;
    Mode mode;
    unsigned gapUs;
    Executor* executor;
    Request& input;
    Result& output;
    std::shared_ptr<Measurement> measurement;
    std::optional<Executor::Ticket> ticket;
    std::array<Entry, 32> entries{};
    size_t count = 0;
    bool prepared = false, consumed = false, ready = false, forceReady = false;
    unsigned mutation = 0;
    bool failSampling = false;
    std::shared_ptr<DeliveryGate> delivery;
    std::function<void(Frame&)> beforeCallback; // Owner-only correctness hook; never sent to a worker.
    Row row;
    std::vector<Callback>* callbackTrace = nullptr;
    const std::thread::id owner = std::this_thread::get_id();
    Frame(Scene& value, Mode variant, unsigned gap, Executor* destination, Request& request, Result& reusable,
          std::shared_ptr<Measurement> trace): scene(value), mode(variant), gapUs(gap), executor(destination),
          input(request), output(reusable), measurement(std::move(trace)) { require(!active, "Nested private frame"); active = this; }
    ~Frame() { require(active == this, "Private frame ownership changed"); active = nullptr; }
    void prepare(bool force, TimePredict predictTime, MaskPredict predictMask)
    {
        if (prepared) return;
        prepared = true;
        const auto begin = Clock::now();
        if (mode != Mode::Direct)
        {
            for (auto& character : scene.characters)
            {
                auto& controller = character->getMotionController(); LLMotionController::SamplingClock clock;
                const bool project = controller.predictSamplingClock(force, clock);
                U8 covered[2][LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
                for (auto* base : controller.getActiveMotions())
                {
                    auto* motion = dynamic_cast<Motion*>(base); F32 time = 0;
                    if (!motion || !motion->permitsOwnedSampling() || !project || !(controller.*predictTime)(*base, clock, time))
                    { std::memset(covered, 0, sizeof(covered)); ++row.excluded; continue; }
                    if (!(controller.*predictMask)(*base, covered)) { ++row.masked; continue; }
                    if (count == entries.size()) { ++row.rejected; continue; }
                    if (input.batch.queries.size() <= count) input.batch.queries.emplace_back();
                    if (!motion->prepareOwnedSampling(time, input.batch.queries[count]))
                    { ++row.rejected; continue; }
                    entries[count++] = {&controller, base->getID(), motion->getOwnedSamplingInstance()}; ++row.predicted;
                }
            }
            input.batch.queries.resize(count);
            input.measurement = measurement;
            input.delivery = delivery;
            if (failSampling && !input.batch.queries.empty()) input.batch.queries.front().numericContext = {};
            // Alias owns the enclosing trace allocation, never a frame or scene.
            input.batch.trace = std::shared_ptr<FSSamplingCost::Trace>(measurement, &measurement->trace);
        }
        row.ns[Capture] += nanos(Clock::now() - begin);
        const auto submit = Clock::now();
        if (count)
        {
            measurement->trace.queued = Clock::now();
            if (isOwned(mode)) { ready = Sampler::sample(std::move(input), output); require(ready, "Owner sample failed"); }
            else { ticket = executor->trySubmit(input); if (ticket) ++row.accepted; else ++row.rejected; }
            if (forceReady && ticket)
            {
                // Correctness-only oracle, outside all performance observations.
                const auto deadline = Clock::now() + std::chrono::seconds(5);
                while (executor->status(*ticket) == Status::Queued || executor->status(*ticket) == Status::Running)
                {
                    if (Clock::now() >= deadline) require(false, "Ready oracle timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        row.ns[Submit] += nanos(Clock::now() - submit);
        if (mutation == 1) FSCurrentMotion::Clock::count();
        if (mutation == 2) for (auto& character : scene.characters) character->setSkeletonSerialNum(17);
        if (mutation == 3) for (auto& character : scene.characters)
            static_cast<Motion*>(character->getMotionController().findMotion(assetId(3)))->setHandPose(LLHandMotion::HAND_POSE_FIST);
        if (mutation == 4) for (auto& character : scene.characters) character->allowOwned = false;
        const auto gap = Clock::now(); U64 checksum = 7;
        if (gapUs) while (nanos(Clock::now() - gap) < U64(gapUs) * 1000)
            checksum = (checksum ^ (checksum >> 7)) * 1664525 + 1013904223;
        gapSink.store(checksum, std::memory_order_relaxed); row.ns[Gap] += nanos(Clock::now() - gap);
    }
    bool invoke(LLMotionController& controller, LLMotion& base, F32 time, U8* mask)
    {
        require(std::this_thread::get_id() == owner, "Scene callback left its owner"); ++row.callbacks;
        if (beforeCallback) beforeCallback(*this);
        if (callbackTrace)
        {
            unsigned character = 0;
            while (character < scene.characters.size() && &scene.characters[character]->getMotionController() != &controller) ++character;
            require(character < scene.characters.size(), "Callback controller is outside its private Scene");
            U32 timeBits; std::memcpy(&timeBits, &time, sizeof(timeBits));
            callbackTrace->push_back({character, base.getID(), timeBits});
        }
        auto* motion = dynamic_cast<Motion*>(&base);
        const auto consume = Clock::now();
        if (!consumed)
        {
            if (ticket)
            {
                ++row.readinessChecks;
                const auto status = executor->take(*ticket, output);
                ready = status == Status::Ready;
                const bool pending = status == Status::Queued || status == Status::Running;
                if (pending) ++row.pendingMisses;
                if (status == Status::Failed) ++row.failedBatches;
                // One nonblocking observation per actual callback. Early misses
                // keep direct execution; only the private later modes retain work.
                consumed = ready || !pending || !checksLaterCallbacks(mode);
                if (!ready && consumed) executor->cancel(*ticket);
            }
            else consumed = true;
            if (ready) row.firstReadyCallback = row.callbacks;
        }
        size_t index = count;
        if (ready && motion) for (size_t i = 0; i < count; ++i)
            if (entries[i].owner == &controller && entries[i].instance == motion->getOwnedSamplingInstance()) { index = i; break; }
        row.ns[Consume] += nanos(Clock::now() - consume);
        const auto update = Clock::now(); bool applied = false;
        const bool result = index < count ? motion->updateWithOwnedSampling(time, mask, output.batch.rows[index], applied) : base.onUpdate(time, mask);
        if (applied) { ++row.applied; row.ns[Apply] += nanos(Clock::now() - update); }
        else { ++row.fallback; row.ns[Fallback] += nanos(Clock::now() - update); if (index < count) ++row.stale; }
        return result;
    }
    void finish()
    {
        const auto begin = Clock::now();
        if (ticket && !consumed) executor->cancel(*ticket);
        if (ready) input = output.recycle();
        if (!retainsBindings(mode)) for (size_t i = 0; i < count; ++i)
        {
            auto* base = entries[i].owner->findMotion(entries[i].id); auto* motion = dynamic_cast<Motion*>(base);
            if (motion) motion->releaseOwnedSamplingBindings(entries[i].instance);
        }
        row.ns[Cleanup] += nanos(Clock::now() - begin);
    }
};
void prepare(LLMotionController&, bool force, TimePredict time, MaskPredict mask)
{ if (active) active->prepare(force, time, mask); }
bool invoke(LLMotionController& controller, LLMotion& motion, F32 time, U8* mask)
{ return active ? active->invoke(controller, motion, time, mask) : motion.onUpdate(time, mask); }

struct Summary
{
    std::vector<Row> rows;
    std::vector<std::shared_ptr<Measurement>> traces;
    FSSamplingAlloc::Stats allocations;
    FSSamplingAlloc::Stats firstFrame;
    U64 poolCycles = 0;
    U64 ownerWindowCycles = 0;
    size_t retainedBindingBytes = 0, retainedBindingMotions = 0, bindingBytesAfterCleanup = 0;
    U64 finalBindingCleanupNs = 0;
};
Summary run(Mode mode, unsigned avatars, unsigned gap, unsigned frames, const std::string& load,
            bool correctness = false, unsigned mutation = 0)
{
    require(avatars && avatars <= 32 && frames && frames <= 2000 && gap <= 2000, "Private workload exceeds bounds");
    require(load == "normal" || load == "busy" || load == "closed", "Unknown queue control");
    FSCurrentMotion::Clock::set(100.0); Assets assets; Scene scene(avatars), expected(avatars);
    Controls controls; std::unique_ptr<Pool> pool; std::unique_ptr<Executor> executor;
    std::promise<void> release, entered; auto gate = release.get_future().share(); auto started = entered.get_future();
    if (isQueued(mode))
    {
        pool = std::make_unique<Pool>(mode); pool->startMeasured();
        require(pool->getWidth() == 1, "Configured private pool width differs");
        require(pool->getWaitPolicy() == (usesEventWait(mode) ? Policy::EventWaitSingleWorker : Policy::PlatformDefault), "Production policy differs");
        auto ready = std::make_shared<std::promise<void>>(); auto warm = ready->get_future();
        require(pool->getQueue().post([ready] { ready->set_value(); }), "Warm task rejected"); await(warm);
        if (load == "busy") { require(pool->getQueue().post([&entered, gate] { entered.set_value(); gate.wait(); }), "Blocker rejected"); await(started); }
        if (load == "closed") pool->close();
        Executor::Submit submit = FSSamplingTest::queueSubmitter(pool->getQueue().getWeak(), 1);
        if (correctness)
        {
            // The ready/stale oracle requires admitted work. tryPost may reject
            // transient lock contention; timed cases retain that nonblocking path.
            const auto queue = pool->getQueue().getWeak();
            submit = [queue](const Executor::Work& work) { auto live = queue.lock(); return live && live->post(work); };
        }
        executor = std::make_unique<Executor>(1, std::move(submit));
    }
    Summary summary; summary.rows.reserve(frames); summary.traces.reserve(frames);
    for (unsigned i = 0; i < frames; ++i) summary.traces.push_back(std::make_shared<Measurement>());
    Request input; Result reusableOutput;
    // Warm controller and hand publication before instrumentation, with a changing clock.
    for (unsigned i = 0; i < 8; ++i)
    { FSCurrentMotion::Clock::set(100.0 + (i + 1) / 64.0); scene.update(); expected.update(); }
    std::vector<Callback> actualCallbacks, expectedCallbacks;
    actualCallbacks.reserve(avatars * 4); expectedCallbacks.reserve(avatars * 4);
    FSSamplingAlloc::Scope allocationScope;
    const auto poolBegin = pool ? pool->workerCycles() : 0;
    const auto ownerBegin = cycles();
    for (unsigned i = 0; i < frames; ++i)
    {
        FSCurrentMotion::Clock::set(100.0 + (i + 9) / 64.0);
        Row row;
        const auto cpu = cycles(); const auto begin = Clock::now();
        {
            Frame frame(scene, mode, gap, executor.get(), input, reusableOutput, summary.traces[i]);
            frame.forceReady = correctness && load == "normal" && mode != Mode::Direct;
            frame.mutation = mutation; frame.callbackTrace = correctness ? &actualCallbacks : nullptr;
            scene.update(); frame.finish(); row = frame.row;
        }
        row.totalNs = nanos(Clock::now() - begin); row.ownerCycles = cycles() - cpu;
        summary.rows.push_back(row);
        if (i == 0) summary.firstFrame = FSSamplingAlloc::snapshot();
        // Correctness-only oracle work is outside the measured owner intervals.
        if (correctness)
        {
            if (mutation == 2) for (auto& character : expected.characters) character->setSkeletonSerialNum(17);
            Request unused; Result unusedResult; Frame oracle(expected, Mode::Direct, 0, nullptr, unused, unusedResult, nullptr);
            oracle.callbackTrace = &expectedCallbacks; expected.update(); oracle.finish();
            require(scene.snapshot() == expected.snapshot(), "Controller/joint/hand state differs from direct");
            require(actualCallbacks == expectedCallbacks, "Controller callback order differs from direct");
            actualCallbacks.clear(); expectedCallbacks.clear();
            require(row.applied + row.fallback == row.callbacks, "Callback accounting lost work");
            if (mode == Mode::Owned || (mode != Mode::Direct && load == "normal"))
            {
                const bool exercised = mutation ? row.applied == 0 && row.stale > 0 : row.applied > 0;
                if (!exercised) std::cerr << "case=" << name(mode) << ",avatars=" << avatars << ",frame=" << i << ",mutation=" << mutation
                    << ",accepted=" << row.accepted << ",rejected=" << row.rejected << ",applied=" << row.applied << ",stale=" << row.stale
                    << ",checks=" << row.readinessChecks << ",misses=" << row.pendingMisses << ",failed=" << row.failedBatches << '\n';
                require(exercised, "Ready/current or stale oracle not exercised");
            }
            if (load != "normal") require(!row.applied && row.fallback > 0, "Busy/closed path applied unavailable work");
        }
    }
    if (executor) executor->close();
    if (load == "busy" && pool) release.set_value();
    if (pool) pool->close(); // Acknowledge abandoned work before reading owned trace fields.
    // This fixed-cohort experiment bounds retention inside Scene's lifetime.
    // A general viewer cache/deprecated-motion resolver is deliberately not installed.
    for (auto& character : scene.characters) for (auto* base : character->getMotionController().getActiveMotions())
    {
        auto* motion = static_cast<Motion*>(base); const auto bytes = motion->ownedSamplingBindingBytes();
        summary.retainedBindingBytes += bytes; summary.retainedBindingMotions += bytes != 0;
    }
    if (retainsBindings(mode))
    {
        require(summary.retainedBindingMotions <= avatars && summary.retainedBindingBytes <= avatars * 32 * sizeof(void*) * 2,
                "Controlled binding retention exceeded one bounded motion per character");
        const auto cleanup = Clock::now();
        for (auto& character : scene.characters) for (auto* base : character->getMotionController().getActiveMotions())
        {
            auto* motion = static_cast<Motion*>(base);
            if (motion->ownedSamplingBindingBytes()) motion->releaseOwnedSamplingBindings(motion->getOwnedSamplingInstance());
        }
        summary.finalBindingCleanupNs = nanos(Clock::now() - cleanup);
    }
    else require(summary.retainedBindingBytes == 0, "Released control retained owner bindings");
    for (auto& character : scene.characters) for (auto* base : character->getMotionController().getActiveMotions())
        summary.bindingBytesAfterCleanup += static_cast<Motion*>(base)->ownedSamplingBindingBytes();
    require(summary.bindingBytesAfterCleanup == 0, "Private binding cleanup left references behind");
    summary.poolCycles = pool ? pool->workerCycles() - poolBegin : 0;
    summary.ownerWindowCycles = cycles() - ownerBegin;
    summary.allocations = FSSamplingAlloc::snapshot(); allocationScope.stop();
    return summary;
}
void print(const Summary& summary, Mode mode, unsigned avatars, unsigned gap, const std::string& load)
{
    std::cout << "mode,avatars,gap_us,load,frame,total_ns,owner_cycles,capture_ns,submit_ns,gap_ns,consume_ns,apply_ns,fallback_ns,cleanup_ns,predicted,masked,excluded,rejected,accepted,callbacks,applied,fallback,stale,worker_cycles,delivery_ns,kernel_ns,sampled_joints,payload_capacity_bytes,pool_cycles_window,owner_cycles_window,retained_binding_bytes,retained_binding_motions,binding_bytes_after_cleanup,final_binding_cleanup_ns,readiness_checks,pending_misses,failed_batches,first_ready_callback,sampled_unused_joints\n";
    for (size_t i = 0; i < summary.rows.size(); ++i)
    {
        const auto& row = summary.rows[i]; const auto& measurement = *summary.traces[i]; const auto& trace = measurement.trace;
        std::cout << name(mode) << ',' << avatars << ',' << gap << ',' << load << ',' << i << ',' << row.totalNs << ',' << row.ownerCycles;
        for (auto value : row.ns) std::cout << ',' << value;
        std::cout << ',' << row.predicted << ',' << row.masked << ',' << row.excluded << ',' << row.rejected << ',' << row.accepted
                  << ',' << row.callbacks << ',' << row.applied << ',' << row.fallback << ',' << row.stale << ',' << measurement.workerCycles
                  << ',' << (trace.sampled ? nanos(trace.started - trace.queued) : 0) << ',' << (trace.sampled ? nanos(trace.finished - trace.started) : 0)
                  << ',' << trace.joints << ',' << trace.payloadCapacityBytes << ',' << summary.poolCycles << ',' << summary.ownerWindowCycles
                  << ',' << summary.retainedBindingBytes << ',' << summary.retainedBindingMotions << ',' << summary.bindingBytesAfterCleanup
                  << ',' << summary.finalBindingCleanupNs << ',' << row.readinessChecks << ',' << row.pendingMisses
                  << ',' << row.failedBatches << ',' << row.firstReadyCallback << ',' << trace.joints - row.applied * 32 << '\n';
    }
}
enum class Transition { Ready, NoReady, Rejected, Failed, FrameChanged, SkeletonChanged, AssetChanged, PermissionChanged };
void transitionMutation(Scene& scene, Assets& assets, Transition kind)
{
    if (kind == Transition::FrameChanged) FSCurrentMotion::Clock::count();
    if (kind == Transition::SkeletonChanged) for (auto& character : scene.characters) character->setSkeletonSerialNum(17);
    if (kind == Transition::AssetChanged) assets.values[3]->invalidateSampling();
    if (kind == Transition::PermissionChanged) for (auto& character : scene.characters) character->allowOwned = false;
}
void transitionCase(Mode mode, unsigned avatars, unsigned at, Transition kind)
{
    auto expect = [&](bool value, const char* message) {
        if (!value) std::cerr << "transition,mode=" << name(mode) << ",avatars=" << avatars << ",at=" << at << ",kind=" << unsigned(kind) << '\n';
        require(value, message);
    };
    expect(isQueued(mode) && retainsBindings(mode) && avatars >= 2 && avatars <= 32 && at >= 2 && at <= avatars,
           "Invalid controlled transition");
    FSCurrentMotion::Clock::set(100.0); Assets assets; Scene scene(avatars), expected(avatars); Controls controls;
    Pool pool(mode); pool.startMeasured();
    expect(pool.getWidth() == 1 && pool.getWaitPolicy() == (usesEventWait(mode) ? Policy::EventWaitSingleWorker : Policy::PlatformDefault),
           "Transition pool selection differs");
    const auto queue = pool.getQueue().getWeak();
    Executor executor(1, [queue, kind](const Executor::Work& work) {
        if (kind == Transition::Rejected) return false;
        auto live = queue.lock(); return live && live->post(work);
    });
    DeliveryControl delivery; // Must open before executor/pool destruction on every exit.
    for (unsigned i = 0; i < 8; ++i)
    { FSCurrentMotion::Clock::set(100.0 + (i + 1) / 64.0); scene.update(); expected.update(); }
    FSCurrentMotion::Clock::set(100.0 + 9 / 64.0);
    Request input; Result output; auto measurement = std::make_shared<Measurement>();
    Row row; std::vector<Callback> actualCallbacks, expectedCallbacks;
    actualCallbacks.reserve(avatars); expectedCallbacks.reserve(avatars);
    auto waitTerminal = [&](Frame& frame) {
        expect(bool(frame.ticket), "Controlled transition lost its admitted ticket");
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (true)
        {
            const auto status = executor.status(*frame.ticket);
            if (status == Status::Ready || status == Status::Failed || status == Status::Expired) break;
            if (Clock::now() >= deadline) expect(false, "Controlled transition did not acknowledge");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    {
        Frame frame(scene, mode, 0, &executor, input, output, measurement);
        frame.delivery = delivery.data; frame.failSampling = kind == Transition::Failed;
        expect(!FSSampling::NumericContext{}.isSupported(), "Failure control must supply unsupported numeric context");
        frame.callbackTrace = &actualCallbacks;
        frame.beforeCallback = [&](Frame& current) {
            if (kind == Transition::Rejected) return;
            if (current.row.callbacks == 1) await(delivery.entered); // Prove Running, not a timing guess.
            if (current.row.callbacks == at && kind != Transition::NoReady)
            {
                delivery.open(); waitTerminal(current);
                transitionMutation(scene, assets, kind); // Mutate only after owned sampling has stopped.
            }
        };
        scene.update(); frame.finish(); row = frame.row;
        if (kind == Transition::NoReady)
            expect(frame.ticket && executor.status(*frame.ticket) == Status::Discarding, "Frame finish did not abandon held work");
    }
    delivery.open(); executor.close(); pool.close(); // No worker can touch observation fields after join.
    {
        Request unused; Result unusedResult; Frame oracle(expected, Mode::Direct, 0, nullptr, unused, unusedResult, nullptr);
        oracle.callbackTrace = &expectedCallbacks;
        oracle.beforeCallback = [&](Frame& current) {
            if (current.row.callbacks == at && kind != Transition::NoReady && kind != Transition::Rejected)
                transitionMutation(expected, assets, kind);
        };
        expected.update(); oracle.finish();
    }
    expect(scene.snapshot() == expected.snapshot(), "Transition changed controller/joint/hand state");
    expect(actualCallbacks == expectedCallbacks, "Transition changed character/motion/time callback order");
    expect(row.callbacks == avatars && row.predicted == avatars && row.masked == avatars * 3 &&
           row.applied + row.fallback == avatars, "Transition accounting lost callbacks or admission");
    const bool later = checksLaterCallbacks(mode), rejected = kind == Transition::Rejected, held = kind == Transition::NoReady;
    const bool stale = kind == Transition::FrameChanged || kind == Transition::SkeletonChanged ||
                       kind == Transition::AssetChanged || kind == Transition::PermissionChanged;
    const unsigned expectedChecks = rejected ? 0 : (later ? (held ? avatars : at) : 1);
    expect(row.readinessChecks == expectedChecks, "Transition made an unexpected number of readiness checks");
    expect(row.pendingMisses == (rejected ? 0 : (later && !held ? at - 1 : expectedChecks)), "Transition missed expected early fallback");
    expect(row.firstReadyCallback == (later && !held && !rejected && kind != Transition::Failed ? at : 0), "Transition became ready at the wrong callback");
    expect(row.applied == (later && kind == Transition::Ready ? avatars - at + 1 : 0), "Transition applied wrong or stale work");
    expect(row.stale == (later && stale ? avatars - at + 1 : 0), "Between-callback staleness was not rejected");
    expect(row.failedBatches == (later && kind == Transition::Failed ? 1 : 0), "Failed batch did not stop consumption");
    expect(row.accepted == (rejected ? 0 : 1) && row.rejected == (rejected ? 1 : 0), "Controlled admission differs");
    expect(measurement->trace.joints == (rejected || kind == Transition::Failed ? 0 : avatars * 32), "Owned sampling count differs");
    if (rejected) expect(delivery.entered.wait_for(std::chrono::seconds(0)) != std::future_status::ready, "Rejected work was delivered");
    size_t heldBytes = 0, after = 0;
    for (auto& character : scene.characters) for (auto* base : character->getMotionController().getActiveMotions())
    {
        auto* motion = static_cast<Motion*>(base); heldBytes += motion->ownedSamplingBindingBytes();
        if (motion->ownedSamplingBindingBytes()) motion->releaseOwnedSamplingBindings(motion->getOwnedSamplingInstance());
        after += motion->ownedSamplingBindingBytes();
    }
    expect(heldBytes == avatars * 512 && after == 0, "Controlled retention bound or final cleanup differs");
}
void transitions()
{
    for (auto mode : {Mode::EventFirstRetained, Mode::EventLaterRetained, Mode::DefaultLaterRetained})
        for (unsigned avatars : {8u, 32u})
        {
            for (unsigned at : {2u, avatars / 2, avatars}) transitionCase(mode, avatars, at, Transition::Ready);
            for (auto kind : {Transition::NoReady, Transition::Rejected, Transition::Failed}) transitionCase(mode, avatars, avatars / 2, kind);
            if (checksLaterCallbacks(mode)) for (auto kind : {Transition::FrameChanged, Transition::SkeletonChanged,
                                                           Transition::AssetChanged, Transition::PermissionChanged})
                transitionCase(mode, avatars, avatars / 2, kind);
        }
}
int check()
{
    for (auto mode : {Mode::Direct, Mode::Owned, Mode::OwnedRetained, Mode::Default, Mode::Event,
                     Mode::EventFirstRetained, Mode::EventLaterRetained, Mode::DefaultLaterRetained})
        for (unsigned avatars : {1u, 8u, 32u}) run(mode, avatars, 0, 64, "normal", true);
    for (auto mode : {Mode::Default, Mode::Event, Mode::EventFirstRetained, Mode::EventLaterRetained, Mode::DefaultLaterRetained})
    {
        for (const auto* load : {"busy", "closed"}) run(mode, 8, 0, 3, load, true);
        for (unsigned mutation : {1u, 2u, 3u, 4u}) run(mode, 8, 0, 1, "normal", true, mutation);
    }
    for (unsigned mutation : {1u, 2u, 3u, 4u}) run(Mode::OwnedRetained, 8, 0, 1, "normal", true, mutation);
    transitions();
    std::cout << checks.load() << " complete-controller, exact-pose/order/hand, current/stale and bounded fallback checks passed\n";
    return 0;
}
}
int main(int argc, char** argv)
{
    using namespace FSEventSampling;
    if (!FSSamplingTest::supportedContext("event controller cost")) return 0;
    LLVector4a::initClass();
    if (argc == 2 && std::string(argv[1]) == "--check") return check();
    if (argc == 7 && ((std::string(argv[1]) == "--benchmark" && !FS_SAMPLING_ALLOCATION_PROBE) ||
                     (std::string(argv[1]) == "--allocations" && FS_SAMPLING_ALLOCATION_PROBE)))
    {
        const auto mode = parseMode(argv[2]); const auto avatars = std::stoul(argv[3]); const auto gap = std::stoul(argv[4]);
        const auto frames = std::stoul(argv[5]); const std::string load = argv[6]; TimerPeriod period;
        require(period.accepted == 1, "Requested timer control unavailable");
        const auto result = run(mode, avatars, gap, frames, load);
        if (FS_SAMPLING_ALLOCATION_PROBE)
        {
            const auto& value = result.allocations;
            std::cout << "mode,avatars,gap_us,load,frames,new_calls,requested_bytes,baseline_bytes,live_bytes,peak_bytes,first_frame_calls,first_frame_bytes,later_calls,later_bytes,retained_binding_bytes,retained_binding_motions,binding_bytes_after_cleanup\n"
                      << name(mode) << ',' << avatars << ',' << gap << ',' << load << ',' << frames << ',' << value.calls << ',' << value.requested
                      << ',' << value.baseline << ',' << value.live << ',' << value.peak << ',' << result.firstFrame.calls << ',' << result.firstFrame.requested
                      << ',' << value.calls - result.firstFrame.calls << ',' << value.requested - result.firstFrame.requested
                      << ',' << result.retainedBindingBytes << ',' << result.retainedBindingMotions << ',' << result.bindingBytesAfterCleanup << '\n';
        }
        else print(result, mode, avatars, gap, load);
        return 0;
    }
    std::cout << "Use --check, or --benchmark mode avatars gap_us frames normal|busy|closed.\n"
                 "Modes: direct, owned, owned-retained, default, event, event-first-retained, event-later-retained, default-later-retained.\n"
                 "Separate allocation build uses --allocations. Synthetic gaps/private cohorts do not establish viewer FPS or a live deadline.\n";
    return argc == 1 ? 0 : 1;
}
