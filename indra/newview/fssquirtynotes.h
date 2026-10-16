/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SQUIRTY_NOTES_H
#define FS_SQUIRTY_NOTES_H

#include "lluuid.h"
#include <string>

namespace FSSquirtyNotes
{
void reset();
void seedFriends();
void onIM(const LLUUID& sender, const std::string& name);
void onFriendStatus(const LLUUID& id, bool online);
std::string token();
void setToken(const std::string& value);
// Status is a floater.string key, never raw server text or credentials.
std::string status();
void test(S32 port, const std::string& token, bool squirt);
void configurationChanged();
}
#endif
