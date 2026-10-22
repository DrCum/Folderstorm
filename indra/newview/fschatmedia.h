/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_H
#define FS_CHAT_MEDIA_H
#include "llfloater.h"
#include <functional>
class FSChatHistory;
class LLChat;
namespace FSChatMedia
{
void append(FSChatHistory* history, const LLChat& chat, bool nearby, S32 bodyStart);
void resize(FSChatHistory* history);
int layoutKey();
void applyPreferences();
void preferencesOpening();
void clear();
void shutdown();
LLPanel* createBoardTile(const std::string& url, std::function<void()> select, std::function<void()> insert,
                        std::function<void(S32, S32)> context = {});
void boardPlayback(LLPanel* tile, bool play, bool visible = true);
void boardPreview(LLPanel* tile);
bool automaticMotionEnabled();
struct BoardOptions { int playback = 0, threshold = 80, size = 160; };
BoardOptions boardOptions();
void boardSize(int size);
}
class FSFloaterChatMedia final : public LLFloater
{
public:
    explicit FSFloaterChatMedia(const LLSD& key) : LLFloater(key) {}
    void onOpen(const LLSD& key) override;
};
#endif
