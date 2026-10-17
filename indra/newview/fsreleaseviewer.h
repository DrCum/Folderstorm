/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_RELEASE_VIEWER_H
#define FS_RELEASE_VIEWER_H
#include "llfloater.h"
namespace FSReleaseViewer {void startup();void check(bool manual);void show();}
class FSFloaterUpdates final : public LLFloater
{
public:
    explicit FSFloaterUpdates(const LLSD& key):LLFloater(key){}
    bool postBuild() override;
    void onOpen(const LLSD&) override;
    void draw() override;
private:
    void refresh();bool apply();unsigned long mRevision=~0UL;
};
#endif
