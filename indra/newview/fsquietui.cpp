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
#include "llviewerprecompiledheaders.h"
#include "fsquietui.h"
#include "llagent.h"
#include "llcallbacklist.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
namespace
{
bool sEnabled = false, sPresented = false;
LLUUID sAccount, sSession;
void idle(void*) { FSQuietUI::refresh(); }
}
bool FSQuietUI::active()
{
    return sEnabled && LLStartUp::getStartupState() == STATE_STARTED && sAccount == gAgent.getID() &&
        sSession == gAgent.getSessionID() && sAccount.notNull();
}
void FSQuietUI::refresh()
{
    const bool quiet = active();
    if (sPresented == quiet) return;
    sPresented = quiet;
    if (!quiet) sEnabled = false; // A new login cannot inherit this presentation mode.
    const bool shown = gViewerWindow && gViewerWindow->getUIVisibility() && !quiet;
    gSavedSettings.setBOOL("FSInternalShowNavbarNavigationPanel", shown && gSavedSettings.getBOOL("ShowNavbarNavigationPanel"));
    gSavedSettings.setBOOL("FSInternalShowNavbarFavoritesPanel", shown && gSavedSettings.getBOOL("ShowNavbarFavoritesPanel"));
}
void FSQuietUI::setEnabled(bool enabled)
{
    if (enabled && (LLStartUp::getStartupState() != STATE_STARTED || gAgent.getID().isNull())) return;
    static bool installed = false;
    if (enabled && !installed) { gIdleCallbacks.addFunction(idle, nullptr); installed = true; }
    sEnabled = enabled; sAccount = gAgent.getID(); sSession = gAgent.getSessionID();
    refresh();
}
