/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsthemeviewer.h"
#include "fsthemepackage.h"
#include "fsthemeeditor.h"
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
bool offline=false,shown=false;
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
bool preview(){return offline;}
void openEditor(bool library)
{
    const auto base=fs_theme::fromUtf8(gDirUtilp->getAppRODataDir());std::string message;
    if(!fs_theme::launchEditor(base,fs_theme::fromUtf8(gDirUtilp->getExecutablePathAndName()),base/"folderstorm-session-host.exe",library,message))
    {LLSD arguments;arguments["MESSAGE"]=message;LLNotificationsUtil::add("GenericAlert",arguments);}
}
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
