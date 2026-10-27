/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_OBJECT_TOOLS_H
#define FS_OBJECT_TOOLS_H
#include "llsd.h"
#include <functional>
class LLChat;
class LLMessageSystem;
class LLHost;
class LLViewerObject;
void fs_initialize_object_tools();
void fs_object_tools_tick();
void fs_object_tools_privacy_changed();
void fs_object_tools_debug(const LLChat&);
// Native editor and assistant reads share one no-nonce query fence. These hooks
// track native state even when MCP access is disabled.
bool fs_object_tools_request_running(LLViewerObject*,const LLUUID&);
void fs_object_tools_native_execution_changed(const LLUUID&,const LLUUID&);
bool fs_object_tools_running_reply(LLMessageSystem*);
void fs_object_tools_inventory_transaction(LLMessageSystem*);
bool fs_object_tools_derez_reply(LLMessageSystem*);
void fs_object_tools_object_killed(const LLUUID&,const LLHost&);
bool fs_object_tools_submit_rez(const LLSD&,const std::function<void()>&,LLSD&,std::string&);
#endif
