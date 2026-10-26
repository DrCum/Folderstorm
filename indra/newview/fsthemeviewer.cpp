/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsthemeviewer.h"
#include "fsthemepackage.h"
#include "fssessionworker.h"
#include "llappviewer.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llstartup.h"
#include <fstream>
#include <optional>
#include <regex>
#if LL_WINDOWS
#include <shlobj.h>
#endif

namespace FSThemeViewer
{
namespace
{
namespace fs=std::filesystem;
bool offline=false,shown=false;
std::optional<fs_theme::Revision> selected;
fs::path previewDirectory;
fs::path startupMarker,lastGoodSelection;
std::string requestId,diagnostic;
std::int64_t generation=0;
void sessionValue(const char* name,const LLSD& value)
{if(auto control=gSavedSettings.getControl(name))control->setValue(value,false);}
void atomicFile(const fs::path& destination,const std::string& bytes)
{
    const auto temporary=destination.wstring()+L".tmp";
    {std::ofstream output(fs::path(temporary),std::ios::binary|std::ios::trunc);output<<bytes;output.flush();fs_theme::require(static_cast<bool>(output),"profile","Could not write theme state");}
#if LL_WINDOWS
    fs_theme::require(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"profile","Could not publish theme state");
#else
    fs::rename(temporary,destination);
#endif
}
fs::path knownFolder(bool roaming)
{
#if LL_WINDOWS
    PWSTR result=nullptr;
    const auto status=SHGetKnownFolderPath(roaming?FOLDERID_RoamingAppData:FOLDERID_LocalAppData,0,nullptr,&result);
    fs_theme::require(SUCCEEDED(status) && result,"profile","Known Windows user folder unavailable");
    const fs::path path(result);CoTaskMemFree(result);return path;
#else
    return fs_theme::fromUtf8(gDirUtilp->getOSUserDir());
#endif
}
std::string text(const boost::json::object& object,const char* field)
{
    const auto* value=object.if_contains(field);fs_theme::require(value && value->is_string(),"preview_request",field);return std::string(value->as_string());
}
void writeStatus(const std::string& state)
{
    if(previewDirectory.empty())return;
    boost::json::object result{{"preview_version",1},{"request_id",requestId},{"generation",generation},{"state",state},{"diagnostic",diagnostic},
        {"revision",selected?selected->revision:""},{"base_fingerprint",selected?selected->manifest.fingerprint:""}};
#if LL_WINDOWS
    result["pid"]=static_cast<std::uint64_t>(GetCurrentProcessId());
#endif
    const auto temporary=previewDirectory/"status.tmp",destination=previewDirectory/"status.json";
    {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<boost::json::serialize(result)<<'\n';output.flush();if(!output)return;}
#if LL_WINDOWS
    MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
    fs::rename(temporary,destination);
#endif
}
}
bool preview(){return offline;}
bool configure()
{
    const auto requestPath=gSavedSettings.getString("FSThemePreviewRequest");offline=!requestPath.empty();
    const auto settings=fs_theme::fromUtf8(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,""));
    try
    {
        std::string id,revision;
        if(offline)
        {
            fs_theme::require(!FSSessionWorker::managed(),"profile","Theme preview cannot attach to a managed viewer session");
            const auto path=fs_theme::fromUtf8(requestPath);const auto session=fs_theme::toUtf8(path.parent_path().filename());
            fs_theme::require(std::regex_match(session,std::regex("[0-9a-f]{32}")),"preview_request","Invalid preview session");
            const auto expected=knownFolder(false)/"Folderstorm"/"theme-preview"/session;
            fs_theme::require(fs::weakly_canonical(path)==fs::weakly_canonical(expected/"request.json"),"preview_request","Request is outside its preview session");
            const auto value=fs_theme::parse(fs_theme::readFile(path));fs_theme::require(value.is_object(),"preview_request","Expected object");const auto& request=value.as_object();
            fs_theme::require(request.at("preview_version").is_int64() && request.at("preview_version").as_int64()==1,"preview_request","Unsupported preview protocol");
            requestId=text(request,"request_id");fs_theme::require(fs_theme::validHash(requestId),"preview_request","Invalid request ID");
            fs_theme::require(request.at("generation").is_int64() && request.at("generation").as_int64()>0,"preview_request","Invalid generation");generation=request.at("generation").as_int64();
            fs_theme::require(fs::weakly_canonical(fs_theme::fromUtf8(gDirUtilp->getOSUserDir()))==fs::weakly_canonical(expected/"roaming"),"profile","Preview roaming profile is not isolated");
            fs_theme::require(fs::weakly_canonical(fs_theme::fromUtf8(gDirUtilp->getOSCacheDir()))==fs::weakly_canonical(expected/"local"),"profile","Preview cache profile is not isolated");
            previewDirectory=expected;id=text(request,"theme_id");revision=text(request,"revision");
            gSavedSettings.setBOOL("AutoLogin",false);gSavedSettings.setBOOL("AllowMultipleViewers",true);gSavedSettings.setBOOL("SLURLPassToOtherInstance",false);
            sessionValue("SkinCurrent","ansastorm");sessionValue("SkinCurrentTheme","folderstorm");sessionValue("Language","en");
            gSavedSettings.setU32("WindowWidth",1280);gSavedSettings.setU32("WindowHeight",900);
            sessionValue("WindowMaximized",false);
        }
        else
        {
            const auto selection=settings/"custom-theme.json";
            if(gSavedSettings.getBOOL("FSResetCustomTheme"))
            {
                if(fs::is_regular_file(selection))atomicFile(selection,"null\n");
                std::error_code error;fs::remove(settings/"custom-theme.starting.json",error);
                return true;
            }
            if(!fs::is_regular_file(selection))return true;
            auto value=fs_theme::parse(fs_theme::readFile(selection));if(value.is_null())return true;
            fs_theme::require(value.is_object(),"selection","Expected theme selection");id=text(value.as_object(),"theme_id");revision=text(value.as_object(),"revision");
            const auto marker=settings/"custom-theme.starting.json";
            if(fs::is_regular_file(marker))
            {
                const auto pending=fs_theme::parse(fs_theme::readFile(marker));
                if(pending.is_object() && text(pending.as_object(),"theme_id")==id && text(pending.as_object(),"revision")==revision)
                {
                    const auto good=settings/"custom-theme.last-good.json";
                    fs_theme::require(fs::is_regular_file(good),"startup_recovery","Previous theme startup did not finish; using the built-in base");
                    const auto previous=fs_theme::parse(fs_theme::readFile(good));fs_theme::require(previous.is_object(),"startup_recovery","Previous theme startup did not finish");
                    const auto oldId=text(previous.as_object(),"theme_id"),oldRevision=text(previous.as_object(),"revision");
                    fs_theme::require(oldId!=id || oldRevision!=revision,"startup_recovery","Failed revision requires an explicit reset or replacement");
                    id=oldId;revision=oldRevision;
                    atomicFile(selection,boost::json::serialize(previous)+"\n");
                }
            }
        }
        if(!id.empty())
        {
            fs_theme::require(std::regex_match(id,std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")) && fs_theme::validHash(revision),"selection","Invalid theme ID/revision");
            const auto library=knownFolder(true)/"Folderstorm"/"theme-library";
            selected=fs_theme::load(library/fs_theme::fromUtf8(id)/fs_theme::fromUtf8(revision),fs_theme::fromUtf8(gDirUtilp->getAppRODataDir()));
            fs_theme::require(selected->revision==revision && selected->manifest.id==id,"selection","Selected revision differs");
            sessionValue("SkinCurrent",selected->manifest.skin);sessionValue("SkinCurrentTheme",selected->manifest.scheme);
            gDirUtilp->setCustomThemeDir(fs_theme::toUtf8(selected->viewer()));
            if(!offline)
            {
                startupMarker=settings/"custom-theme.starting.json";lastGoodSelection=settings/"custom-theme.last-good.json";
                atomicFile(startupMarker,boost::json::serialize(boost::json::object{{"theme_id",id},{"revision",revision}})+"\n");
            }
            LL_INFOS("Theme")<<"Loaded custom theme "<<id<<" revision "<<revision<<LL_ENDL;
        }
        writeStatus("validated");return true;
    }
    catch(const std::exception& error)
    {
        selected.reset();gDirUtilp->setCustomThemeDir("");diagnostic=error.what();
        LL_WARNS("Theme")<<"Using built-in theme: "<<diagnostic<<LL_ENDL;
        if(offline && previewDirectory.empty())return false;
        if(!offline)
        {
            try
            {
                atomicFile(settings/"custom-theme.error.json",boost::json::serialize(boost::json::object{{"diagnostic",diagnostic},{"fallback","built-in"}})+"\n");
                atomicFile(settings/"custom-theme.json","null\n");
                std::error_code ignored;fs::remove(settings/"custom-theme.starting.json",ignored);
            }
            catch(const std::exception& stateError){LL_WARNS("Theme")<<"Could not save theme recovery: "<<stateError.what()<<LL_ENDL;}
        }
        writeStatus("fallback");return true;
    }
}
bool idlePreview()
{
    if(!offline)
    {
        if(!startupMarker.empty() && LLStartUp::getStartupState()>=STATE_LOGIN_WAIT)
        {
            try
            {
                atomicFile(lastGoodSelection,fs_theme::readFile(startupMarker));
                std::error_code error;fs::remove(startupMarker,error);startupMarker.clear();
            }
            catch(const std::exception& error){LL_WARNS("Theme")<<"Could not commit last-good theme: "<<error.what()<<LL_ENDL;}
        }
        return false;
    }
    if(!shown)
    {
        gViewerWindow->setShowProgress(false,false);LLSplashScreen::hide();
        if(auto* sample=LLFloaterReg::showInstance("test_widgets"))
        {
            sample->setTitle(diagnostic.empty()?"Theme preview":"Theme preview (base fallback)");sample->center();
            gViewerWindow->getWindow()->show();writeStatus(diagnostic.empty()?"ready":"fallback_ready");shown=true;
        }
        else {diagnostic="Native widget sample could not be built";writeStatus("failed");LLAppViewer::instance()->forceQuit();}
    }
    return true;
}
}
