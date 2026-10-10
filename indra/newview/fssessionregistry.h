/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_REGISTRY_H
#define FS_SESSION_REGISTRY_H
#include "fssessionprotocol.h"
#include <array>
namespace fs_session
{
constexpr int MaxCharacters = 5;
inline bool validSlot(int index) { return index >= 0 && index < MaxCharacters; }
inline std::array<Mode, MaxCharacters> warmModes()
{ std::array<Mode, MaxCharacters> modes{}; modes.fill(Mode::Warm); return modes; }
struct LoginPeer
{
    const Message* snapshot = nullptr;
    const std::string* reservation = nullptr;
    bool live = false;
};
template<class PeerAt> bool loginCollisionAny(const Message& candidate, int index, PeerAt peerAt)
{
    for (int i = 0; i < MaxCharacters; ++i) if (i != index)
    {
        const auto peer = peerAt(i);
        if (peer.live && peer.snapshot && peer.reservation && loginCollision(candidate, *peer.snapshot, *peer.reservation)) return true;
    }
    return false;
}
// Real host scheduling: one outstanding request per worker, rotating periodic
// services so a constantly due event stream cannot starve catalog/workspace.
class PeriodicPoller
{
public:
    Kind next(bool ready, bool restricted, std::uint64_t now,
        std::uint64_t events, std::uint64_t catalog, std::uint64_t workspace, std::uint64_t poll)
    {
        const Kind kinds[] = {Kind::Events, Kind::Conversations, Kind::WorkspaceInfo, Kind::Poll};
        const std::uint64_t at[] = {events, catalog, workspace, poll};
        const std::uint64_t interval[] = {100, 20000, 2000, 1000};
        for (unsigned int offset = 0; offset < 4; ++offset)
        {
            const auto index = (cursor + offset) % 4;
            if (index < 3 && !ready) continue;
            if (index < 2 && restricted) continue;
            if (now >= at[index] && now - at[index] >= interval[index])
            { cursor = (index + 1) % 4; return kinds[index]; }
        }
        return Kind::Status; // No request is due.
    }
private:
    unsigned int cursor = 0;
};
}
#endif
