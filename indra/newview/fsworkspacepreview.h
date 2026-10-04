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
#ifndef FS_WORKSPACE_PREVIEW_H
#define FS_WORKSPACE_PREVIEW_H
#include "fsworkspacecontroller.h"
#include "llfloater.h"
#include "lluictrl.h"
#include "lltimer.h"

class FSWorkspaceDiagram : public LLUICtrl
{
public:
    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params> {};
    explicit FSWorkspaceDiagram(const Params& params) : LLUICtrl(params) {}
    void setDiagram(const FSWorkspaceController::Diagram& diagram) { mDiagram = diagram; }
    void draw() override;
private:
    FSWorkspaceController::Diagram mDiagram;
};
class FSFloaterWorkspacePreview final : public LLFloater
{
public:
    explicit FSFloaterWorkspacePreview(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;
    void select(const std::string& key);
private:
    void refresh();
    std::string mEntry;
    LLUUID mAccount, mSession;
    LLTimer mTimer;
};
#endif
