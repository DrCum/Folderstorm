/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include "llfloater.h"
#include "fsupdate.h"
#include "fscrash.h"

namespace FSInstallViewer
{
void poll();
void shortcuts();
}

// Viewer prompts must use its frame-driven UI, not a nested Win32 modal loop
// on the rendering thread. The session host retains its native dialog path.
class FSFloaterSetup final : public LLFloater
{
public:
    explicit FSFloaterSetup(const LLSD& key):LLFloater(key){}
    bool pending() const {return mShortcuts.has_value() || mConsent.has_value();}
    bool postBuild() override;
    void onClose(bool app_quitting) override;
    bool handleKeyHere(KEY key,MASK mask) override;
    void open(fs_update::ShortcutPrompt prompt);
    void open(fs_crash::ConsentPrompt prompt);
private:
    void accept();
    std::optional<fs_update::ShortcutPrompt> mShortcuts;
    std::optional<fs_crash::ConsentPrompt> mConsent;
};
