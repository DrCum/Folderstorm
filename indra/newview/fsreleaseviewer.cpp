/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsreleaseviewer.h"
#include "fsreleasebuild.h"
#include "fsreleaseprefs.h"
#include "fssessionworker.h"
#include "llapp.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llcoros.h"
#include "llcorehttputil.h"
#include "llfloaterreg.h"
#include "lltexteditor.h"
#include "llweb.h"
#include <stdexcept>
namespace
{
fs_release::Result result;bool checking=false,requested=false;unsigned long revision=0;
std::string status="Check for a published Folderstorm release.";
void openUrl(const std::string& url){if(fs_release::trustedUrl(url,fs_release::DownloadHosts))LLWeb::loadURLExternal(url);}
}
void FSReleaseViewer::check(bool manual)
{
    requested=requested || manual;if(checking)return;
    auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);
    if(!manual && !prefs.due(fs_release::now()))return;
    if(fs_release::host(fs_release::FeedUrl).empty()){status="The update URL is not configured correctly.";++revision;return;}
    prefs.lastCheck=fs_release::now();fs_release::savePreferences(prefs);
    checking=true;status="Checking for updates…";++revision;
    LLCoros::instance().launch("FolderstormReleaseCheck",[channel=prefs.channel]()
    {
        fs_release::Result next;
        try
        {
            LLCoreHttpUtil::HttpCoroutineAdapter adapter("FolderstormReleaseCheck",LLCore::HttpRequest::DEFAULT_POLICY_ID);
            auto request=std::make_shared<LLCore::HttpRequest>();auto options=std::make_shared<LLCore::HttpOptions>();
            options->setTimeout(15);options->setTransferTimeout(20);options->setFollowRedirects(false);
            options->setMaxResponseBytes(fs_release::MaxFeedBytes);options->setRetries(0);
            options->setSSLVerifyPeer(true);options->setSSLVerifyHost(true);
            auto headers=std::make_shared<LLCore::HttpHeaders>();headers->append("Accept","application/json");
            std::string error;
            const auto releases=fs_release::fetchFeed(fs_release::FeedUrl,fs_release::DownloadHosts,[&](const std::string& url)
            {
                if(LLApp::isExiting())throw std::runtime_error("Viewer exiting");
                const auto response=adapter.getRawAndSuspend(request,url,options,headers);
                const auto httpStatus=LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(response[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
                if(httpStatus==LLCore::HttpStatus(404))return std::string("[]");
                if(!httpStatus)throw std::runtime_error("Update request failed");
                const auto& bytes=response[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
                return std::string(bytes.begin(),bytes.end());
            },error);
            next=fs_release::choose(releases,fs_release::runningBuild(),fs_release::loadPreferences(fs_release::DefaultChannel),requested,fs_release::DownloadHosts);
            if(!error.empty())next.error=error;
        }
        catch(const std::exception&){next.error="Could not check for updates. Try again later.";}
        checking=false;
        if(LLApp::isExiting())return;
        const bool show=requested || next.state==fs_release::State::Available || next.state==fs_release::State::NoArtifact;
        const bool manualCheck=requested;requested=false;
        if(channel!=fs_release::loadPreferences(fs_release::DefaultChannel).channel){if(manualCheck)check(true);return;}
        result=std::move(next);
        status=!result.error.empty()?result.error:result.state==fs_release::State::NoReleases?"No Folderstorm releases have been published yet.":
            result.state==fs_release::State::Current?"You're up to date for this channel.":
            result.state==fs_release::State::Available?"Folderstorm "+result.release.version+" is available.":
            "A newer release exists, but has no matching download for this installation.";
        ++revision;
        // A visible floater owns an Apply/Cancel draft. Do not run onOpen again.
        const auto floater=LLFloaterReg::findInstance("folderstorm_updates");
        if(show && (!floater || !floater->getVisible()))LLFloaterReg::showInstance("folderstorm_updates");
    });
}
void FSReleaseViewer::startup(){if(!FSSessionWorker::managed())check(false);}
void FSReleaseViewer::show(){LLFloaterReg::showInstance("folderstorm_updates");check(true);}
bool FSFloaterUpdates::postBuild()
{
    getChild<LLButton>("check")->setCommitCallback([](LLUICtrl*,const LLSD&){FSReleaseViewer::check(true);});
    getChild<LLButton>("download")->setCommitCallback([](LLUICtrl*,const LLSD&){if(result.artifact)openUrl(result.artifact->url);});
    getChild<LLButton>("notes")->setCommitCallback([](LLUICtrl*,const LLSD&){openUrl(result.release.url.empty()?fs_release::ReleasesPage:result.release.url);});
    getChild<LLButton>("skip")->setCommitCallback([this](LLUICtrl*,const LLSD&){auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);if(fs_release::Version::parse(result.release.version)){prefs.skipped=result.release.version;fs_release::savePreferences(prefs);}closeFloater();});
    getChild<LLButton>("apply")->setCommitCallback([this](LLUICtrl*,const LLSD&){apply();});
    getChild<LLButton>("ok")->setCommitCallback([this](LLUICtrl*,const LLSD&){if(apply())closeFloater();});
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*,const LLSD&){closeFloater();});
    return true;
}
bool FSFloaterUpdates::apply()
{
    auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);
    const auto channel=getChild<LLComboBox>("channel")->getValue().asString();
    if(!fs_release::channelValid(channel))return false;
    const bool changed=prefs.channel!=channel;
    if(changed){prefs.channel=channel;prefs.skipped.clear();prefs.lastCheck=0;}
    prefs.automatic=getChild<LLCheckBoxCtrl>("automatic")->getValue().asBoolean();
    if(!fs_release::savePreferences(prefs)){getChild<LLUICtrl>("status")->setValue("Update choices could not be saved.");return false;}
    if(changed){result={};status="Channel saved. Check now to find matching releases.";++revision;}
    return true;
}
void FSFloaterUpdates::onOpen(const LLSD&)
{
    const auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);
    getChild<LLCheckBoxCtrl>("automatic")->setValue(prefs.automatic);getChild<LLComboBox>("channel")->setValue(prefs.channel);
    getChild<LLUICtrl>("installed")->setValue("Installed: "+fs_release::runningBuild().version+" · "+fs_release::runningBuild().installer);
    mRevision=~0UL;refresh();
}
void FSFloaterUpdates::refresh()
{
    if(mRevision==revision)return;mRevision=revision;
    getChild<LLUICtrl>("status")->setValue(status);
    getChild<LLTextEditor>("changelog")->setText(result.release.notes);
    getChild<LLButton>("download")->setEnabled(!checking && result.artifact.has_value() && result.error.empty());
    getChild<LLButton>("skip")->setEnabled(!checking && (result.state==fs_release::State::Available || result.state==fs_release::State::NoArtifact));
    getChild<LLButton>("check")->setEnabled(!checking);
}
void FSFloaterUpdates::draw(){refresh();LLFloater::draw();}
