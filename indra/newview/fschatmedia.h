/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_H
#define FS_CHAT_MEDIA_H
#include "llfloater.h"
class FSChatHistory;
class LLChat;
namespace FSChatMedia
{
void append(FSChatHistory* history, const LLChat& chat, bool nearby);
void resize(FSChatHistory* history);
int layoutKey();
void applyPreferences();
void preferencesOpening();
void clear();
void shutdown();
}
class FSFloaterChatMedia final : public LLFloater
{
public:
    explicit FSFloaterChatMedia(const LLSD& key) : LLFloater(key) {}
    void onOpen(const LLSD& key) override;
};
#endif
