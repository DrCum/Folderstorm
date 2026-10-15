/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_SOCIAL_H
#define FS_ASSISTANT_SOCIAL_H
#include <memory>
#include <vector>
class LLEventAPI;
class LLChat;
// Viewer-main-thread owner. The bridge retains this for exactly its lifetime;
// privacyChanged invalidates all retained private data and reader authority.
class FSAssistantSocial
{
public:
    FSAssistantSocial();
    ~FSAssistantSocial();
    std::vector<std::shared_ptr<LLEventAPI>> listeners() const;
    void tick();
    void privacyChanged();
    void nearbyMessage(const LLChat& message);
    struct State;
private:
    std::shared_ptr<State> mState;
    std::vector<std::shared_ptr<LLEventAPI>> mListeners;
};
#endif
