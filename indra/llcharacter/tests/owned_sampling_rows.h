// Private owned-row publication primitive. No viewer caller or scene pointer.
#ifndef FS_TEST_OWNED_SAMPLING_ROWS_H
#define FS_TEST_OWNED_SAMPLING_ROWS_H
#include "llkeyframemotion.h"
#include "bounded_sampling_executor.h"
#include "sampling_cost_fixture.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace FSOwnedRows
{
inline constexpr size_t MAX_ROWS = 32, MAX_GROUPS = 2;
using Motion = LLKeyframeMotion;
enum class Published : unsigned char { Empty, Ready, Failed };
struct Observation
{
    FSSamplingCost::Trace trace;
    std::uint64_t workerCycles = 0;
    size_t sampledRows = 0, failedRows = 0;
    bool cycleReadOk = true;
};
struct Gate
{
    size_t beforeRow;
    std::promise<void> entered;
    std::shared_future<void> released;
    Gate(size_t row, std::shared_future<void> signal): beforeRow(row), released(std::move(signal)) {}
};
struct Cell
{
    Motion::SamplingRequest request;
    Motion::SamplingResult result;
    std::atomic<Published> published{Published::Empty};
};
class BufferPool;
class Group
{
    friend class BufferPool;
    friend struct Sampler;
    std::array<Cell, MAX_ROWS> cells;
    size_t count = 0;
    std::uint64_t epoch = 0;
    std::atomic<bool> cancelled{false};
    std::shared_ptr<Gate> gate; // Optional owned correctness control, not a callback.
    std::shared_ptr<Observation> observation;
    void reset(std::uint64_t next)
    {
        // BufferPool has proved owner completion AND exact ticket acknowledgement.
        for (auto& cell : cells)
        {
            auto recycled = cell.result.takeRequest();
            if (recycled.usage.capacity() >= cell.request.usage.capacity()) cell.request = std::move(recycled);
            cell.request.asset.reset();
            cell.request.identity = {}; cell.request.numericContext = {};
            cell.request.time = cell.request.duration = 0.f; cell.request.usage.clear();
            cell.published.store(Published::Empty, std::memory_order_relaxed);
        }
        count = 0; epoch = next; gate.reset(); observation.reset(); cancelled.store(false, std::memory_order_relaxed);
    }
};
struct Request { std::shared_ptr<Group> group; std::uint64_t epoch = 0; };
struct Result
{
    std::shared_ptr<Group> group;
    // Discard releases ownership only. Published cell data remains immutable.
    void takeRequest() { group.reset(); }
};
struct Sampler
{
    using Request = FSOwnedRows::Request;
    using Result = FSOwnedRows::Result;
    static bool sample(Request input, Result& output)
    {
        output.takeRequest();
        if (!input.group || input.epoch != input.group->epoch || !input.group->count || input.group->count > MAX_ROWS) return false;
        output.group = std::move(input.group);
        auto& group = *output.group;
        const auto observation = group.observation;
        std::uint64_t start = 0;
#if LL_WINDOWS
        if (observation) observation->cycleReadOk = bool(QueryThreadCycleTime(GetCurrentThread(), &start));
#endif
        if (observation) observation->trace.started = FSSamplingCost::Clock::now();
        bool complete = true;
        for (size_t index = 0; index < group.count; ++index)
        {
            if (group.cancelled.load(std::memory_order_acquire)) { complete = false; break; }
            if (group.gate && group.gate->beforeRow == index)
            {
                group.gate->entered.set_value();
                group.gate->released.wait(); // Controlled tests only; no gate in measured work.
            }
            auto& cell = group.cells[index];
            bool ok = false;
            try { ok = Motion::sampleKeyframes(std::move(cell.request), cell.result); }
            catch (...) { cell.result.takeRequest(); }
            complete &= ok;
            if (observation)
            {
                observation->sampledRows += ok; observation->failedRows += !ok;
                if (ok) observation->trace.joints += cell.result.getJoints().size();
            }
            cell.published.store(ok ? Published::Ready : Published::Failed, std::memory_order_release);
            // Do not access this cell again until its acknowledged epoch ends.
        }
        if (observation)
        {
            observation->trace.finished = FSSamplingCost::Clock::now(); observation->trace.sampled = true;
#if LL_WINDOWS
            std::uint64_t end = 0;
            observation->cycleReadOk &= bool(QueryThreadCycleTime(GetCurrentThread(), &end));
            observation->workerCycles = end - start;
#endif
        }
        return complete;
    }
};
using Executor = FSSamplingTest::BoundedExecutor<Sampler>;

class BufferPool
{
    struct Record
    {
        std::shared_ptr<Group> group = std::make_shared<Group>();
        std::optional<Executor::Ticket> ticket;
        std::uint64_t epoch = 0;
        bool ownerActive = false, attempted = false;
    };
    Executor& executor;
    std::array<Record, MAX_GROUPS> records;
    Record& active(size_t index, std::uint64_t epoch)
    {
        auto& record = records.at(index);
        if (!record.ownerActive || record.epoch != epoch) throw std::logic_error("Inactive owned-row lease");
        return record;
    }
    void stop(Record& record)
    {
        record.group->cancelled.store(true, std::memory_order_release);
        if (record.ticket) executor.cancel(*record.ticket);
    }
public:
    class Lease
    {
        friend class BufferPool;
        BufferPool* pool = nullptr;
        size_t index = 0;
        std::uint64_t epoch = 0;
        Lease(BufferPool& owner, size_t slot, std::uint64_t generation): pool(&owner), index(slot), epoch(generation) {}
        Record& record() const
        {
            if (!pool) throw std::logic_error("Finished owned-row lease");
            return pool->active(index, epoch);
        }
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept: pool(std::exchange(other.pool, nullptr)), index(other.index), epoch(other.epoch) {}
        Lease& operator=(Lease&& other) noexcept
        {
            if (this != &other) { finish(); pool = std::exchange(other.pool, nullptr); index = other.index; epoch = other.epoch; }
            return *this;
        }
        ~Lease() { finish(); }
        Motion::SamplingRequest& add()
        {
            auto& value = record();
            if (value.attempted || value.group->cancelled.load(std::memory_order_acquire) || value.group->count == MAX_ROWS)
                throw std::logic_error("Cannot add owned row");
            return value.group->cells[value.group->count++].request;
        }
        void removeLast()
        {
            auto& value = record();
            if (value.attempted || !value.group->count) throw std::logic_error("Cannot remove owned row");
            auto& input = value.group->cells[--value.group->count].request;
            input.asset.reset(); input.usage.clear();
        }
        void setObservation(std::shared_ptr<Observation> observation)
        {
            auto& value = record();
            if (value.attempted) throw std::logic_error("Cannot change submitted observation");
            value.group->observation = std::move(observation);
        }
        void setGate(std::shared_ptr<Gate> gate)
        {
            auto& value = record();
            if (value.attempted) throw std::logic_error("Cannot change a submitted row gate");
            value.group->gate = std::move(gate);
        }
        bool submit()
        {
            auto& value = record();
            if (value.attempted) throw std::logic_error("Owned row submission already attempted");
            value.attempted = true;
            if (!value.group->count || value.group->cancelled.load(std::memory_order_acquire)) return false;
            Request input{value.group, epoch};
            value.ticket = pool->executor.trySubmit(input);
            return bool(value.ticket);
        }
        Executor::Status status() const
        {
            const auto& value = record();
            return value.ticket ? pool->executor.status(*value.ticket) : Executor::Status::Expired;
        }
        Published state(size_t row) const
        {
            const auto& value = record();
            if (row >= value.group->count) return Published::Empty;
            return value.group->cells[row].published.load(std::memory_order_acquire);
        }
        const Motion::SamplingResult* ready(size_t row) const
        {
            // Borrow only within this owner's epoch; finish() must follow every read.
            const auto& value = record();
            return state(row) == Published::Ready ? &value.group->cells[row].result : nullptr;
        }
        // Stops work without ending the owner's read epoch. Discard cannot clear cells.
        void cancel() { auto& value = record(); pool->stop(value); }
        void finish() noexcept
        {
            if (!pool) return;
            auto& value = pool->records[index];
            pool->stop(value);
            value.ownerActive = false;
            pool = nullptr;
        }
    };
    explicit BufferPool(Executor& value): executor(value) {}
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    size_t residentBytes() const
    {
        size_t bytes = sizeof(Group) * MAX_GROUPS;
        for (const auto& record : records)
        {
            if (record.ownerActive || (record.ticket && executor.status(*record.ticket) != Executor::Status::Expired))
                throw std::logic_error("Resident inspection requires acknowledged idle groups");
            for (const auto& cell : record.group->cells)
                bytes += cell.request.usage.capacity() * sizeof(U32) + cell.result.getRequest().usage.capacity() * sizeof(U32) +
                         cell.result.getJoints().capacity() * sizeof(Motion::SampledJoint);
        }
        return bytes; // Fixed cells and their vector capacities; excludes shared assets/control blocks.
    }
    // All pool/lease calls belong to the one owner; leases must end before pool/executor.
    std::optional<Lease> acquire()
    {
        for (size_t index = 0; index < records.size(); ++index)
        {
            auto& record = records[index];
            if (record.ownerActive || (record.ticket && executor.status(*record.ticket) != Executor::Status::Expired) ||
                record.epoch == std::numeric_limits<std::uint64_t>::max()) continue;
            record.group->reset(++record.epoch);
            record.ticket.reset(); record.attempted = false; record.ownerActive = true;
            return Lease(*this, index, record.epoch);
        }
        return {};
    }
};
}
#endif
