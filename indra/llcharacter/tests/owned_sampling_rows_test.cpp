#include "linden_common.h"
#include "owned_sampling_rows.h"
#include "sampling_cost_fixture.h"
#include "sampling_test_context.h"
#include "llsimdmath.h"
#include <deque>
#include <iostream>
#include <thread>

namespace FSOwnedRowsTest
{
using namespace FSOwnedRows;
unsigned checks = 0;
void require(bool value, const char* message)
{ ++checks; if (!value) { std::cerr << message << '\n'; std::_Exit(2); } }
void fill(BufferPool::Lease& lease, FSSamplingCost::Fixture& fixture)
{
    for (const auto& current : fixture.current)
    {
        auto& input = lease.add(); input.asset = current.asset; input.identity = current.identity;
        input.numericContext = current.numericContext; input.time = current.time; input.duration = current.duration; input.usage = current.usage;
    }
}
void matched(BufferPool::Lease& lease, const FSSamplingCost::Fixture& fixture, size_t index)
{
    const auto* result = lease.ready(index);
    require(result && result->isReady(), "Published row was not ready");
    require(FSSamplingCost::Probe::matches(*result, fixture.current[index]), "Published row lost its independent query identity");
}
void deliver(std::deque<Executor::Work>& callbacks)
{
    require(!callbacks.empty(), "Expected owned callback was not retained");
    auto work = std::move(callbacks.front()); callbacks.pop_front(); work();
}
void runningAndReuse()
{
    FSSamplingCost::Fixture fixture({8, 32, 32, 8}); fixture.prepareCurrent(1);
    std::deque<Executor::Work> callbacks;
    Executor executor(1, [&](const Executor::Work& work) { callbacks.push_back(work); return true; });
    BufferPool buffers(executor);
    auto first = buffers.acquire(); require(bool(first), "First row buffer unavailable"); fill(*first, fixture);
    std::promise<void> release;
    auto gate = std::make_shared<Gate>(1, release.get_future().share()); auto entered = gate->entered.get_future(); first->setGate(gate);
    require(first->submit() && callbacks.size() == 1, "First row callback admission failed");
    auto work = std::move(callbacks.front()); callbacks.pop_front();
    std::thread worker([work] { work(); });
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Owned row gate did not enter"); entered.get();
    matched(*first, fixture, 0);
    require(first->state(1) == Published::Empty && !first->ready(1), "Unfinished row became visible");
    first->cancel(); matched(*first, fixture, 0);
    auto second = buffers.acquire(); require(bool(second), "Second bounded buffer unavailable"); fill(*second, fixture);
    require(!second->submit(), "Busy one-slot executor admitted another callback");
    bool duplicateRejected = false;
    try { second->submit(); } catch (const std::logic_error&) { duplicateRejected = true; }
    require(duplicateRejected, "A buffer retried submission in one epoch");
    first->finish();
    require(!buffers.acquire(), "Running abandoned buffer reused before acknowledgement");
    release.set_value(); worker.join();
    auto next = buffers.acquire(); require(bool(next), "Acknowledged buffer was not reusable");
    require(!next->ready(0), "New buffer epoch retained readiness");
    fixture.prepareCurrent(2); fill(*next, fixture); require(next->submit(), "Reused buffer could not submit"); deliver(callbacks);
    for (size_t index = 0; index < fixture.current.size(); ++index) matched(*next, fixture, index);
    next->cancel(); matched(*next, fixture, 0); // Ready discard must release only the group reference.
    require(!buffers.acquire(), "Acknowledged buffer reused while its owner was still reading");
    next->finish(); second->finish();
    auto final = buffers.acquire(); require(bool(final) && !final->ready(0), "Retired epoch was not reset safely");
}
void queuedAndFailed()
{
    FSSamplingCost::Fixture fixture({3, 32, 32, 3}); fixture.prepareCurrent(3);
    std::deque<Executor::Work> callbacks;
    Executor executor(1, [&](const Executor::Work& work) { callbacks.push_back(work); return true; });
    BufferPool buffers(executor);
    auto queued = buffers.acquire(); fill(*queued, fixture); require(queued->submit(), "Queued case did not submit"); queued->finish();
    auto other = buffers.acquire(); require(bool(other), "Queued case has no spare buffer");
    require(!buffers.acquire(), "Cancelled queued buffer reused before delivery acknowledgement");
    deliver(callbacks);
    auto next = buffers.acquire(); require(bool(next) && !next->ready(0), "Queued cancellation published or retained data");
    auto& a = next->add(); const auto& source = fixture.current[0];
    a.asset = source.asset; a.identity = source.identity; a.numericContext = source.numericContext;
    a.time = source.time; a.duration = source.duration; a.usage = source.usage;
    auto& failed = next->add(); failed.asset = source.asset; failed.identity = source.identity;
    require(!failed.numericContext.isSupported(), "New epoch retained old numeric context");
    failed.numericContext = {};
    failed.time = source.time; failed.duration = source.duration; failed.usage = source.usage;
    auto& c = next->add(); const auto& last = fixture.current[2];
    c.asset = last.asset; c.identity = last.identity; c.numericContext = last.numericContext;
    c.time = last.time; c.duration = last.duration; c.usage = last.usage;
    require(next->submit(), "Partial failure did not submit"); deliver(callbacks);
    matched(*next, fixture, 0); matched(*next, fixture, 2);
    require(next->state(1) == Published::Failed && !next->ready(1), "Failed row was exposed as usable");
    next->cancel(); matched(*next, fixture, 0); matched(*next, fixture, 2);
    next->finish(); other->finish();
    auto reset = buffers.acquire(); require(bool(reset) && reset->state(0) == Published::Empty, "Failed epoch did not retire");
}
void stoppedRows()
{
    FSSamplingCost::Fixture fixture({8, 32, 32, 8}); fixture.prepareCurrent(4);
    std::deque<Executor::Work> callbacks;
    Executor executor(1, [&](const Executor::Work& work) { callbacks.push_back(work); return true; });
    BufferPool buffers(executor); auto lease = buffers.acquire(); fill(*lease, fixture);
    std::promise<void> release;
    auto gate = std::make_shared<Gate>(1, release.get_future().share()); auto entered = gate->entered.get_future(); lease->setGate(gate);
    require(lease->submit(), "Stopped-row case did not submit");
    auto work = std::move(callbacks.front()); callbacks.pop_front(); std::thread worker([work] { work(); });
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Stopped-row gate did not enter"); entered.get();
    lease->cancel(); release.set_value(); worker.join();
    matched(*lease, fixture, 0); matched(*lease, fixture, 1); // Already-started row may finish safely.
    for (size_t index = 2; index < fixture.current.size(); ++index)
        require(lease->state(index) == Published::Empty && !lease->ready(index), "Cancellation started a later row");
    bool editRejected = false;
    try { lease->add(); } catch (const std::logic_error&) { editRejected = true; }
    require(editRejected, "Submitted row storage was mutable through its owner API");
}
void boundsAndEmpty()
{
    std::deque<Executor::Work> callbacks;
    Executor executor(1, [&](const Executor::Work& work) { callbacks.push_back(work); return true; });
    BufferPool buffers(executor); auto empty = buffers.acquire(); auto spare = buffers.acquire();
    require(empty && spare && !buffers.acquire(), "Row group count exceeded two");
    require(!empty->submit() && callbacks.empty(), "Empty group posted useless work"); empty->finish();
    bool endedReadRejected = false;
    try { empty->ready(0); } catch (const std::logic_error&) { endedReadRejected = true; }
    require(endedReadRejected, "Read API accepted a finished owner lease");
    auto full = buffers.acquire(); require(bool(full), "Unsubmitted buffer did not retire");
    for (size_t index = 0; index < MAX_ROWS; ++index) full->add();
    bool overflowRejected = false;
    try { full->add(); } catch (const std::logic_error&) { overflowRejected = true; }
    require(overflowRejected, "Row group exceeded fixed capacity");
    full->cancel(); require(!full->submit() && callbacks.empty(), "Pre-cancelled group posted work");
}
int run()
{
    if (!FSSamplingTest::supportedContext("owned row publication")) return 0;
    LLVector4a::initClass(); runningAndReuse(); queuedAndFailed(); stoppedRows(); boundsAndEmpty();
    std::cout << checks << " owned-row publication, cancellation and acknowledged-reuse checks passed\n";
    return 0;
}
}
int main() { return FSOwnedRowsTest::run(); }
