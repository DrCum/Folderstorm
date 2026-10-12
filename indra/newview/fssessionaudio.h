/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_AUDIO_H
#define FS_SESSION_AUDIO_H
namespace fs_session
{
constexpr unsigned int AudioVoice = 1, AudioMuteWorld = 2, AudioHostUI = 4;
inline bool backgroundHostAlertAllowed(bool ready, bool background, bool switching, bool restricted,
                                       bool dnd, bool muted, bool incoming)
{ return ready && background && !switching && !restricted && !dnd && !muted && incoming; }
}
#endif
