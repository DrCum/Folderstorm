#include "linden_common.h"
#include "sampling_cost_fixture.h"
#include "sampling_test_context.h"
#include <cstdlib>
#include <deque>
#include <iostream>

namespace SamplingBatchTest
{
using namespace FSSamplingCost;
unsigned checks = 0;
void require(bool value, const char* text)
{
    if (!value) { std::cerr << text << '\n'; std::exit(1); }
    ++checks;
}
void correctnessAndReuse()
{
    for (unsigned motions : {1u, 9u, 33u}) for (unsigned keys : {1u, 32u, 128u}) for (bool seek : {false, true})
    {
        Config config{motions, 17, keys, 8, seek}; Fixture actual(config), expected(config);
        const unsigned groups = (motions + config.batch - 1) / config.batch;
        std::vector<BatchRequest> inputs(groups);
        std::vector<BatchResult> outputs(groups);
        std::vector<const void*> envelopes(groups), masks(groups), poses(groups);
        for (unsigned frame = 1; frame <= 7; ++frame)
        {
            actual.prepareCurrent(frame); expected.direct(frame, 0, motions);
            for (unsigned g = 0; g < groups; ++g)
            {
                const unsigned first = g * config.batch, count = std::min(config.batch, motions - first);
                actual.prepareBatch(inputs[g], first, count, nullptr);
                if (frame > 1) require(inputs[g].queries.data() == envelopes[g] &&
                    inputs[g].queries[0].usage.data() == masks[g], "Batch recycling reallocated the envelope or masks");
                envelopes[g] = inputs[g].queries.data(); masks[g] = inputs[g].queries[0].usage.data();
                require(BatchSampler::sample(std::move(inputs[g]), outputs[g]), "Valid uneven batch failed");
                require(actual.validate(outputs[g], first, count), "Valid batch failed independent current-state validation");
                if (frame > 1) require(outputs[g].rows[0].getJoints().data() == poses[g], "Output joint capacity was discarded");
                poses[g] = outputs[g].rows[0].getJoints().data();
                actual.apply(outputs[g], first); inputs[g] = outputs[g].recycle();
                require(!outputs[g].ready && !outputs[g].rows[0].isReady(), "Recycled batch retained readiness");
            }
            require(actual.exact(expected), "Batch application changed pose bytes or state metadata");
        }
    }
}
void failureAndBarrier()
{
    Config config{9, 17, 32, 8, true}; Fixture actual(config), expected(config);
    actual.prepareCurrent(10); BatchRequest input; BatchResult output;
    actual.prepareBatch(input, 0, 8, nullptr);
    input.queries[3].usage.pop_back(); // Earlier rows succeed before this failure.
    require(!BatchSampler::sample(std::move(input), output) && !output.ready, "Partial failure published a successful batch");
    for (const auto& row : output.rows) require(!row.isReady() && !row.getRequest().asset,
                                               "Partial failure left a readable row/asset lease");
    actual.prepareBatch(input, 0, 8, nullptr);
    require(BatchSampler::sample(std::move(input), output), "Failed output could not be reused");
    ++actual.current[7].identity.edit;
    require(!actual.validate(output, 0, 8), "Late-row stale edit was accepted");
    actual.direct(10, 0, 9); expected.direct(10, 0, 9);
    require(actual.exact(expected), "Direct fallback changed current pose");
    output.takeRequest();
    require(!output.ready, "Discard retained batch readiness");
    BatchRequest empty;
    require(!BatchSampler::sample(std::move(empty), output), "Empty batch accepted");
    BatchRequest oversized; oversized.queries.resize(MAX_BATCH + 1);
    require(!BatchSampler::sample(std::move(oversized), output), "Oversized batch accepted");

    // A held callback must not cause any owner wait or next-frame stale apply.
    std::deque<LL::WorkQueueBase::Work> callbacks;
    Executor executor(1, [&](const auto& callback) { callbacks.push_back(callback); return true; });
    actual.prepareCurrent(11); actual.prepareBatch(input, 0, 8, nullptr);
    auto ticket = executor.trySubmit(input); require(bool(ticket), "Barrier fixture admission failed");
    std::vector<Executor::Ticket> outstanding;
    trackAdmission(executor, outstanding, ticket);
    require(executor.take(*ticket, output) == Executor::Status::Queued, "Held callback looked ready");
    require(executor.cancel(*ticket) == Executor::Status::Queued, "Barrier miss was not cancelled");
    actual.direct(11, 0, 9); expected.direct(11, 0, 9);
    require(actual.exact(expected), "Held callback fallback changed pose bytes");
    actual.prepareCurrent(12); actual.prepareBatch(input, 0, 8, nullptr);
    require(!executor.trySubmit(input), "Unacknowledged cancelled batch allowed resubmission");
    callbacks.front()(); callbacks.pop_front();
    ticket = executor.trySubmit(input); require(bool(ticket), "Cancelled batch did not acknowledge");
    trackAdmission(executor, outstanding, ticket);
    require(outstanding.size() == 1, "Accounting counted both generations of a reused slot");
    callbacks.front()(); callbacks.pop_front();
    require(executor.take(*ticket, output) == Executor::Status::Ready && actual.validate(output, 0, 8),
            "New-frame batch did not replace cancelled work");
    actual.apply(output, 0); actual.direct(12, 8, 1); expected.direct(12, 0, 9);
    require(actual.exact(expected), "New-frame batch mixed old and current output");
}
int run()
{
    if (!FSSamplingTest::supportedContext("owned batch sampling")) return 0;
    correctnessAndReuse(); failureAndBarrier();
    std::cout << checks << " owned batch, reusable-capacity, failure and same-frame fallback checks passed\n";
    return 0;
}
}
int main() { return SamplingBatchTest::run(); }
