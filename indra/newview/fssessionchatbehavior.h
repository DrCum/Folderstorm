/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CHAT_BEHAVIOR_H
#define FS_SESSION_CHAT_BEHAVIOR_H
#include <algorithm>
#include <cstdint>
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
// Restore an anchor by message ID, including wrapped/multiline messages. If
// that message was evicted, choose the first surviving line conservatively.
template<typename Position>
inline Position fitTranscriptAnchor(Position position,const std::vector<std::pair<std::uint64_t,Position>>& old,
    const std::vector<std::pair<std::uint64_t,Position>>& next,Position length)
{
    if (old.empty()) return 0;
    auto prior = old.begin();
    for (auto i = old.begin(); i != old.end() && i->second <= position; ++i) prior = i;
    const auto found = std::find_if(next.begin(),next.end(),[&](const auto& entry) { return entry.first == prior->first; });
    if (found == next.end()) return 0;
    const auto limit = found+1 == next.end() ? length : (found+1)->second;
    return (std::min)(limit,found->second+(std::max)(Position{0},position-prior->second));
}
}
#endif
