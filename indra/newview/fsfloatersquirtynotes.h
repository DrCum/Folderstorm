/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_FLOATER_SQUIRTY_NOTES_H
#define FS_FLOATER_SQUIRTY_NOTES_H

#include "llfloater.h"

class FSFloaterSquirtyNotes : public LLFloater
{
public:
    explicit FSFloaterSquirtyNotes(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

private:
    void updateEnabled();
    void refreshList(bool online);
    void addResident(bool online);
    void removeResident(bool online);
    void apply();
    void test(bool squirt);
    LLSD mIMResidents;
    LLSD mOnlineResidents;
    std::string mStatusOverride;
};
#endif
