/** Scrollable per-request confirmation for the local assistant bridge. */
#ifndef FS_ASSISTANT_APPROVAL_H
#define FS_ASSISTANT_APPROVAL_H

#include "llfloater.h"
#include <functional>

class FSAssistantApproval final : public LLFloater
{
public:
    explicit FSAssistantApproval(const LLSD& key);
    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void present(const LLSD& summary, std::function<void(bool)> callback);
    void dismiss();

private:
    void answer(bool allow);
    std::string describe(const LLSD& summary);
    void updatePage();
    std::function<void(bool)> mCallback;
    LLSD mSummary;
    S32 mPage = 0;
};

#endif
