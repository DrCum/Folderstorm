/**
 * @file llfloaterabout.cpp
 * @author James Cook
 * @brief The about box from Help->About
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include <iostream>
#include <fstream>

#include "llfloaterabout.h"

// Viewer includes
#include "llagent.h"
#include "llagentui.h"
#include "llappviewer.h"
#include "llnotificationsutil.h"
#include "llslurl.h"
#include "llvoiceclient.h"
#include "lluictrlfactory.h"
#include "llviewertexteditor.h"
#include "llviewercontrol.h"
#include "llviewerstats.h"
#include "llviewerregion.h"
#include "llversioninfo.h"
#include "llweb.h"

// Linden library includes
#include "llaudioengine.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "rlvactions.h"
#include "llglheaders.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llimagej2c.h"
#include "llsys.h"
#include "lltrans.h"
#include "lluri.h"
#include "v3dmath.h"
#include "llwindow.h"
#include "stringize.h"
#include "llsdutil_math.h"
#include "lleventapi.h"
#include "llcorehttputil.h"
#include "lldir.h"

#if LL_WINDOWS
#include "lldxhardware.h"
#endif

extern LLMemoryInfo gSysMemory;
extern U32 gPacketsIn;

///----------------------------------------------------------------------------
/// Class LLFloaterAbout
///----------------------------------------------------------------------------
class LLFloaterAbout
    : public LLFloater
{
    friend class LLFloaterReg;
private:
    LLFloaterAbout(const LLSD& key);
    virtual ~LLFloaterAbout();

public:
    bool postBuild() override;
    void onOpen(const LLSD&) override;
    void onClose(bool) override;
    void draw() override;

    /// Obtain the data used to fill out the contents string. This is
    /// separated so that we can programmatically access the same info.
    static LLSD getInfo();
    void onClickCopyToClipboard();
    void onClickUpdateCheck();
    static void setUpdateListener();

// <FS:Ansariel> Release notes link not updating
//private:
    void setSupportText();
    void syncLocationChoice(bool reopening = false);
    void onLocationChoice();
    LLCheckBoxCtrl* mLocationChoice = nullptr;
    LLUUID mReportSession, mReportRegion;
    bool mCanShowLocation = false;
    U64 mNotesGeneration = 0;
    boost::signals2::scoped_connection mCapsReady;
    static LLFloaterAbout* notesOwner(const LLHandle<LLFloater>& handle, U64 generation);

    // notifications for user requested checks
    static void showCheckUpdateNotification(S32 state);

    // callback method for manual checks
    static bool callbackCheckUpdate(LLSD const & event);

    // listener name for update checks
    static const std::string sCheckUpdateListenerName;

    void startFetchServerReleaseNotes();
    static void fetchServerReleaseNotesCoro(std::string cap_url, LLHandle<LLFloater> owner, U64 generation);
    void handleServerReleaseNotes(LLSD results);
};


// Default constructor
LLFloaterAbout::LLFloaterAbout(const LLSD& key)
:   LLFloater(key)
{

}

// Destroys the object
LLFloaterAbout::~LLFloaterAbout()
{
}

bool LLFloaterAbout::postBuild()
{
    center();
    LLViewerTextEditor *support_widget =
        getChild<LLViewerTextEditor>("support_editor", true);

    LLViewerTextEditor *contrib_names_widget =
        getChild<LLViewerTextEditor>("contrib_names", true);

    LLViewerTextEditor *licenses_widget =
        getChild<LLViewerTextEditor>("licenses_editor", true);

    getChild<LLUICtrl>("copy_btn")->setCommitCallback(
        boost::bind(&LLFloaterAbout::onClickCopyToClipboard, this));

    // <FS:Ansariel> Disabled update button
    //getChild<LLUICtrl>("update_btn")->setCommitCallback(
    //  boost::bind(&LLFloaterAbout::onClickUpdateCheck, this));
    // </FS:Ansariel>

    static const LLUIColor about_color = LLUIColorTable::instance().getColor("TextFgReadOnlyColor");

    mLocationChoice = getChild<LLCheckBoxCtrl>("include_location");
    mLocationChoice->setCommitCallback([this](LLUICtrl*, const LLSD&) { onLocationChoice(); });

    support_widget->blockUndo();

    // Fix views
    support_widget->setEnabled(false);
    support_widget->startOfDoc();

    // Get the names of contributors, extracted from .../doc/contributions.txt by viewer_manifest.py at build time
    std::string contributors_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,"contributors.txt");
    llifstream contrib_file;
    std::string contributors;
    contrib_file.open(contributors_path.c_str());       /* Flawfinder: ignore */
    if (contrib_file.is_open())
    {
        std::getline(contrib_file, contributors); // all names are on a single line
        contrib_file.close();
    }
    else
    {
        LL_WARNS("AboutInit") << "Could not read contributors file at " << contributors_path << LL_ENDL;
    }
    contrib_names_widget->setText(contributors);
    contrib_names_widget->setEnabled(false);
    contrib_names_widget->startOfDoc();

    // Get the Versions and Copyrights, created at build time
    std::string licenses_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,"packages-info.txt");
    llifstream licenses_file;
    licenses_file.open(licenses_path.c_str());      /* Flawfinder: ignore */
    if (licenses_file.is_open())
    {
        std::string license_line;
        licenses_widget->clear();
        while ( std::getline(licenses_file, license_line) )
        {
            licenses_widget->appendText(license_line+"\n", false,
                                        LLStyle::Params() .color(about_color));
        }
        licenses_file.close();
    }
    else
    {
        // this case will use the (out of date) hard coded value from the XUI
        LL_INFOS("AboutInit") << "Could not read licenses file at " << licenses_path << LL_ENDL;
    }
    licenses_widget->setEnabled(false);
    licenses_widget->startOfDoc();

    return true;
}

LLSD LLFloaterAbout::getInfo()
{
    return LLAppViewer::instance()->getViewerInfo();
}

void LLFloaterAbout::onOpen(const LLSD&)
{
    syncLocationChoice(true);
}

void LLFloaterAbout::onClose(bool)
{
    ++mNotesGeneration;
    mCapsReady.disconnect();
}

void LLFloaterAbout::draw()
{
    syncLocationChoice();
    LLFloater::draw();
}

void LLFloaterAbout::syncLocationChoice(bool reopening)
{
    const auto* region = gAgent.getRegion();
    const auto region_id = region ? region->getRegionID() : LLUUID::null;
    const auto session = gAgent.getSessionID();
    const bool allowed = !gDisconnected && region && session.notNull() && RlvActions::canShowLocation();
    if (!reopening && session == mReportSession && region_id == mReportRegion && allowed == mCanShowLocation) return;

    mReportSession = session;
    mReportRegion = region_id;
    mCanShowLocation = allowed;
    ++mNotesGeneration;
    mCapsReady.disconnect();
    mLocationChoice->set(false);
    mLocationChoice->setEnabled(allowed);
    setSupportText();
}

void LLFloaterAbout::onLocationChoice()
{
    syncLocationChoice();
    ++mNotesGeneration;
    mCapsReady.disconnect();
    if (mLocationChoice->get() && mCanShowLocation)
    {
        LLAppViewer::instance()->setServerReleaseNotesURL("");
        startFetchServerReleaseNotes();
    }
    setSupportText();
}

LLFloaterAbout* LLFloaterAbout::notesOwner(const LLHandle<LLFloater>& handle, U64 generation)
{
    auto* about = dynamic_cast<LLFloaterAbout*>(handle.get());
    if (!about || about->mNotesGeneration != generation) return nullptr;
    about->syncLocationChoice();
    return about->mNotesGeneration == generation && about->mLocationChoice->get() ? about : nullptr;
}

void LLFloaterAbout::startFetchServerReleaseNotes()
{
    LLViewerRegion* region = gAgent.getRegion();
    if (!region) return;

    const auto handle = getHandle();
    const auto generation = mNotesGeneration;
    mCapsReady.disconnect();
    if (!region->capabilitiesReceived())
    {
        mCapsReady = region->setCapabilitiesReceivedCallback([handle, generation](const LLUUID&, LLViewerRegion*) {
            if (auto* about = notesOwner(handle, generation)) about->startFetchServerReleaseNotes();
        });
        return;
    }

    // We cannot display the URL returned by the ServerReleaseNotes capability
    // because opening it in an external browser will trigger a warning about untrusted
    // SSL certificate.
    // So we query the URL ourselves, expecting to find
    // an URL suitable for external browsers in the "Location:" HTTP header.
    std::string cap_url = region->getCapability("ServerReleaseNotes");

    if (cap_url.empty())
    {
        LLAppViewer::instance()->setServerReleaseNotesURL(LLTrans::getString("ErrorFetchingServerReleaseNotesURL"));
        setSupportText();
        return;
    }
    LLCoros::instance().launch("fetchServerReleaseNotesCoro", [cap_url, handle, generation] {
        fetchServerReleaseNotesCoro(cap_url, handle, generation);
    });

}

/*static*/
void LLFloaterAbout::fetchServerReleaseNotesCoro(std::string cap_url, LLHandle<LLFloater> owner, U64 generation)
{
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("fetchServerReleaseNotesCoro", LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();
    LLCore::HttpOptions::ptr_t httpOpts = std::make_shared<LLCore::HttpOptions>();

    httpOpts->setWantHeaders(true);
    httpOpts->setFollowRedirects(false);
    httpOpts->setSSLVerifyPeer(false); // We want this data even if SSL verification fails

    LLSD result = httpAdapter->getAndSuspend(httpRequest, cap_url, httpOpts);

    auto* about = notesOwner(owner, generation);
    if (!about) return;
    if (!gAgent.getRegion()->capabilitiesReceived() || gAgent.getRegion()->getCapability("ServerReleaseNotes") != cap_url)
    {
        about->startFetchServerReleaseNotes();
        return;
    }

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);
    if (!status)
    {
        about->handleServerReleaseNotes(httpResults);
    }
    else
    {
        about->handleServerReleaseNotes(result);
    }
}

void LLFloaterAbout::handleServerReleaseNotes(LLSD results)
{
    LLSD http_headers;
    if (results.has(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS))
    {
        LLSD http_results = results[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        http_headers = http_results[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS];
    }
    else
    {
        http_headers = results[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS];
    }

    std::string location = http_headers[HTTP_IN_HEADER_LOCATION].asString();
    if (location.empty())
    {
        location = LLTrans::getString("ErrorFetchingServerReleaseNotesURL");
    }
    LLAppViewer::instance()->setServerReleaseNotesURL(location);

    setSupportText();
}

class LLFloaterAboutListener: public LLEventAPI
{
public:
    LLFloaterAboutListener():
        LLEventAPI("LLFloaterAbout",
                   "LLFloaterAbout listener to retrieve About box info")
    {
        add("getInfo",
            "Request an LLSD::Map containing information used to populate About box",
            &LLFloaterAboutListener::getInfo,
            LLSD().with("reply", LLSD()));
    }

private:
    void getInfo(const LLSD& request) const
    {
        LLReqID reqid(request);
        LLSD reply(LLFloaterAbout::getInfo());
        reqid.stamp(reply);
        LLEventPumps::instance().obtain(request["reply"]).post(reply);
    }
};

static LLFloaterAboutListener floaterAboutListener;

void LLFloaterAbout::onClickCopyToClipboard()
{
    syncLocationChoice();
    LLViewerTextEditor *support_widget =
        getChild<LLViewerTextEditor>("support_editor", true);
    support_widget->selectAll();
    support_widget->copy();
    support_widget->deselect();
}

void LLFloaterAbout::onClickUpdateCheck()
{
    setUpdateListener();
}

void LLFloaterAbout::setSupportText()
{
#if LL_WINDOWS
    getWindow()->incBusyCount();
    getWindow()->setCursor(UI_CURSOR_ARROW);
#endif
#if LL_WINDOWS
    getWindow()->decBusyCount();
    getWindow()->setCursor(UI_CURSOR_ARROW);
#endif

    LLViewerTextEditor *support_widget =
        getChild<LLViewerTextEditor>("support_editor", true);

    LLUIColor about_color = LLUIColorTable::instance().getColor("TextFgReadOnlyColor");
    support_widget->clear();
    const auto location = mCanShowLocation && mLocationChoice->get()
        ? LLAppViewer::ReportLocation::Include : LLAppViewer::ReportLocation::Omit;
    support_widget->appendText(LLAppViewer::instance()->getViewerInfoString(false, location),
                               false, LLStyle::Params() .color(about_color));
}

//This is bound as a callback in postBuild()
void LLFloaterAbout::setUpdateListener()
{
    typedef std::vector<std::string> vec;

    //There are four possibilities:
    //no downloads directory or version directory in "getOSUserAppDir()/downloads"
    //   => no update
    //version directory exists and .done file is not present
    //   => download in progress
    //version directory exists and .done file exists
    //   => update ready for install
    //version directory, .done file and either .skip or .next file exists
    //   => update deferred
    bool downloads = false;
    std::string downloadDir = "";
    bool done = false;
    bool next = false;
    bool skip = false;

    LLSD info(LLFloaterAbout::getInfo());
    std::string version = info["VIEWER_VERSION_STR"].asString();
    std::string appDir = gDirUtilp->getOSUserAppDir();

    //drop down two directory levels so we aren't searching for markers among the log files and crash dumps
    //or among other possible viewer upgrade directories if the resident is running multiple viewer versions
    //we should end up with a path like ../downloads/1.2.3.456789
    vec file_vec = gDirUtilp->getFilesInDir(appDir);

    for(vec::const_iterator iter=file_vec.begin(); iter!=file_vec.end(); ++iter)
    {
        if ( (iter->rfind("downloads") ) )
        {
            vec dir_vec = gDirUtilp->getFilesInDir(*iter);
            for(vec::const_iterator dir_iter=dir_vec.begin(); dir_iter!=dir_vec.end(); ++dir_iter)
            {
                if ( (dir_iter->rfind(version)))
                {
                    downloads = true;
                    downloadDir = *dir_iter;
                }
            }
        }
    }

    if ( downloads )
    {
        for(vec::const_iterator iter=file_vec.begin(); iter!=file_vec.end(); ++iter)
        {
            if ( (iter->rfind(version)))
            {
                if ( (iter->rfind(".done") ) )
                {
                    done = true;
                }
                else if ( (iter->rfind(".next") ) )
                {
                    next = true;
                }
                else if ( (iter->rfind(".skip") ) )
                {
                    skip = true;
                }
            }
        }
    }

    if ( !downloads )
    {
        LLNotificationsUtil::add("UpdateViewerUpToDate");
    }
    else
    {
        if ( !done )
        {
            LLNotificationsUtil::add("UpdateDownloadInProgress");
        }
        else if ( (!next) && (!skip) )
        {
            LLNotificationsUtil::add("UpdateDownloadComplete");
        }
        else //done and there is a next or skip
        {
            LLNotificationsUtil::add("UpdateDeferred");
        }
    }
}

///----------------------------------------------------------------------------
/// LLFloaterAboutUtil
///----------------------------------------------------------------------------
void LLFloaterAboutUtil::registerFloater()
{
    LLFloaterReg::add("sl_about", "floater_about.xml",
        &LLFloaterReg::build<LLFloaterAbout>);
}

void LLFloaterAboutUtil::checkUpdatesAndNotify()
{
    LLFloaterAbout::setUpdateListener();
}

