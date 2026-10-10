/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_RESTART_H
#define FS_SESSION_RESTART_H
#include "fssessionprotocol.h"
namespace fs_session
{
// Used by the host. A Quit acknowledgment is not process exit or a released lease.
class Restart
{
public:
    enum class Phase { Idle, Queued, Waiting };
    enum class Result { None, Launch, Cancelled, TimedOut };
    Phase phase() const { return mPhase; }
    const Message& source() const { return mSource; }
    std::uint64_t sequence() const { return mSequence; }
    bool begin(const Message& source, std::uint64_t now)
    {
        if (mPhase != Phase::Idle || !source.pid || !source.generation || source.grid.empty() ||
            !std::any_of(source.worker.begin(), source.worker.end(), [](std::uint8_t c) { return c != 0; })) return false;
        mSource = source; mPhase = Phase::Queued; mDeadline = now + 60000; mSequence = 0; return true;
    }
    bool owns(const Message& current) const
    {
        return current.pid == mSource.pid && current.worker == mSource.worker && current.generation == mSource.generation &&
            current.account == mSource.account && current.grid == mSource.grid;
    }
    bool sent(std::uint64_t sequence)
    {
        if (mPhase != Phase::Queued || !sequence) return false;
        mSequence = sequence; mPhase = Phase::Waiting; return true;
    }
    void cancel() { mPhase = Phase::Idle; }
    Result poll(bool processAlive, bool cancelled, std::uint64_t now)
    {
        if (mPhase == Phase::Idle) return Result::None;
        // Cancellation wins even if process exit is observed in the same tick.
        if (cancelled) { cancel(); return Result::Cancelled; }
        if (now >= mDeadline) { cancel(); return Result::TimedOut; }
        if (!processAlive) { cancel(); return Result::Launch; }
        return Result::None;
    }
private:
    Phase mPhase = Phase::Idle;
    Message mSource;
    std::uint64_t mSequence = 0, mDeadline = 0;
};
}
#endif
