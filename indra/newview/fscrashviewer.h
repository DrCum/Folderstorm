/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include "llfloater.h"
class FSFloaterCrash final : public LLFloater
{
public:
    explicit FSFloaterCrash(const LLSD& key):LLFloater(key){}
    bool postBuild() override;
    void onOpen(const LLSD&) override;
private:
    bool apply();bool mOriginal=false;
};
