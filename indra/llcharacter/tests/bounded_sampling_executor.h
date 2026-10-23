// Test-only prototype. No viewer playback caller or additional thread is installed.
#ifndef FS_TEST_BOUNDED_SAMPLING_EXECUTOR_H
#define FS_TEST_BOUNDED_SAMPLING_EXECUTOR_H

#include "workqueue.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace FSSamplingTest
{
// The queue and this adapter are accessed on their owning thread. LLInstanceTracker's
// weak handle detects an already deleted queue; it does not extend its C++ lifetime.
inline auto queueSubmitter(LL::WorkQueueBase::weak_t queue, size_t workers)
{
    return [queue, workers](const LL::WorkQueueBase::Work& work)
    {
        if (!workers) return false;
        auto live = queue.lock();
        return live && live->tryPost(work);
    };
}

// submit/take/cancel/close belong to one owner thread. Only delivered callbacks
// touch worker-owned buffers. The submitter must be nonblocking: false or a throw
// means it retained/delivered no callback; true means exactly one delivery.
// Duplicate deliveries are tolerated, but are not part of the queue contract.
template<class Sampler> class BoundedExecutor
{
public:
    using Request = typename Sampler::Request;
    using Result = typename Sampler::Result;
    using Work = LL::WorkQueueBase::Work;
    using Submit = std::function<bool(const Work&)>;
    enum class Status { Expired, Queued, Running, Ready, Failed, Discarding };
private:
    enum Phase : uint64_t { Idle, Queued, Running, Ready, Failed, Cancelled, Abandoned, Cleaning, Retired };
    static constexpr uint64_t MASK = 15;
    struct Slot
    {
        std::atomic<uint64_t> state{Idle};
        Request input;
        Result output;
    };
public:
    class Ticket
    {
        friend class BoundedExecutor;
        std::weak_ptr<Slot> slot;
        size_t index = 0;
        uint64_t generation = 0;
    };

    BoundedExecutor(size_t capacity, Submit submit): mSubmit(std::move(submit))
    {
        static_assert(std::atomic<uint64_t>::is_always_lock_free);
        static_assert(std::is_nothrow_move_assignable_v<Request> && std::is_nothrow_move_constructible_v<Result> &&
                      std::is_nothrow_move_assignable_v<Result>);
        mSlots.reserve(capacity);
        for (size_t i = 0; i < capacity; ++i) mSlots.push_back(std::make_shared<Slot>());
    }
    ~BoundedExecutor() { close(); }
    BoundedExecutor(const BoundedExecutor&) = delete;
    BoundedExecutor& operator=(const BoundedExecutor&) = delete;

    // Rejection leaves the caller's input intact for immediate serial fallback.
    std::optional<Ticket> trySubmit(Request& input)
    {
        if (mClosed) return {};
        for (size_t i = 0; i < mSlots.size(); ++i)
        {
            auto& slot = mSlots[i];
            const auto previous = slot->state.load(std::memory_order_acquire);
            if (phase(previous) != Idle) continue;
            if ((previous & ~MASK) == (std::numeric_limits<uint64_t>::max() & ~MASK))
            {
                slot->state.store(Retired, std::memory_order_release); // No generation wrap/ABA.
                continue;
            }
            const auto generation = (previous & ~MASK) + MASK + 1;
            slot->input = std::move(input);
            slot->state.store(generation | Queued, std::memory_order_release);
            try
            {
                Work work = [slot, generation] { deliver(slot, generation); };
                if (mSubmit(work))
                {
                    Ticket ticket;
                    ticket.slot = slot; ticket.index = i; ticket.generation = generation;
                    return ticket;
                }
            }
            catch (...)
            {
                input = std::move(slot->input);
                slot->state.store(generation | Idle, std::memory_order_release);
                throw;
            }
            input = std::move(slot->input);
            slot->state.store(generation | Idle, std::memory_order_release);
            return {};
        }
        return {};
    }

    Status status(const Ticket& ticket) const
    {
        auto slot = find(ticket);
        return slot ? statusOf(slot->state.load(std::memory_order_acquire), ticket.generation) : Status::Expired;
    }

    // Transfers a completed result to the owner. Application still requires the
    // independently prepared current-input/version check; readiness is insufficient.
    Status take(const Ticket& ticket, Result& result)
    {
        auto slot = find(ticket);
        if (!slot) return Status::Expired;
        const auto state = slot->state.load(std::memory_order_acquire);
        const auto value = statusOf(state, ticket.generation);
        if (value == Status::Ready)
        {
            result.takeRequest(); // Retire any previous lease, retaining output capacity.
            std::swap(result, slot->output);
            slot->state.store(ticket.generation | Idle, std::memory_order_release);
        }
        else if (value == Status::Failed)
        {
            result.takeRequest();
            clear(*slot);
            slot->state.store(ticket.generation | Idle, std::memory_order_release);
        }
        return value;
    }

    // Never wait or recycle a queued/running payload. Cancelled callbacks must
    // acknowledge before reuse, which bounds callbacks as well as resident slots.
    Status cancel(const Ticket& ticket)
    {
        auto slot = find(ticket);
        if (!slot) return Status::Expired;
        auto state = slot->state.load(std::memory_order_acquire);
        while ((state & ~MASK) == ticket.generation)
        {
            const auto value = statusOf(state, ticket.generation);
            if (phase(state) == Queued || phase(state) == Running)
            {
                const auto next = ticket.generation | (phase(state) == Queued ? Cancelled : Abandoned);
                if (!slot->state.compare_exchange_weak(state, next, std::memory_order_acq_rel)) continue;
            }
            else if (phase(state) == Ready || phase(state) == Failed)
            {
                clear(*slot);
                slot->state.store(ticket.generation | Idle, std::memory_order_release);
            }
            return value;
        }
        return Status::Expired;
    }

    void close()
    {
        if (mClosed) return;
        mClosed = true;
        for (size_t i = 0; i < mSlots.size(); ++i)
        {
            Ticket ticket;
            ticket.slot = mSlots[i]; ticket.index = i;
            ticket.generation = mSlots[i]->state.load(std::memory_order_acquire) & ~MASK;
            cancel(ticket);
        }
    }
    size_t capacity() const { return mSlots.size(); }

private:
    static Phase phase(uint64_t value) { return static_cast<Phase>(value & MASK); }
    static Status statusOf(uint64_t value, uint64_t generation)
    {
        if ((value & ~MASK) != generation) return Status::Expired;
        switch (phase(value))
        {
        case Queued: return Status::Queued;
        case Running: return Status::Running;
        case Ready: return Status::Ready;
        case Failed: return Status::Failed;
        case Cancelled: case Abandoned: case Cleaning: return Status::Discarding;
        default: return Status::Expired;
        }
    }
    std::shared_ptr<Slot> find(const Ticket& ticket) const
    {
        auto slot = ticket.slot.lock();
        return ticket.index < mSlots.size() && slot == mSlots[ticket.index] ? slot : nullptr;
    }
    static void clear(Slot& slot)
    {
        slot.input = Request{};
        slot.output.takeRequest();
    }
    static void deliver(const std::shared_ptr<Slot>& slot, uint64_t generation)
    {
        auto expected = generation | Queued;
        if (!slot->state.compare_exchange_strong(expected, generation | Running, std::memory_order_acq_rel))
        {
            expected = generation | Cancelled;
            if (slot->state.compare_exchange_strong(expected, generation | Cleaning, std::memory_order_acq_rel))
            {
                clear(*slot);
                slot->state.store(generation | Idle, std::memory_order_release);
            }
            return;
        }
        bool success = false;
        try { success = Sampler::sample(std::move(slot->input), slot->output); }
        catch (...) {} // Publish failure; never escape a queue callback with a half-result.
        expected = generation | Running;
        if (!slot->state.compare_exchange_strong(expected, generation | (success ? Ready : Failed),
                                                 std::memory_order_acq_rel))
        {
            // Only owner-thread abandonment can change Running. No observer reads
            // the buffers until this worker has discarded and acknowledged them.
            clear(*slot);
            slot->state.store(generation | Idle, std::memory_order_release);
        }
    }
    std::vector<std::shared_ptr<Slot>> mSlots;
    Submit mSubmit;
    bool mClosed = false;
};
}
#endif
