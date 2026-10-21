/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fscrashviewer.h"
#include "fscrash.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "lluri.h"
#include "llweb.h"
bool FSFloaterCrash::postBuild()
{
    getChild<LLButton>("apply")->setCommitCallback([this](LLUICtrl*,const LLSD&){apply();});
    getChild<LLButton>("ok")->setCommitCallback([this](LLUICtrl*,const LLSD&){if(apply())closeFloater();});
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*,const LLSD&){closeFloater();});
    getChild<LLButton>("report")->setCommitCallback([](LLUICtrl*,const LLSD&){LLWeb::loadURLExternal(fs_crash::reportUrl());});
    return true;
}
void FSFloaterCrash::onOpen(const LLSD&)
{
    mOriginal=fs_crash::consent().enabled;getChild<LLCheckBoxCtrl>("enabled")->setValue(mOriginal);
    getChild<LLCheckBoxCtrl>("enabled")->setEnabled(fs_crash::configured() || mOriginal);
    getChild<LLUICtrl>("status")->setValue(fs_crash::status());
}
bool FSFloaterCrash::apply()
{
    const bool enabled=getChild<LLCheckBoxCtrl>("enabled")->getValue().asBoolean();
    if(enabled!=mOriginal && !fs_crash::setConsent(enabled)){getChild<LLUICtrl>("status")->setValue("Crash-report choices could not be saved.");return false;}
    mOriginal=enabled;getChild<LLUICtrl>("status")->setValue(fs_crash::status());return true;
}
