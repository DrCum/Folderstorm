/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsinstallviewer.h"
#include "fssessionworker.h"
#include "llapp.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llfloaterreg.h"
#include "llnotificationsutil.h"
#include "llstartup.h"

namespace
{
enum class Stage {Shortcuts,Consent,Done};
Stage stage=Stage::Shortcuts;
const char* const checks[]={"desktop_viewer","desktop_host","start_viewer","start_host"};
void error(const std::string& text)
{
    if(text.empty())return;
    LLSD args;args["MESSAGE"]=text;LLNotificationsUtil::add("GenericAlert",args);
}
FSFloaterSetup* floater()
{return LLFloaterReg::getTypedInstance<FSFloaterSetup>("folderstorm_setup");}
bool pending()
{
    const auto* setup=LLFloaterReg::findTypedInstance<FSFloaterSetup>("folderstorm_setup");
    return setup && setup->pending();
}
void showShortcuts(bool manual)
{
    if(auto prompt=fs_update::shortcutPrompt(manual))
    {
        if(auto* setup=floater())setup->open(std::move(*prompt));
        // If skin construction fails, release the lease without saving a choice.
    }
    else error(fs_update::lastError());
}
}
void FSInstallViewer::poll()
{
    // Login UI and the normal gatherInput/render loop must exist first. This
    // also covers auto-login; opening setup never blocks login or networking.
    if(stage==Stage::Done || FSSessionWorker::managed() || LLApp::isQuitting() ||
       (LLStartUp::getStartupState()!=STATE_LOGIN_WAIT && LLStartUp::getStartupState()!=STATE_STARTED))return;
    if(pending())return;
    if(stage==Stage::Shortcuts)
    {
        stage=Stage::Consent;showShortcuts(false);return;
    }
    stage=Stage::Done;
    if(auto prompt=fs_crash::consentPrompt())
        if(auto* setup=floater())setup->open(std::move(*prompt));
}
void FSInstallViewer::shortcuts()
{
    if(pending())
    {
        LLFloaterReg::showInstance("folderstorm_setup");return;
    }
    showShortcuts(true);
}
bool FSFloaterSetup::postBuild()
{
    getChild<LLButton>("accept")->setCommitCallback([this](LLUICtrl*,const LLSD&){accept();});
    getChild<LLButton>("later")->setCommitCallback([this](LLUICtrl*,const LLSD&){closeFloater();});
    return true;
}
void FSFloaterSetup::open(fs_update::ShortcutPrompt prompt)
{
    mConsent.reset();mShortcuts=std::move(prompt);
    setTitle("Folderstorm shortcuts");
    getChild<LLUICtrl>("explanation")->setValue("Choose shortcuts for this Windows user.");
    getChild<LLUICtrl>("details")->setValue("Choices are remembered. Shortcuts for other installs are kept. Change these choices later from Help or host Support.");
    for(unsigned i=0;i<4;++i)
    {
        auto* check=getChild<LLCheckBoxCtrl>(checks[i]);check->setVisible(true);
        check->setValue((mShortcuts->choices&(1u<<i))!=0);
    }
    getChild<LLButton>("accept")->setLabel("Apply");setDefaultBtn("accept");
    if(LLFloaterReg::showInstance("folderstorm_setup"))getChild<LLCheckBoxCtrl>(checks[0])->setFocus(true);
    else mShortcuts.reset();
}
void FSFloaterSetup::open(fs_crash::ConsentPrompt prompt)
{
    mShortcuts.reset();mConsent=std::move(prompt);
    setTitle("Folderstorm crash reporting");
    getChild<LLUICtrl>("explanation")->setValue(
        "Help improve Folderstorm?\n\nAutomatically send crash reports to Folderstorm's Sentry project to help fix bugs. Reports include technical details and a memory dump, which may contain personal information. Chat logs and screenshots are not attached.\n\n"
        "Your choice applies to the viewer, host and all character sessions for this Windows user.");
    getChild<LLUICtrl>("details")->setValue("Off by default. Change your choice later in viewer Help or host Settings > Support > Crash reports.");
    for(const auto* name:checks)getChild<LLCheckBoxCtrl>(name)->setVisible(false);
    getChild<LLButton>("accept")->setLabel("Enable crash reporting");setDefaultBtn("later");
    if(LLFloaterReg::showInstance("folderstorm_setup"))getChild<LLButton>("later")->setFocus(true);
    else mConsent.reset();
}
void FSFloaterSetup::accept()
{
    bool saved=true;std::string failure;
    if(mShortcuts)
    {
        std::uint32_t selected=0;
        for(unsigned i=0;i<4;++i)if(getChild<LLCheckBoxCtrl>(checks[i])->getValue().asBoolean())selected|=1u<<i;
        saved=mShortcuts->respond(selected);failure=fs_update::lastError();mShortcuts.reset();
    }
    else if(mConsent)
    {
        saved=mConsent->respond(true);mConsent.reset();
        failure="Your crash-reporting choice could not be saved. Retry in Help > Crash reporting.";
    }
    closeFloater();if(!saved)error(failure);
}
void FSFloaterSetup::onClose(bool app_quitting)
{
    // Closing/Escape means Not now, but shutting down without a decision does
    // not write preferences. Never convert a cancelled startup to consent.
    if(!app_quitting && !LLApp::isQuitting())
    {
        if(mShortcuts && !mShortcuts->respond(std::nullopt))error(fs_update::lastError());
        if(mConsent && !mConsent->respond(false))error("Your crash-reporting choice could not be saved. Retry in Help > Crash reporting.");
    }
    mShortcuts.reset();mConsent.reset();
}
bool FSFloaterSetup::handleKeyHere(KEY key,MASK mask)
{
    if(key==KEY_ESCAPE && mask==MASK_NONE){closeFloater();return true;}
    return LLFloater::handleKeyHere(key,mask);
}
