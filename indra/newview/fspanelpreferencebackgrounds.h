/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_PANEL_PREFERENCE_BACKGROUNDS_H
#define FS_PANEL_PREFERENCE_BACKGROUNDS_H
#include "llpanel.h"
#include "lltimer.h"
#include "lluuid.h"
#include "fsworkspacebackgroundmodel.h"

class FSPanelPreferenceBackgrounds : public LLPanel
{
public:
    bool postBuild() override;
    void draw() override;
    void onVisibilityChange(bool visible) override;
private:
    void load();
    void controls();
    void changed();
    void chooseFile();
    void save();
    bool sameSource() const;
    FSWorkspaceBackground::Choice mChoice;
    FSWorkspaceBackground::Config mConfig;
    FSWorkspaceBackground::Image mImage;
    LLUUID mAccount, mLogin;
    unsigned long mRevision = 0, mEdit = 0;
    LLTimer mTimer;
    bool mDefault = false, mLoading = false;
    int mDisplay = -1;
    std::string mTopology, mError;
};
#endif
