/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_RECOVERY_H
#define FS_SESSION_RECOVERY_H
#include "fssessionpresentation.h"
namespace fs_session
{
inline unsigned int recoveredChrome(unsigned int current, bool controlsClosing = false)
{ return current == 2 || !controlsClosing ? 1u : current; }
inline int revealHeight(unsigned int chrome, bool comfortable = false)
{ return chrome == 2 ? (comfortable ? 32 : 24) : 0; }
inline ShellRect reachableRect(ShellRect current, const ShellRect& work, unsigned int dpi)
{
    if (!current.valid()) current = {work.x + 20, work.y + 20, 800, 500, 96};
    return fitShellRect(current, work, dpi);
}
}
#endif
