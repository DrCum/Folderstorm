/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_UPLOAD_H
#define FS_CHAT_UPLOAD_H
#include "llfloater.h"
#include <functional>
class LLChatEntry;
namespace FSChatUpload
{
void bind(LLChatEntry* editor, const LLUUID& conversation = LLUUID::null,
          std::function<bool()> available = {}, std::function<std::string()> destination = {});
void bindNearby(LLChatEntry* editor);
bool blocksSend(LLChatEntry* editor);
void choose(LLChatEntry* editor);
void stageFiles(LLChatEntry* editor, const std::vector<std::string>& paths);
void stageBytes(LLChatEntry* editor, std::vector<unsigned char> bytes);
bool registered(LLChatEntry* editor);
void applyPreferences();
void preferencesOpening();
void shutdown();
}
class FSFloaterChatUpload final : public LLFloater
{
public:
    explicit FSFloaterChatUpload(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;
private:
    std::string mBinding;
};
#endif
