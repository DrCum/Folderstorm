/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

/** Native review of frozen inventory changes and bounded session history. */
#ifndef FS_FLOATER_INVENTORY_BULK_H
#define FS_FLOATER_INVENTORY_BULK_H

#include "llfloater.h"
#include "lltimer.h"

class LLInventoryPanel;

class FSFloaterInventoryBulk final : public LLFloater
{
public:
    explicit FSFloaterInventoryBulk(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;
    void onClose(bool app_quitting) override;

    static void showForSelection(LLInventoryPanel* panel = nullptr);
    static void showPlan(const std::string& plan_id);
    static void showHistory();

private:
    enum class Mode { Compose, Preview, History, Operation };
    void loadSelection(LLInventoryPanel* panel);
    void loadPlan(const std::string& id);
    void loadHistory();
    void loadOperation(const std::string& id);
    void prepare();
    void execute();
    void reviewUndo();
    void chooseDestination();
    void invalidatePreview();
    void updateRows();
    void updateButtons();
    void describeSelection();
    void changePage(S32 delta);
    void clearHistory();
    void reportError(const std::string& error);

    Mode mMode = Mode::Compose;
    LLSD mSelection = LLSD::emptyArray();
    LLSD mData;
    std::string mPlanID;
    std::string mOperationID;
    LLUUID mSession;
    S32 mPage = 0;
    S32 mHistoryTotal = 0;
    U64 mGeneration = 0;
    bool mBusy = false;
    bool mUpdating = false;
    LLTimer mRefresh;
};

#endif
