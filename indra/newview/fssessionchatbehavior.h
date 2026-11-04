/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CHAT_BEHAVIOR_H
#define FS_SESSION_CHAT_BEHAVIOR_H
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>
namespace fs_session
{
enum class ReturnAction { Ignore, Newline, Send };
struct ComposerReturn
{
    bool ime = false, imeReturn = false, consumed = false, newline = false;
    void down(bool repeat,bool shift)
    { imeReturn = imeReturn || ime; if (!repeat) { consumed = false; newline = shift; } }
    ReturnAction character(bool repeat)
    {
        if (ime || imeReturn || consumed || repeat) return ReturnAction::Ignore;
        consumed = true; return newline ? ReturnAction::Newline : ReturnAction::Send;
    }
    void up() { imeReturn = consumed = false; }
};
// An event and an offset within it survive history trimming and line wrapping.
template<typename Position>
struct TranscriptAnchor
{
    std::uint64_t event;
    Position offset;
};

template<typename Position>
inline std::optional<TranscriptAnchor<Position>> captureTranscriptAnchor(
    Position position, const std::vector<std::pair<std::uint64_t,Position>>& lines)
{
    if (lines.empty()) return std::nullopt;
    auto prior = lines.begin();
    for (auto it = lines.begin(); it != lines.end() && it->second <= position; ++it) prior = it;
    return TranscriptAnchor<Position>{prior->first, (std::max)(Position{0}, position - prior->second)};
}

template<typename Position>
inline Position restoreTranscriptAnchor(const TranscriptAnchor<Position>& place,
    const std::vector<std::pair<std::uint64_t,Position>>& lines, Position length)
{
    const auto found = std::find_if(lines.begin(), lines.end(), [&](const auto& entry) { return entry.first == place.event; });
    if (found == lines.end()) return 0; // Resume at the oldest surviving message.
    const auto limit = found + 1 == lines.end() ? length : (found + 1)->second;
    return found->second + (std::min)(place.offset, limit - found->second);
}

template<typename Position>
inline Position fitTranscriptAnchor(Position position,const std::vector<std::pair<std::uint64_t,Position>>& old,
    const std::vector<std::pair<std::uint64_t,Position>>& next,Position length)
{
    const auto place = captureTranscriptAnchor(position, old);
    return place ? restoreTranscriptAnchor(*place, next, length) : 0;
}
}
#endif
