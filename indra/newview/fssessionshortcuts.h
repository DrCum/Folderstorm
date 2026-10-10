/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_SHORTCUTS_H
#define FS_SESSION_SHORTCUTS_H
#include "fssessionchatmodel.h"
#include "fssessionregistry.h"
namespace fs_session
{
template<typename Ready> int characterShortcutTarget(unsigned int command,int active,Ready ready)
{
    if (command < MaxCharacters) return ready(static_cast<int>(command)) ? static_cast<int>(command) : -1;
    if (command > 6 || !validSlot(active)) return -1;
    const int direction = command == 5 ? 1 : -1;
    for (int step = 1; step < MaxCharacters; ++step)
    { const int next = (active+direction*step+MaxCharacters)%MaxCharacters; if (ready(next)) return next; }
    return -1;
}
inline bool validSwitchIntent(const Message& event,const Message& current,int source,int active,std::uint64_t now,bool enabled,bool busy)
{
    return enabled && !busy && source == active && validSlot(source) && current.state == State::Ready && current.mode == Mode::Active &&
        !(current.flags & (ChatRestricted|Error|Promoting)) && event.eventType == EventType::SwitchIntent && event.unread <= 6 &&
        ChatKey{current.worker,current.generation,current.account,current.grid,{}}.owns(event) && event.eventAt &&
        now >= event.eventAt && now-event.eventAt <= 3000;
}
}
#endif
