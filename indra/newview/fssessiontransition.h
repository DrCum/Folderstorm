/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_TRANSITION_H
#define FS_SESSION_TRANSITION_H
#include "fssessionprotocol.h"
#include <algorithm>
#include <cstdint>
namespace fs_session
{
inline bool transitionCancelMatches(const Message& cancel, const Message& pending)
{
    return cancel.kind == Kind::CancelTransition && pending.kind == Kind::SetMode && cancel.worker == pending.worker &&
        cancel.generation == pending.generation && cancel.cursor == pending.sequence && cancel.sequence > pending.sequence &&
        !cancel.account.empty() && cancel.account == pending.account && cancel.grid == pending.grid;
}
// A finite visual-only clock. It never owns a camera, input or a session.
class TransitionClock
{
public:
    static constexpr std::uint64_t Duration = 1100;
    void begin(std::uint64_t now, std::uint64_t generation)
    { mStart = now; mGeneration = generation; mActive = generation != 0; }
    bool active() const { return mActive; }
    bool finished(std::uint64_t now, std::uint64_t generation, bool allowed) const
    { return !mActive || !allowed || generation != mGeneration || now < mStart || now - mStart >= Duration; }
    float fraction(std::uint64_t now) const
    {
        const auto elapsed = now >= mStart ? now - mStart : 0;
        const float t = static_cast<float>((std::min)(elapsed, Duration)) / static_cast<float>(Duration);
        return t * t * (3.f - 2.f * t);
    }
    void cancel() { mActive = false; }
private:
    std::uint64_t mStart = 0, mGeneration = 0;
    bool mActive = false;
};
}
#endif
