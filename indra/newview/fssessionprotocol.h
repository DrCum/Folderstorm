/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#ifndef FS_SESSION_PROTOCOL_H
#define FS_SESSION_PROTOCOL_H

// Private, fixed-size protocol. No native structure/pointer layout on the wire.
// This header also builds without viewer libraries for the prototype host/tests.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace fs_session
{
constexpr std::size_t FrameSize = 512;
constexpr std::uint32_t Version = 2;
using Frame = std::array<std::uint8_t, FrameSize>;
using WorkerId = std::array<std::uint8_t, 16>;
enum class Kind : std::uint32_t
{
    Poll = 1, SetMode, PermitLogin, DenyLogin, Detach, Quit, Embed, Unembed, Focus,
    Status = 16
};
enum class Mode : std::uint32_t { Active, Warm };
enum class State : std::uint32_t { Starting, Login, Connecting, Ready, Disconnected };
enum Flag : std::uint32_t { LoginPending = 1, Error = 2, Embedded = 4, Promoting = 8, ClientFocused = 16 };
constexpr std::uint32_t KnownFlags = LoginPending | Error | Embedded | Promoting | ClientFocused;

struct Message
{
    Kind kind = Kind::Poll;
    WorkerId worker{};
    std::uint64_t generation = 1, sequence = 1;
    Mode mode = Mode::Warm;
    State state = State::Starting;
    std::uint32_t flags = 0, pid = 0, width = 0, height = 0;
    std::uint64_t surface = 0, frames = 0, maintenance = 0;
    std::string account, grid, name, detail;
};

inline bool validKind(Kind kind)
{
    return (kind >= Kind::Poll && kind <= Kind::Focus) || kind == Kind::Status;
}
inline bool validUtf8(const std::string& text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
        int count = 0;
        std::uint32_t value = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; value = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; value = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        for (int n = 0; n < count; ++n)
        {
            if (i == text.size()) return false;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
inline bool validAccount(const std::string& value)
{
    if (value.empty()) return true;
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}
inline bool viewerFilename(std::string text, std::string& result)
{
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
    if (text.size() <= 4 || text.size() > 240 || !validUtf8(text) ||
        text.find_first_of("/\\:*?\"<>|") != std::string::npos || text.back() == ' ') return false;
    auto extension = text.substr(text.size() - 4);
    for (auto& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (extension != ".exe") return false;
    result = std::move(text);
    return true;
}
inline void put(Frame& frame, std::size_t offset, std::uint64_t value, std::size_t size)
{
    for (std::size_t n = 0; n < size; ++n) frame[offset + n] = static_cast<std::uint8_t>(value >> (n * 8));
}
inline std::uint64_t get(const Frame& frame, std::size_t offset, std::size_t size)
{
    std::uint64_t value = 0;
    for (std::size_t n = 0; n < size; ++n) value |= static_cast<std::uint64_t>(frame[offset + n]) << (n * 8);
    return value;
}
inline bool putText(Frame& frame, std::size_t offset, std::size_t size, const std::string& text)
{
    if (text.size() >= size || !validUtf8(text)) return false;
    std::copy(text.begin(), text.end(), frame.begin() + offset);
    return true;
}
inline bool getText(const Frame& frame, std::size_t offset, std::size_t size, std::string& text)
{
    const auto first = frame.begin() + offset, last = first + size;
    const auto end = std::find(first, last, 0);
    if (end == last || std::any_of(end, last, [](std::uint8_t c) { return c != 0; })) return false;
    text.assign(first, end);
    return validUtf8(text);
}
inline bool valid(const Message& message)
{
    return validKind(message.kind) && message.generation && message.sequence &&
        message.mode <= Mode::Warm && message.state <= State::Disconnected &&
        !(message.flags & ~KnownFlags) && validAccount(message.account) &&
        std::any_of(message.worker.begin(), message.worker.end(), [](std::uint8_t c) { return c != 0; });
}
inline bool encode(const Message& message, Frame& frame)
{
    frame.fill(0);
    if (!valid(message)) return false;
    put(frame, 0, 0x31535346, 4); // FSS1
    put(frame, 4, Version, 4);
    put(frame, 8, static_cast<std::uint32_t>(message.kind), 4);
    put(frame, 12, message.flags, 4);
    std::copy(message.worker.begin(), message.worker.end(), frame.begin() + 16);
    put(frame, 32, message.generation, 8);
    put(frame, 40, message.sequence, 8);
    put(frame, 48, static_cast<std::uint32_t>(message.mode), 4);
    put(frame, 52, static_cast<std::uint32_t>(message.state), 4);
    put(frame, 56, message.surface, 8);
    put(frame, 64, message.frames, 8);
    put(frame, 72, message.maintenance, 8);
    put(frame, 80, message.pid, 4);
    put(frame, 84, message.width, 4);
    put(frame, 88, message.height, 4);
    return putText(frame, 96, 40, message.account) && putText(frame, 136, 128, message.grid) &&
        putText(frame, 264, 128, message.name) && putText(frame, 392, 120, message.detail);
}
inline bool decode(const Frame& frame, Message& output)
{
    if (get(frame, 0, 4) != 0x31535346 || get(frame, 4, 4) != Version || get(frame, 92, 4)) return false;
    Message message;
    message.kind = static_cast<Kind>(get(frame, 8, 4));
    message.flags = static_cast<std::uint32_t>(get(frame, 12, 4));
    std::copy(frame.begin() + 16, frame.begin() + 32, message.worker.begin());
    message.generation = get(frame, 32, 8); message.sequence = get(frame, 40, 8);
    message.mode = static_cast<Mode>(get(frame, 48, 4)); message.state = static_cast<State>(get(frame, 52, 4));
    message.surface = get(frame, 56, 8); message.frames = get(frame, 64, 8); message.maintenance = get(frame, 72, 8);
    message.pid = static_cast<std::uint32_t>(get(frame, 80, 4));
    message.width = static_cast<std::uint32_t>(get(frame, 84, 4)); message.height = static_cast<std::uint32_t>(get(frame, 88, 4));
    if (!getText(frame, 96, 40, message.account) || !getText(frame, 136, 128, message.grid) ||
        !getText(frame, 264, 128, message.name) || !getText(frame, 392, 120, message.detail) || !valid(message)) return false;
    output = std::move(message);
    return true;
}

// Used by the actual host, including rollback. Each successful transition is
// an acknowledged revoke followed by a fresh-session first-frame promotion.
class Handoff
{
public:
    enum class Step { Idle, Revoke, Promote, Rollback };
    bool begin(int current, int target, std::uint64_t current_generation, std::uint64_t target_generation)
    {
        if (mStep != Step::Idle || target < 0 || target == current || !target_generation) return false;
        mOld = current; mTarget = target; mOldGeneration = current_generation; mTargetGeneration = target_generation;
        mStep = current < 0 ? Step::Promote : Step::Revoke;
        return true;
    }
    Step step() const { return mStep; }
    int worker() const { return mStep == Step::Promote ? mTarget : mOld; }
    std::uint64_t generation() const { return mStep == Step::Promote ? mTargetGeneration : mOldGeneration; }
    Mode mode() const { return mStep == Step::Revoke ? Mode::Warm : Mode::Active; }
    int target() const { return mTarget; }
    int original() const { return mOld; }
    bool accept(int worker, const Message& reply)
    {
        if (mStep == Step::Idle || worker != this->worker() || reply.generation != generation() ||
            reply.kind != Kind::Status || reply.mode != mode() || (reply.flags & (Error | Promoting)) ||
            (mStep == Step::Revoke ? (reply.flags & Embedded) || (reply.state != State::Ready && reply.state != State::Disconnected) :
                reply.state != State::Ready)) return false;
        mStep = mStep == Step::Revoke ? Step::Promote : Step::Idle;
        return true;
    }
    void fail()
    {
        mStep = mStep == Step::Promote && mOld >= 0 ? Step::Rollback : Step::Idle;
    }
    void cancel() { mStep = Step::Idle; }
private:
    Step mStep = Step::Idle;
    int mOld = -1, mTarget = -1;
    std::uint64_t mOldGeneration = 0, mTargetGeneration = 0;
};

inline bool loginCollision(const Message& candidate, const Message& other, const std::string& reservation)
{
    if (candidate.grid.empty() || candidate.name.empty() || candidate.grid != other.grid) return false;
    return candidate.name == reservation ||
        (!candidate.account.empty() && candidate.account == other.account && other.state == State::Ready);
}
}
#endif
