// Test-only owned batching and actual-joint fixture for complete-cost measurement.
#ifndef FS_TEST_SAMPLING_COST_FIXTURE_H
#define FS_TEST_SAMPLING_COST_FIXTURE_H
#include "llkeyframemotion.h"
#include "bounded_sampling_executor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace FSSamplingCost
{
using Clock = std::chrono::steady_clock;
inline double micros(Clock::duration value) { return std::chrono::duration<double, std::micro>(value).count(); }
inline constexpr unsigned MAX_MOTIONS = 128, MAX_BATCH = 32;
inline constexpr U32 ALL = LLJointState::POS | LLJointState::ROT | LLJointState::SCALE;
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
    using Row = SampledJoint;
    static bool sample(Request request, Result& result) { return sampleKeyframes(std::move(request), result); }
    static bool matches(const Result& result, const Request& current) { return samplingResultMatches(result, current); }
};
struct Trace
{
    Clock::time_point queued, started, finished;
    size_t joints = 0, payloadCapacityBytes = 0;
    bool sampled = false;
};
struct BatchRequest
{
    std::vector<Probe::Request> queries;
    std::shared_ptr<Trace> trace; // Owned instrumentation only, never a live frame/state pointer.
};
class BatchResult
{
public:
    std::vector<Probe::Result> rows;
    std::vector<Probe::Request> envelope;
    std::shared_ptr<Trace> trace;
    bool ready = false;
    // BoundedExecutor's discard hook. No allocation or owner/executor access.
    void takeRequest()
    {
        ready = false;
        for (auto& row : rows) row.takeRequest();
        for (auto& input : envelope) input.asset.reset();
        trace.reset();
    }
    BatchRequest recycle()
    {
        if (!ready || rows.size() != envelope.size()) throw std::logic_error("Cannot recycle incomplete batch");
        ready = false;
        for (size_t i = 0; i < rows.size(); ++i) envelope[i] = rows[i].takeRequest();
        return {std::move(envelope), std::move(trace)};
    }
};
struct BatchSampler
{
    using Request = BatchRequest;
    using Result = BatchResult;
    static bool sample(Request input, Result& output)
    {
        output.takeRequest();
        output.envelope = std::move(input.queries); output.trace = std::move(input.trace);
        auto trace = output.trace;
        if (trace) trace->started = Clock::now();
        bool success = !output.envelope.empty() && output.envelope.size() <= MAX_BATCH;
        if (success)
        {
            output.rows.resize(output.envelope.size());
            for (size_t i = 0; i < output.envelope.size(); ++i)
            {
                if (!Probe::sample(std::move(output.envelope[i]), output.rows[i])) { success = false; break; }
                if (trace) trace->joints += output.rows[i].getJoints().size();
            }
        }
        if (trace)
        {
            trace->payloadCapacityBytes = output.rows.capacity() * sizeof(Probe::Result) +
                output.envelope.capacity() * sizeof(Probe::Request);
            for (const auto& row : output.rows)
                trace->payloadCapacityBytes += row.getJoints().capacity() * sizeof(Probe::Row) +
                    row.getRequest().usage.capacity() * sizeof(U32);
            trace->finished = Clock::now(); trace->sampled = true;
        }
        if (!success) { output.takeRequest(); return false; }
        output.ready = true;
        return true;
    }
};
using Executor = FSSamplingTest::BoundedExecutor<BatchSampler>;
inline void trackAdmission(const Executor& executor, std::vector<Executor::Ticket>& outstanding,
                           const std::optional<Executor::Ticket>& admitted)
{
    // Observe after trySubmit: an acknowledgement between an earlier scan and
    // submission can let the same slot be reused with a new generation.
    outstanding.erase(std::remove_if(outstanding.begin(), outstanding.end(), [&](const auto& ticket)
        { return executor.status(ticket) == Executor::Status::Expired; }), outstanding.end());
    if (admitted) outstanding.push_back(*admitted);
    if (outstanding.size() > executor.capacity()) throw std::logic_error("Accepted callbacks exceeded bounded slots");
}

struct Config
{
    unsigned motions = 32, joints = 64, keys = 32, batch = 8;
    bool seek = false;
};
inline LLPointer<LLJointState> state(U32 usage)
{
    LLPointer<LLJointState> result = new LLJointState;
    result->setUsage(ALL); result->setScale(LLVector3(2.f, 3.f, 4.f));
    result->setPosition(LLVector3(-101.f, 0.25f, 9.f));
    result->setRotation(LLQuaternion(0.3f, LLVector3::y_axis));
    result->setUsage(usage); result->setWeight(0.375f); result->setPriority(LLJoint::HIGH_PRIORITY);
    return result;
}
inline std::shared_ptr<Probe::Asset> asset(unsigned joints, unsigned keys, unsigned seed)
{
    auto data = std::make_shared<Probe::Asset>();
    for (unsigned j = 0; j < joints; ++j)
    {
        auto* motion = new Probe::Joint;
        motion->mUsage = ALL;
        for (unsigned k = 0; k < keys; ++k)
        {
            const F32 time = static_cast<F32>(k) / 30.f;
            const F32 value = static_cast<F32>((j + k + seed) % 31) / 32.f;
            motion->mScaleCurve.mKeys[time] = Probe::SK(time, LLVector3(value, 2.f + value, -0.f));
            motion->mRotationCurve.mKeys[time] = Probe::RK(time, LLQuaternion(value, LLVector3::x_axis));
            motion->mPositionCurve.mKeys[time] = Probe::PK(time, LLVector3(-value, 0.f, value / 4.f));
        }
        motion->mScaleCurve.mNumKeys = motion->mRotationCurve.mNumKeys = motion->mPositionCurve.mNumKeys = keys;
        if (j % 11 == 0) motion->mScaleCurve.mNumKeys = 0; // Preserve the original count gate.
        data->mJointMotionArray.push_back(motion);
    }
    return data;
}
class Fixture
{
public:
    Config config;
    std::vector<std::shared_ptr<Probe::Asset>> assets;
    std::vector<std::vector<LLPointer<LLJointState>>> states;
    std::vector<Probe::Request> current;
    explicit Fixture(Config spec): config(spec)
    {
        if (!spec.motions || spec.motions > MAX_MOTIONS || !spec.joints ||
            spec.joints > LL_CHARACTER_MAX_ANIMATED_JOINTS || !spec.keys || spec.keys > 128 ||
            !spec.batch || spec.batch > MAX_BATCH) throw std::logic_error("Fixture exceeds payload bounds");
        for (unsigned i = 0; i < 4; ++i) assets.push_back(asset(spec.joints, spec.keys, i));
        states.resize(spec.motions); current.resize(spec.motions);
        const U32 masks[] = {ALL, ALL, LLJointState::ROT, 0, LLJointState::POS,
                             LLJointState::SCALE, LLJointState::ROT | LLJointState::POS,
                             LLJointState::SCALE | LLJointState::POS};
        for (unsigned m = 0; m < spec.motions; ++m)
            for (unsigned j = 0; j < spec.joints; ++j) states[m].push_back(state(masks[(j + m) % 8]));
    }
    F32 duration() const { return std::max(1.f / 30.f, static_cast<F32>(config.keys - 1) / 30.f); }
    F32 time(unsigned frame, unsigned motion) const
    {
        return config.seek ? duration() * static_cast<F32>((frame * 1664525u + motion * 1013904223u) % 65536) / 65536.f :
            std::fmod(static_cast<F32>(frame + motion % 5) / 60.f, duration());
    }
    void prepareCurrent(unsigned frame)
    {
        for (unsigned m = 0; m < config.motions; ++m)
        {
            auto& input = current[m]; input.asset = assets[m % assets.size()];
            input.identity = {m + 1, 1, frame, 1, 1}; input.time = time(frame, m); input.duration = duration();
            input.numericContext = FSSampling::NumericContext::capture();
            input.usage.resize(config.joints);
            for (unsigned j = 0; j < config.joints; ++j) input.usage[j] = states[m][j]->getUsage();
        }
    }
    void prepareBatch(BatchRequest& batch, unsigned first, unsigned count, const std::shared_ptr<Trace>& trace)
    {
        batch.queries.resize(count); batch.trace = trace;
        for (unsigned i = 0; i < count; ++i)
        {
            const auto& source = current[first + i]; auto& copy = batch.queries[i];
            copy.asset = source.asset; copy.identity = source.identity;
            copy.numericContext = source.numericContext;
            copy.time = source.time; copy.duration = source.duration; copy.usage = source.usage;
        }
    }
    void direct(unsigned frame, unsigned first, unsigned count)
    {
        for (unsigned m = first; m < first + count; ++m)
        {
            const F32 query = time(frame, m), span = duration();
            for (unsigned j = 0; j < config.joints; ++j)
                assets[m % assets.size()]->getJointMotion(j)->update(states[m][j].get(), query, span);
        }
    }
    bool validate(const BatchResult& result, unsigned first, unsigned count) const
    {
        if (!result.ready || result.rows.size() != count) return false;
        for (unsigned i = 0; i < count; ++i) if (!Probe::matches(result.rows[i], current[first + i])) return false;
        return true;
    }
    void apply(const BatchResult& result, unsigned first)
    {
        for (unsigned i = 0; i < result.rows.size(); ++i)
            for (unsigned j = 0; j < result.rows[i].getJoints().size(); ++j)
            {
                auto& target = *states[first + i][j]; const auto& row = result.rows[i].getJoints()[j];
                if (row.usage & LLJointState::SCALE) target.setScale(row.scale);
                if (row.usage & LLJointState::ROT) target.setRotation(row.rotation);
                if (row.usage & LLJointState::POS) target.setPosition(row.position);
            }
    }
    bool exact(const Fixture& expected) const
    {
        for (unsigned m = 0; m < config.motions; ++m) for (unsigned j = 0; j < config.joints; ++j)
        {
            const auto& a = *states[m][j]; const auto& b = *expected.states[m][j];
            if (std::memcmp(a.getScale().mV, b.getScale().mV, sizeof(a.getScale().mV)) ||
                std::memcmp(a.getRotation().mQ, b.getRotation().mQ, sizeof(a.getRotation().mQ)) ||
                std::memcmp(a.getPosition().mV, b.getPosition().mV, sizeof(a.getPosition().mV)) ||
                a.getUsage() != b.getUsage() || a.getWeight() != b.getWeight() || a.getPriority() != b.getPriority()) return false;
        }
        return true;
    }
};
}
#endif
