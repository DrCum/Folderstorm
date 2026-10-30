/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsthemeviewer.h"
#include "fsthemepackage.h"
#include "fsthemeeditor.h"
#include "fsthemeselection.h"
#include "llnotificationsutil.h"
#include "fssessionworker.h"
#include "llappviewer.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llgl.h"
#include "llimagepng.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llstartup.h"
#include "lluictrlfactory.h"
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <optional>
#include <regex>
#include <cmath>
#if LL_WINDOWS
#include <shlobj.h>
#endif

namespace FSThemeViewer
{
namespace
{
namespace fs=std::filesystem;
bool offline=false,shown=false,keepPersonalColors=false;
std::optional<fs_theme::Revision> selected;
fs::path previewDirectory;
fs::path startupMarker,lastGoodSelection;
std::string requestId,diagnostic;
std::int64_t generation=0;
std::uint64_t sampleDowns=0,sampleUps=0,sampleCommits=0;
bool pinnedPress=false,sampleDisabled=false;
std::string lastCaptureRequest;
fs::file_time_type lastCaptureWrite;
std::uintmax_t lastCaptureSize=0;
bool captureRequestSeen=false;
std::optional<fs_theme::CaptureRequest> pendingCapture;
unsigned captureFrames=0;
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
    result["sample_interaction"]=boost::json::object{{"mouse_down",sampleDowns},{"mouse_up",sampleUps},{"commits",sampleCommits},
        {"pinned_press",pinnedPress},{"disabled",sampleDisabled}};
    const auto temporary=previewDirectory/"status.tmp",destination=previewDirectory/"status.json";
    {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<boost::json::serialize(result)<<'\n';output.flush();if(!output)return;}
#if LL_WINDOWS
    MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
    fs::rename(temporary,destination);
#endif
}
}
struct Preferences
{
    fs_viewer_theme::State accepted;
    fs_viewer_theme::Choice draft;
    std::vector<fs_viewer_theme::Available> rows;
    std::string status,acceptedStatus;
    bool edited=false;
};
namespace
{
fs::path libraryRoot(){return knownFolder(true)/"Folderstorm"/"theme-library";}
fs::path baseRoot(){return fs_theme::fromUtf8(gDirUtilp->getAppRODataDir());}
bool hasCustomHistory(const fs_viewer_theme::Selection& value)
{return value.current.custom || value.previous && value.previous->custom;}
std::string token(const fs_viewer_theme::Choice& choice)
{return choice.custom?choice.custom->id+"/"+choice.custom->revision:"builtin";}
void checkDraft(Preferences& state)
{
    if(!state.draft.custom){state.status=state.edited?"Restart after OK.":state.acceptedStatus;return;}
    try
    {
        const auto& ref=*state.draft.custom;
        const auto loaded=fs_theme::load(fs_theme::containedPath(libraryRoot(),ref.id+"/"+ref.revision),baseRoot());
        fs_theme::require(loaded.manifest.id==ref.id && loaded.revision==ref.revision,"viewer_selection","Selected revision identity differs");
        state.draft.skin=loaded.manifest.skin;state.draft.scheme=loaded.manifest.scheme;
        state.status=state.edited?"Restart after OK.":"Selected revision verified.";
    }
    catch(const std::exception& error){state.status=error.what();}
}
}
std::shared_ptr<Preferences> preferences(const std::string& skin,const std::string& scheme)
{
    auto result=std::make_shared<Preferences>();
    result->accepted=fs_viewer_theme::read(fs_theme::fromUtf8(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"")),{skin,scheme,{},{}});
    result->draft=result->accepted.accepted.current;result->acceptedStatus=result->accepted.diagnostic;
    try{result->rows=fs_viewer_theme::available(libraryRoot());checkDraft(*result);}
    catch(const std::exception& error){result->status=error.what();}
    if(result->accepted.usable && result->draft.custom){result->accepted.accepted.current.skin=result->draft.skin;result->accepted.accepted.current.scheme=result->draft.scheme;}
    if(!diagnostic.empty())result->status=diagnostic;
    result->acceptedStatus=result->status;return result;
}
std::vector<PreferenceOption> preferenceOptions(const Preferences& state)
{
    std::vector<PreferenceOption> result{{"builtin","Built-in"}};bool found=!state.draft.custom;
    for(const auto& row:state.rows)
    {
        const auto value=row.reference.id+"/"+row.reference.revision;
        result.push_back({value,row.name+" "+row.version+" ["+row.reference.revision.substr(0,8)+"]"});found=found || value==token(state.draft);
    }
    if(!found)result.push_back({token(state.draft),"Unavailable saved theme ["+state.draft.custom->revision.substr(0,8)+"]"});
    if(!state.accepted.usable)result.push_back({"invalid","Unavailable saved selection"});return result;
}
std::string preferenceToken(const Preferences& state){return !state.edited && !state.accepted.usable?"invalid":token(state.draft);}
std::string preferenceDiagnostic(const Preferences& state){return state.status;}
std::pair<std::string,std::string> preferenceBase(const Preferences& state){return {state.draft.skin,state.draft.scheme};}
bool preferenceCustom(const Preferences& state){return state.draft.custom.has_value();}
bool preferenceCanRollback(const Preferences& state){return state.accepted.usable && state.accepted.accepted.previous.has_value();}
void preferenceSelect(Preferences& state,const std::string& value)
{
    if(value=="invalid" || state.accepted.usable && value==token(state.draft))return;
    auto next=state.draft;
    if(value=="builtin")next.custom.reset();
    else
    {
        const auto row=std::find_if(state.rows.begin(),state.rows.end(),[&](const auto& item){return item.reference.id+"/"+item.reference.revision==value;});
        fs_theme::require(row!=state.rows.end(),"viewer_selection","The selected library revision is unavailable");next={row->skin,row->scheme,row->reference,{}};
    }
    const auto checked=fs_viewer_theme::prepare(state.accepted,next,libraryRoot(),baseRoot(),!state.accepted.usable);
    state.draft=checked.selected.current;state.edited=true;state.status="Restart after OK.";
}
void preferenceBuiltin(Preferences& state,const std::string& skin,const std::string& scheme)
{state.draft={skin,scheme,{},{}};state.edited=true;state.status="Restart after OK.";}
void preferenceRollback(Preferences& state)
{state.draft=fs_viewer_theme::prepareRollback(state.accepted,libraryRoot(),baseRoot()).selected.current;state.edited=true;state.status="Restart after OK.";}
void preferenceRefresh(Preferences& state){state.rows=fs_viewer_theme::available(libraryRoot());checkDraft(state);}
void preferenceCancel(Preferences& state){state.draft=state.accepted.accepted.current;state.edited=false;state.status=state.acceptedStatus;}
PreferenceCommit preferenceCommit(Preferences& state)
{
    if(!state.edited)
    {
        const bool preserve=state.draft.custom && (state.draft.skin!=gSavedSettings.getString("SkinCurrent") || state.draft.scheme!=gSavedSettings.getString("SkinCurrentTheme"));
        keepPersonalColors=keepPersonalColors || preserve;return {false,preserve};
    }
    const auto prepared=fs_viewer_theme::prepare(state.accepted,state.draft,libraryRoot(),baseRoot(),!state.accepted.usable);
    const bool preserve=prepared.changed && (state.accepted.accepted.current.custom || prepared.selected.current.custom);
    state.accepted=fs_viewer_theme::commit(state.accepted,prepared,libraryRoot(),baseRoot());
    state.draft=state.accepted.accepted.current;state.edited=false;state.status=state.acceptedStatus=prepared.changed?"Restart to use this theme.":"";
    keepPersonalColors=preserve || hasCustomHistory(state.accepted.accepted);return {prepared.changed,preserve};
}
bool preservePersonalColors(){return keepPersonalColors;}
bool customThemeActive(){return selected.has_value();}
bool preview(){return offline;}
void openEditor(bool library)
{
    const auto base=fs_theme::fromUtf8(gDirUtilp->getAppRODataDir());std::string message;
    if(!fs_theme::launchEditor(base,fs_theme::fromUtf8(gDirUtilp->getExecutablePathAndName()),base/"folderstorm-session-host.exe",library,message))
    {LLSD arguments;arguments["MESSAGE"]=message;LLNotificationsUtil::add("GenericAlert",arguments);}
}
bool configure()
{
    keepPersonalColors=false;
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
            const fs_viewer_theme::Choice builtin{gSavedSettings.getString("SkinCurrent"),gSavedSettings.getString("SkinCurrentTheme"),{},{}};
            auto state=fs_viewer_theme::read(settings,builtin);
            keepPersonalColors=state.exists && (!state.usable || hasCustomHistory(state.accepted));
            if(gSavedSettings.getBOOL("FSResetCustomTheme"))
            {
                keepPersonalColors=true;
                if(state.exists)fs_viewer_theme::commit(state,fs_viewer_theme::prepare(state,builtin,libraryRoot(),baseRoot(),true),libraryRoot(),baseRoot());
                std::error_code error;fs::remove(fs_theme::containedPath(settings,"custom-theme.starting.json"),error);
                return true;
            }
            fs_theme::require(state.usable,"viewer_selection",state.diagnostic);
            if(!state.exists)return true;
            const auto marker=fs_theme::containedPath(settings,"custom-theme.starting.json");
            if(state.accepted.current.custom && fs::is_regular_file(marker))
            {
                const auto failed=*state.accepted.current.custom;bool failedStartup=false;
                try
                {
                    const auto pending=fs_theme::parse(fs_theme::readFile(marker,4096));
                    fs_theme::require(pending.is_object(),"startup_recovery","Invalid startup marker");
                    failedStartup=text(pending.as_object(),"theme_id")==failed.id && text(pending.as_object(),"revision")==failed.revision;
                }
                catch(const std::exception& error){failedStartup=true;LL_WARNS("Theme")<<"Unusable startup marker: "<<error.what()<<LL_ENDL;}
                if(failedStartup)
                {
                    std::optional<fs_viewer_theme::Choice> good;
                    try
                    {
                        const auto path=fs_theme::containedPath(settings,"custom-theme.last-good.json");
                        if(fs::is_regular_file(path))good=fs_viewer_theme::selection(fs_theme::readFile(path,4096),builtin).current;
                    }
                    catch(const std::exception& error){LL_WARNS("Theme")<<"Last-good theme unavailable: "<<error.what()<<LL_ENDL;}
                    state=fs_viewer_theme::commit(state,fs_viewer_theme::recoverStartup(state,builtin,good,libraryRoot(),baseRoot()),libraryRoot(),baseRoot());
                    diagnostic="Recovered after an incomplete theme startup.";
                    atomicFile(fs_theme::containedPath(settings,"custom-theme.error.json"),boost::json::serialize(boost::json::object{{"diagnostic",diagnostic},{"theme_id",failed.id},{"revision",failed.revision}})+"\n");
                    std::error_code ignored;fs::remove(marker,ignored);
                }
            }
            const auto& current=state.accepted.current;
            if(current.custom){id=current.custom->id;revision=current.custom->revision;}
            else
            {
                const auto relative="skins/"+current.skin+(current.scheme.empty()?"":"/themes/"+current.scheme);
                fs_theme::require(fs::is_directory(fs_theme::containedPath(baseRoot(),relative)),"viewer_selection","Selected built-in base is unavailable");
                sessionValue("SkinCurrent",current.skin);sessionValue("SkinCurrentTheme",current.scheme);
            }
        }
        if(!id.empty())
        {
            fs_theme::require(std::regex_match(id,std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")) && fs_theme::validHash(revision),"selection","Invalid theme ID/revision");
            const auto library=knownFolder(true)/"Folderstorm"/"theme-library";
            selected=fs_theme::load(fs_theme::containedPath(library,id+"/"+revision),fs_theme::fromUtf8(gDirUtilp->getAppRODataDir()));
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
                atomicFile(fs_theme::containedPath(settings,"custom-theme.error.json"),boost::json::serialize(boost::json::object{{"diagnostic",diagnostic},{"fallback","built-in"}})+"\n");
                std::error_code ignored;fs::remove(fs_theme::containedPath(settings,"custom-theme.starting.json"),ignored);
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
            if(auto* button=sample->findChild<LLButton>("test_button"))
            {
                button->setMouseDownCallback([](LLUICtrl*,const LLSD&){++sampleDowns;writeStatus(diagnostic.empty()?"ready":"fallback_ready");});
                button->setMouseUpCallback([](LLUICtrl*,const LLSD&){++sampleUps;writeStatus(diagnostic.empty()?"ready":"fallback_ready");});
                button->setClickedCallback([](LLUICtrl*,const LLSD&){++sampleCommits;writeStatus(diagnostic.empty()?"ready":"fallback_ready");});
                // Pin the real LLButton draw state so a released mouse can be
                // used to inspect/capture pressed artwork in this offline tool.
                LLCheckBoxCtrl::Params pin;
                pin.name="theme_preview_pin_pressed";pin.label="Pin pressed state";
                pin.rect=LLRect(10,55,240,35);pin.follows.flags=FOLLOWS_LEFT|FOLLOWS_BOTTOM;
                auto* pressed=LLUICtrlFactory::create<LLCheckBoxCtrl>(pin);sample->addChild(pressed);
                pressed->setCommitCallback([button](LLUICtrl* control,const LLSD&)
                {pinnedPress=control->getValue().asBoolean();button->setForcePressedState(pinnedPress);writeStatus(diagnostic.empty()?"ready":"fallback_ready");});
                pin.name="theme_preview_disable_button";pin.label="Disable sample button";pin.rect=LLRect(10,30,240,10);
                auto* disabled=LLUICtrlFactory::create<LLCheckBoxCtrl>(pin);sample->addChild(disabled);
                disabled->setCommitCallback([button](LLUICtrl* control,const LLSD&)
                {sampleDisabled=control->getValue().asBoolean();button->setEnabled(!sampleDisabled);writeStatus(diagnostic.empty()?"ready":"fallback_ready");});
            }
            gViewerWindow->getWindow()->show();writeStatus(diagnostic.empty()?"ready":"fallback_ready");shown=true;
        }
        else {diagnostic="Native widget sample could not be built";writeStatus("failed");LLAppViewer::instance()->forceQuit();}
    }
    return true;
}
void captureFrame()
{
    // The offline startup frame has finished drawing its UI and has not swapped yet.
    if(!offline || !shown || !diagnostic.empty() || !gViewerWindow || !gViewerWindow->getWindow())return;
    std::string captureId;
    auto receipt=[&](const std::string& state,const std::string& message,boost::json::object details={})
    {
        details["capture_version"]=1;details["request_id"]=requestId;details["generation"]=generation;
        details["revision"]=selected?selected->revision:"";details["capture_id"]=captureId;
        details["state"]=state;details["diagnostic"]=message;
#if LL_WINDOWS
        details["pid"]=static_cast<std::uint64_t>(GetCurrentProcessId());
#endif
        fs_theme::capturePath(previewDirectory,"capture-status.json.tmp");
        atomicFile(fs_theme::capturePath(previewDirectory,"capture-status.json"),boost::json::serialize(details)+"\n");
    };
    try
    {
        const auto requestFile=fs_theme::capturePath(previewDirectory,"capture-request.json");
        if(fs::is_regular_file(requestFile))
        {
            const auto write=fs::last_write_time(requestFile);const auto size=fs::file_size(requestFile);
            if(!captureRequestSeen || write!=lastCaptureWrite || size!=lastCaptureSize)
            {
                captureRequestSeen=true;lastCaptureWrite=write;lastCaptureSize=size;
                const auto bytes=fs_theme::readFile(requestFile,4096),digest=fs_theme::hash(bytes);
                if(digest!=lastCaptureRequest)
                {
                    lastCaptureRequest=digest;pendingCapture.reset();
                    pendingCapture=fs_theme::captureRequest(bytes,requestId,generation,selected?selected->revision:"");captureFrames=0;
                }
            }
        }
        if(!pendingCapture || ++captureFrames<3)return;
        const auto request=*pendingCapture;pendingCapture.reset();captureId=request.id;
        const int width=gViewerWindow->getWindowWidthRaw(),height=gViewerWindow->getWindowHeightRaw();
        fs_theme::require(width>0 && height>0 && width<=16384 && height<=16384 && static_cast<std::int64_t>(width)*height<=16000000,"image","Preview window is too large to capture");
        LLPointer<LLImageRaw> pixels=new LLImageRaw(static_cast<U16>(width),static_cast<U16>(height),3);
        fs_theme::require(pixels->getData()!=nullptr,"image","Could not allocate preview pixels");
        {
            // Preserve readback state, including a possible asynchronous pixel-pack buffer.
            struct ReadState
            {
                GLint framebuffer,buffer,defaultBuffer,pbo,alignment,rowLength,skipRows,skipPixels;
                ReadState()
                {
                    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_READ_BUFFER,&buffer);
                    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pbo);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
                    glGetIntegerv(GL_PACK_ROW_LENGTH,&rowLength);glGetIntegerv(GL_PACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipPixels);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glGetIntegerv(GL_READ_BUFFER,&defaultBuffer);glReadBuffer(GL_BACK);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
                    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
                }
                ~ReadState()
                {
                    glReadBuffer(defaultBuffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);glReadBuffer(buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
                    glPixelStorei(GL_PACK_ALIGNMENT,alignment);glPixelStorei(GL_PACK_ROW_LENGTH,rowLength);glPixelStorei(GL_PACK_SKIP_ROWS,skipRows);glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);
                }
            } restore;
            glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels->getData());
        }
        const double factor=std::min({1.0,static_cast<double>(request.width)/width,static_cast<double>(request.height)/height});
        const int outputWidth=std::max(1,static_cast<int>(std::floor(width*factor))),outputHeight=std::max(1,static_cast<int>(std::floor(height*factor)));
        if(outputWidth!=width || outputHeight!=height)fs_theme::require(pixels->scale(outputWidth,outputHeight),"image","Could not resize preview image");
        LLPointer<LLImagePNG> png=new LLImagePNG();fs_theme::require(png->encode(pixels,0.f),"image","Could not encode preview image");
        fs_theme::require(png->getDataSize()>0 && png->getDataSize()<=32*1024*1024,"size","Preview PNG exceeds its byte limit");
        const std::string bytes(reinterpret_cast<const char*>(png->getData()),png->getDataSize()),leaf="capture-"+captureId+".png";
        fs_theme::capturePath(previewDirectory,leaf+".tmp");atomicFile(fs_theme::capturePath(previewDirectory,leaf),bytes);
        receipt("captured","",{{"image",leaf},{"sha256",fs_theme::hash(bytes)},{"width",outputWidth},{"height",outputHeight},{"source_width",width},{"source_height",height}});
    }
    catch(const std::exception& error)
    {
        pendingCapture.reset();LL_WARNS("Theme")<<"Preview capture failed: "<<error.what()<<LL_ENDL;
        try {receipt("failed",error.what());}catch(const std::exception&){ }
    }
}
}
