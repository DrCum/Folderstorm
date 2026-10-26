/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_OBJECT_TOOLS_H
#define FS_OBJECT_TOOLS_H
class LLChat;
class LLMessageSystem;
void fs_initialize_object_tools();
void fs_object_tools_tick();
void fs_object_tools_privacy_changed();
void fs_object_tools_debug(const LLChat&);
void fs_object_tools_running_reply(LLMessageSystem*);
#endif
