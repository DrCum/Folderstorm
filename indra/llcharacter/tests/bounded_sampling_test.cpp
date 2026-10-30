// Actual keyframe sampling through a bounded, test-only executor.
#include "linden_common.h"
#include "llkeyframemotion.h"
#include "sampling_test_context.h"
#include "bounded_sampling_executor.h"
#include "threadpool.h"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace BoundedSamplingTest
{
unsigned checks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
class Probe : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    using Joint = JointMotion;
    using Request = SamplingRequest;
    using Result = SamplingResult;
    using SK = ScaleKey;
    using RK = RotationKey;
    using PK = PositionKey;
    static bool sample(Request request, Result& result) { return sampleKeyframes(std::move(request), result); }
    static bool matches(const Result& result, const Request& current) { return samplingResultMatches(result, current); }
};
constexpr U32 ALL = LLJointState::POS | LLJointState::ROT | LLJointState::SCALE;
std::shared_ptr<Probe::Asset> asset(unsigned count = 3)
{
    auto data = std::make_shared<Probe::Asset>();
    for (unsigned j = 0; j < count; ++j)
    {
        auto* joint = new Probe::Joint;
        joint->mUsage = ALL;
        for (unsigned k = 0; k < 4; ++k)
        {
            const F32 time = static_cast<F32>(k) * 0.5f, value = static_cast<F32>(j + k) / 8.f;
            joint->mScaleCurve.mKeys[time] = Probe::SK(time, LLVector3(value, 2.f, -0.f));
            joint->mRotationCurve.mKeys[time] = Probe::RK(time, LLQuaternion(value, LLVector3::x_axis));
            joint->mPositionCurve.mKeys[time] = Probe::PK(time, LLVector3(7.f, value, -value));
        }
        joint->mScaleCurve.mNumKeys = joint->mRotationCurve.mNumKeys = joint->mPositionCurve.mNumKeys = 4;
        data->mJointMotionArray.push_back(joint);
    }
    return data;
}
Probe::Request request(const std::shared_ptr<Probe::Asset>& data, U64 frame = 1)
{
    Probe::Request input;
    input.numericContext = FSSampling::NumericContext::capture();
    input.asset = data; input.identity = {19, 23, frame, 31, 41};
    input.time = 0.625f; input.duration = 1.5f;
    input.usage.resize(data->getNumJointMotions(), ALL);
    return input;
}
void oracle(const Probe::Result& result, const Probe::Request& current, const std::shared_ptr<Probe::Asset>& data)
{
    require(Probe::matches(result, current), "Ready result mismatches independently prepared current state");
    for (size_t j = 0; j < result.getJoints().size(); ++j)
    {
        LLPointer<LLJointState> expected = new LLJointState;
        expected->setUsage(current.usage[j]);
        data->getJointMotion(static_cast<U32>(j))->update(expected.get(), current.time, current.duration);
        const auto& row = result.getJoints()[j];
        require(row.usage == current.usage[j], "Executor changed used channels");
        require(std::memcmp(row.scale.mV, expected->getScale().mV, sizeof(row.scale.mV)) == 0 &&
                std::memcmp(row.rotation.mQ, expected->getRotation().mQ, sizeof(row.rotation.mQ)) == 0 &&
                std::memcmp(row.position.mV, expected->getPosition().mV, sizeof(row.position.mV)) == 0,
                "Executor output differs from actual serial joint update");
    }
}
struct ManualQueue
{
    std::deque<LL::WorkQueueBase::Work> work;
    bool accept = true, throws = false;
    bool submit(const LL::WorkQueueBase::Work& callback)
    {
        if (throws) throw std::runtime_error("injected submission exception");
        if (!accept) return false;
        work.push_back(callback); return true;
    }
    void one()
    {
        auto next = std::move(work.front()); work.pop_front(); next();
    }
};
using Executor = FSSamplingTest::BoundedExecutor<Probe>;
using Status = Executor::Status;
void queuedAndReady()
{
    ManualQueue queue;
    Executor executor(2, [&](const auto& work) { return queue.submit(work); });
    auto data = asset(), other = asset();
    auto a = request(data, 1), b = request(data, 2), c = request(data, 3);
    const auto* originalBuffer = a.usage.data();
    auto first = executor.trySubmit(a), second = executor.trySubmit(b);
    require(first && second && !a.asset && !b.asset, "Accepted requests were not moved");
    require(executor.status(*first) == Status::Queued && queue.work.size() == 2, "Queued state or capacity changed");
    require(!executor.trySubmit(c) && c.asset == data, "Saturation consumed the fallback request");
    Probe::Result output;
    require(executor.take(*first, output) == Status::Queued && !output.isReady(), "Queued input was readable as output");
    auto duplicate = queue.work.front();
    queue.one(); duplicate();
    require(executor.take(*first, output) == Status::Ready, "Completed sample was not published");
    require(output.getRequest().usage.data() == originalBuffer, "Executor copied moved request masks");
    auto current = request(data, 1); oracle(output, current, data);
    require(executor.status(*first) == Status::Expired, "Taken ticket stayed live");
    auto third = executor.trySubmit(c);
    require(third && executor.status(*third) == Status::Queued, "Consumed slot did not recycle");
    duplicate();
    require(executor.status(*third) == Status::Queued, "Old-generation callback claimed recycled storage");
    queue.one(); // Second request.
    require(executor.take(*second, output) == Status::Ready, "Second slot lost its output");
    oracle(output, request(data, 2), data);
    queue.one();
    require(executor.take(*third, output) == Status::Ready, "Recycled slot did not sample");
    oracle(output, request(data, 3), data);
    auto stale = request(data, 4);
    require(!Probe::matches(output, stale), "Stale frame would be applied");
    require(!Probe::matches(output, request(other, 3)), "Different asset would be applied");
    Executor foreign(2, [&](const auto& work) { return queue.submit(work); });
    require(foreign.status(*third) == Status::Expired && foreign.cancel(*third) == Status::Expired,
            "Ticket from another executor was accepted");
}
void cancellationBounds()
{
    ManualQueue queue;
    Executor executor(2, [&](const auto& work) { return queue.submit(work); });
    auto data = asset();
    auto a = request(data), b = request(data, 2);
    auto first = executor.trySubmit(a), second = executor.trySubmit(b);
    require(first && second, "Cancellation fixture did not fill");
    require(executor.cancel(*first) == Status::Queued && executor.cancel(*second) == Status::Queued,
            "Queued cancellation did not claim input");
    for (unsigned i = 0; i < 1000; ++i)
    {
        auto fallback = request(data, i + 3);
        require(!executor.trySubmit(fallback) && fallback.asset == data && queue.work.size() == executor.capacity(),
                "Cancelled but unacknowledged callbacks allowed queue growth");
    }
    auto late = queue.work.front(); queue.one();
    require(executor.status(*first) == Status::Expired, "Cancelled callback did not acknowledge");
    auto next = request(data, 1004); auto ticket = executor.trySubmit(next);
    require(ticket && queue.work.size() == 2, "Acknowledged slot was not reusable");
    late();
    require(executor.status(*ticket) == Status::Queued, "Duplicate cancellation cleaned a newer generation");
    queue.one(); queue.one();
    require(executor.cancel(*ticket) == Status::Ready && executor.status(*ticket) == Status::Expired,
            "Ready cancellation did not release input");
    executor.close(); executor.close();
    auto after = request(data);
    require(!executor.trySubmit(after) && after.asset == data, "Closed executor admitted work");
    Executor zero(0, [&](const auto& work) { return queue.submit(work); });
    require(!zero.trySubmit(after), "Zero-capacity executor admitted work");
}
void rejectionAndFailure()
{
    ManualQueue queue;
    Executor executor(1, [&](const auto& work) { return queue.submit(work); });
    auto data = asset(); auto input = request(data);
    const auto* buffer = input.usage.data();
    queue.accept = false;
    require(!executor.trySubmit(input) && input.asset == data && input.usage.data() == buffer && queue.work.empty(),
            "Queue rejection did not restore input for serial fallback");
    queue.throws = true;
    bool threw = false;
    try { executor.trySubmit(input); } catch (const std::runtime_error&) { threw = true; }
    require(threw && input.asset == data && input.usage.data() == buffer, "Submission exception lost input");
    queue.accept = true; queue.throws = false;
    input.usage.pop_back();
    auto ticket = executor.trySubmit(input); require(bool(ticket), "Failure fixture rejected valid admission");
    queue.one();
    Probe::Result output;
    require(executor.status(*ticket) == Status::Failed && executor.take(*ticket, output) == Status::Failed &&
            !output.isReady(), "Invalid sampling shape published a ready output");
    input = request(data); ticket = executor.trySubmit(input); queue.one();
    require(executor.take(*ticket, output) == Status::Ready, "Failed slot was not reusable");
    oracle(output, request(data), data);
    // Inline successful delivery is legal; rejection after delivery is not.
    Executor inlineExecutor(1, [](const auto& work) { work(); return true; });
    input = request(data); ticket = inlineExecutor.trySubmit(input);
    require(ticket && inlineExecutor.take(*ticket, output) == Status::Ready, "Inline delivery lost completion");
}
struct Gate
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    void block()
    {
        std::unique_lock lock(mutex); entered = true; changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; })) std::exit(2);
    }
    void await()
    {
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }), "Worker failed to enter explicit gate");
    }
    void release() { std::lock_guard lock(mutex); released = true; changed.notify_all(); }
};
struct GatedProbe : Probe
{
    static Gate* gate;
    static bool throws;
    static std::atomic<unsigned> calls;
    static bool sample(Request input, Result& result)
    {
        ++calls;
        if (gate) gate->block();
        if (throws) throw std::runtime_error("injected sampler exception");
        return Probe::sample(std::move(input), result);
    }
};
Gate* GatedProbe::gate = nullptr;
bool GatedProbe::throws = false;
std::atomic<unsigned> GatedProbe::calls{0};
void runningAndDestruction()
{
    using Gated = FSSamplingTest::BoundedExecutor<GatedProbe>;
    using S = Gated::Status;
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        ManualQueue queue;
        auto executor = std::make_unique<Gated>(1, [&](const auto& work) { return queue.submit(work); });
        auto data = asset(); std::weak_ptr<Probe::Asset> life = data;
        auto input = request(data); auto current = request(data);
        Gate gate; GatedProbe::gate = &gate; GatedProbe::calls = 0;
        auto ticket = executor->trySubmit(input); require(bool(ticket), "Running fixture rejected work");
        auto duplicate = queue.work.front();
        auto work = std::move(queue.work.front()); queue.work.pop_front();
        std::thread worker(std::move(work)); gate.await();
        require(executor->status(*ticket) == S::Running, "Worker did not exclusively claim slot");
        std::thread duplicateWorker(std::move(duplicate)); duplicateWorker.join();
        require(GatedProbe::calls == 1, "Contending duplicate callback sampled the same payload");
        if (mode == 0) require(executor->cancel(*ticket) == S::Running, "Running cancellation did not abandon");
        else if (mode == 1) executor->close();
        Probe::Result fallback;
        input = request(data);
        require(Probe::sample(std::move(input), fallback), "Serial fallback failed");
        oracle(fallback, current, data);
        auto rejected = request(data);
        require(!executor->trySubmit(rejected) && rejected.asset == data, "Running buffer was recycled before acknowledgement");
        current.asset.reset(); rejected.asset.reset(); fallback.takeRequest(); data.reset();
        require(!life.expired(), "Running cancellation released an in-use asset");
        if (mode == 2) executor.reset();
        gate.release(); worker.join(); GatedProbe::gate = nullptr;
        require(life.expired(), "Abandoned worker retained its input lease after completion");
        if (mode != 2) require(executor->status(*ticket) == S::Expired, "Abandoned result became readable");
    }
    ManualQueue queue;
    auto data = asset(); auto input = request(data);
    Gated executor(1, [&](const auto& work) { return queue.submit(work); });
    GatedProbe::throws = true;
    auto ticket = executor.trySubmit(input); queue.one(); GatedProbe::throws = false;
    Probe::Result output;
    require(ticket && executor.take(*ticket, output) == S::Failed && !output.isReady(), "Worker exception escaped or published readiness");
    input = request(data); ticket = executor.trySubmit(input); queue.one();
    require(executor.take(*ticket, output) == S::Ready, "Exception slot did not recover");
    oracle(output, request(data), data);
    // Queued callbacks outlive both executor and submitter without capturing either.
    LL::WorkQueueBase::Work delayed;
    std::weak_ptr<Probe::Asset> life;
    {
        ManualQueue temporary;
        auto owned = asset(); life = owned;
        Executor shortLived(1, [&](const auto& work) { return temporary.submit(work); });
        auto pending = request(owned); require(bool(shortLived.trySubmit(pending)), "Delayed fixture failed");
        delayed = std::move(temporary.work.front());
    }
    require(!life.expired(), "Undelivered cancelled callback lost its owned payload");
    delayed(); require(life.expired(), "Cancelled callback did not release its owned payload");
}
void realQueue()
{
    auto data = asset(); Probe::Result output;
    LL::WorkQueue queue("FSBoundedSamplingManual", 1, false);
    Executor executor(2, FSSamplingTest::queueSubmitter(queue.getWeak(), 1));
    auto input = request(data); auto ticket = executor.trySubmit(input);
    require(bool(ticket), "Actual tryPost rejected empty queue");
    auto rejected = request(data, 2);
    require(!executor.trySubmit(rejected) && rejected.asset == data && queue.size() == 1,
            "Actual full queue did not reject without consuming input");
    queue.runOne();
    require(executor.take(*ticket, output) == Status::Ready, "Actual WorkQueue did not deliver sample");
    oracle(output, request(data), data);
    Executor disabled(1, FSSamplingTest::queueSubmitter(queue.getWeak(), 0));
    require(!disabled.trySubmit(rejected) && queue.size() == 0, "Zero-worker adapter accepted work");
    queue.close();
    require(!executor.trySubmit(rejected) && rejected.asset == data, "Closed actual queue accepted work");
    Executor::Submit expired;
    {
        LL::WorkQueue temporary("FSBoundedSamplingExpired", 1, false);
        expired = FSSamplingTest::queueSubmitter(temporary.getWeak(), 1);
    }
    Executor gone(1, expired);
    require(!gone.trySubmit(rejected) && rejected.asset == data, "Expired queue was dereferenced");
    // An actual worker runs the sampler; explicit pool close drains and joins it.
    LL::ThreadPool pool("FSBoundedSamplingWorker", 1, 4, false);
    Executor threaded(2, FSSamplingTest::queueSubmitter(pool.getQueue().getWeak(), 1));
    input = request(data, 3); ticket = threaded.trySubmit(input);
    require(bool(ticket), "Actual pool admission failed");
    pool.start(); require(pool.getWidth() == 1, "Actual worker width differed from requested test width");
    pool.close();
    require(threaded.take(*ticket, output) == Status::Ready, "Actual pool did not drain accepted work before joining");
    oracle(output, request(data, 3), data);
    rejected = request(data, 4);
    require(!threaded.trySubmit(rejected), "Closed worker queue admitted work");
}
int run()
{
    if (!FSSamplingTest::supportedContext("bounded sampling")) return 0;
    queuedAndReady(); cancellationBounds(); rejectionAndFailure(); runningAndDestruction(); realQueue();
    std::cout << checks << " bounded sampling ownership/admission/lifecycle checks passed\n";
    return 0;
}
}
int main() { return BoundedSamplingTest::run(); }
