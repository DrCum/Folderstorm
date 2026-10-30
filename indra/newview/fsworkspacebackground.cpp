/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsworkspacebackground.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "llfile.h"
#include "llglslshader.h"
#include "llimage.h"
#include "llimagepng.h"
#include "llrender2dutils.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"
#include "llviewernetwork.h"
#include "llviewertexture.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "fssessionworker.h"
#include "llviewershadermgr.h"
#include <chrono>
#include <future>
#include <set>

namespace FSWorkspaceBackground
{
namespace
{
bool ready()
{ return !LLApp::isExiting() && !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED && gAgent.getID().notNull(); }
S32 pixel(double value) { return static_cast<S32>(std::lround(value)); }
Rect rect(const LLRect& r) { return {double(r.mLeft),double(r.mBottom),double(r.mRight),double(r.mTop)}; }
bool workspaceVisible()
{
    return ready() && gViewerWindow && gSavedSettings.getBOOL("FSWorldViewEnabled") &&
        !outside(rect(gViewerWindow->getWorldViewBaseRectRaw(gAgentCamera.cameraMouselook())),viewport()).empty();
}
std::string directory()
{ return ready() ? gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT,"workspace_backgrounds") : ""; }
std::string key(const Image& image)
{ return image.source == Source::Color ? "" : std::to_string(static_cast<int>(image.source))+":"+image.grid+":"+image.asset; }
std::vector<Rect> queryDisplays()
{
    std::vector<Rect> result;
    std::vector<LLWindow::MonitorRect> monitors;
    if (gViewerWindow && gViewerWindow->getWindow()->getMonitorRectsInClient(monitors))
        for (const auto& monitor:monitors) result.push_back(rect(monitor.rect));
    if (result.empty()) result.push_back(client());
    return orderedDisplays(std::move(result));
}
struct ImportResult { std::string asset, error; int width = 0, height = 0; };
ImportResult copyImage(const std::string& filename, const std::string& folder)
{
    ImportResult result;
    llstat info{};
    const auto extension = gDirUtilp->getExtension(filename);
    if ((extension != "png" && extension != "jpg" && extension != "jpeg" && extension != "bmp" && extension != "tga") ||
        LLFile::stat(filename,&info) || info.st_size <= 0 || info.st_size > 32*1024*1024)
    { result.error = "Choose a PNG, JPEG, BMP or TGA up to 32 MiB."; return result; }
    LLPointer<LLImageFormatted> encoded = LLImageFormatted::createFromExtension(extension);
    if (!encoded || !encoded->load(filename,static_cast<S32>(info.st_size)) || !encoded->getWidth() || !encoded->getHeight() ||
        encoded->getWidth() > 8192 || encoded->getHeight() > 8192 ||
        static_cast<U64>(encoded->getWidth())*encoded->getHeight() > 32*1024*1024)
    { result.error = "Image is invalid or exceeds 32 megapixels / 8192 pixels per side."; return result; }
    LLPointer<LLImageRaw> raw = new LLImageRaw;
    if (!encoded->decode(raw,0.f) || raw->getComponents() < 3 || raw->getComponents() > 4)
    { result.error = "Could not decode this image."; return result; }
    // Normalize once. Managed files are bounded, immutable PNGs, not live file references.
    result.width=raw->getWidth(); result.height=raw->getHeight();
    raw->biasedScaleToPowerOfTwo(4096);
    LLPointer<LLImagePNG> png = new LLImagePNG;
    result.asset = LLUUID::generateNewID().asString()+".png";
    const auto final = folder+gDirUtilp->getDirDelimiter()+result.asset;
    const auto temporary = final+".tmp";
    if ((LLFile::mkdir(folder) && errno != EEXIST) || !png->encode(raw,0.f) || !png->save(temporary) || LLFile::rename(temporary,final))
    { LLFile::remove(temporary); result.asset.clear(); result.error = "Could not save the background in your profile."; }
    return result;
}
struct Service
{
    bool initialized = false, stopped = false;
    LLUUID account, login;
    std::string grid;
    Config config, extra;
    std::vector<Rect> displayGeometry;
    bool hasExtra = false;
    std::map<std::string,LLPointer<LLViewerFetchedTexture>> textures;
    std::map<std::string,std::string> errors;
    std::future<ImportResult> importJob;
    std::function<bool(Image,std::string)> done;
    LLUUID importAccount, importLogin;
    std::string importGrid, importDirectory;
    LLFrameTimer timer, geometryTimer;
    static void idle(void* opaque) { static_cast<Service*>(opaque)->poll(false); }
    bool sourceCurrent() const
    { return ready() && account == gAgent.getID() && login == gAgent.getSessionID() && grid == LLGridManager::instance().getGrid(); }
    void clear() { textures.clear(); errors.clear(); hasExtra=false; }
    void demand(const Image& image, std::set<std::string>& needed)
    {
        if (image.source == Source::Color) return;
        const auto id = key(image); needed.insert(id);
        if (textures.count(id) || errors.count(id)) return;
        // Shared viewer fetch/decode/cache; no per-region GL texture allocations.
        LLPointer<LLViewerFetchedTexture> texture;
        if (image.source == Source::Texture)
        {
            if (image.grid != grid) { errors[id]="Texture belongs to a different grid."; return; }
            texture = LLViewerTextureManager::getFetchedTexture(LLUUID(image.asset));
        }
        else
        {
            if (!localAsset(image.asset)) return;
            const auto path = directory()+gDirUtilp->getDirDelimiter()+image.asset;
            if (!LLFile::isfile(path)) { errors[id]="Image is missing. Locate a replacement."; return; }
            // FromFile searches skin textures; managed profile images use the local URL path.
            texture = LLViewerTextureManager::getFetchedTextureFromUrl("file://"+path,FTT_LOCAL_FILE);
        }
        if (texture) textures.emplace(id,std::move(texture));
    }
    void finishImport()
    {
        if (!importJob.valid() || importJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        ImportResult result;
        try { result=importJob.get(); }
        catch (...) { result.error="Could not import this image."; }
        auto callback=std::move(done);
        done={};
        bool accepted=false;
        if (ready() && importAccount == gAgent.getID() && importLogin == gAgent.getSessionID() && importGrid == LLGridManager::instance().getGrid() && callback)
        {
            Image image;
            image.source=Source::Local; image.asset=result.asset;
            image.width=result.width; image.height=result.height;
            accepted=callback(image,result.error);
        }
        if (!accepted && !result.asset.empty()) LLFile::remove(importDirectory+gDirUtilp->getDirDelimiter()+result.asset);
    }
    void poll(bool force)
    {
        if (stopped) return;
        finishImport();
        if (!force && timer.getElapsedTimeF32() < .2f) return;
        timer.reset();
        if (displayGeometry.empty() || geometryTimer.getElapsedTimeF32() >= .2f)
        {
            displayGeometry=queryDisplays(); // Never enumerate native displays while drawing.
            geometryTimer.reset();
        }
        if (!sourceCurrent()) { clear(); account=gAgent.getID(); login=gAgent.getSessionID(); grid=LLGridManager::instance().getGrid(); }
        if (!ready()) { clear(); return; }
        config=resolve(current(),defaults());
        std::set<std::string> needed;
        if (workspaceVisible())
        {
            const auto screens=displays(); const auto topology=arrangement(screens);
            if (config.common.mode == Mode::Span) demand(config.common,needed);
            else for (size_t i=0;i<screens.size();++i)
                if (!outside(intersect(screens[i],client()),viewport()).empty()) demand(forDisplay(config,topology,static_cast<int>(i)),needed);
        }
        if (hasExtra)
        {
            demand(extra.common,needed);
            const auto screens=displays(); const auto topology=arrangement(screens);
            if (extra.common.mode!=Mode::Span)
                for (size_t i=0;i<screens.size();++i) demand(forDisplay(extra,topology,static_cast<int>(i)),needed);
        }
        for (auto it=textures.begin();it!=textures.end();) if (!needed.count(it->first)) it=textures.erase(it); else ++it;
        for (auto it=errors.begin();it!=errors.end();) if (!needed.count(it->first)) it=errors.erase(it); else ++it;
    }
};
Service& service() { static Service s; return s; }
void paint(const Image& image, const Rect& target, const Rect& clip, double pixelScale=1.)
{
    const auto area=intersect(target,clip); if (area.empty()) return;
    gl_rect_2d(pixel(area.left),pixel(area.top),pixel(area.right),pixel(area.bottom),
        LLColor4(image.color[0],image.color[1],image.color[2],1.f));
    const auto found=service().textures.find(key(image));
    if (found == service().textures.end()) return;
    auto texture=found->second;
    if (!texture || texture->isMissingAsset()) return;
    texture->addTextureStats(static_cast<F32>(area.width()*area.height()));
    if (!texture->hasGLTexture()) return;
    const auto p=place(target,(image.source == Source::Local ? image.width : texture->getFullWidth())*pixelScale,(image.source == Source::Local ? image.height : texture->getFullHeight())*pixelScale,image.mode);
    const auto dest=intersect(p.destination,area); if (dest.empty()) return;
    const double du=p.uv.width()/p.destination.width(), dv=p.uv.height()/p.destination.height();
    LLRectf uv(static_cast<F32>(p.uv.left+(dest.left-p.destination.left)*du),static_cast<F32>(p.uv.bottom+(dest.top-p.destination.bottom)*dv),
        static_cast<F32>(p.uv.left+(dest.right-p.destination.left)*du),static_cast<F32>(p.uv.bottom+(dest.bottom-p.destination.bottom)*dv));
    const auto address=texture->getAddressMode();
    texture->setAddressMode(image.mode == Mode::Tile ? LLTexUnit::TAM_WRAP : LLTexUnit::TAM_CLAMP);
    const F32 b=image.brightness;
    gl_draw_scaled_image(pixel(dest.left),pixel(dest.bottom),pixel(dest.right)-pixel(dest.left),pixel(dest.top)-pixel(dest.bottom),texture,LLColor4(b,b,b,1.f),uv);
    gGL.flush(); texture->setAddressMode(address);
}
}
Choice current()
{ Choice value; fromLLSD(gSavedPerAccountSettings.getLLSD("FSWorkspaceBackground"),value); return value; }
Config defaults()
{ Config value; fromLLSD(gSavedPerAccountSettings.getLLSD("FSWorkspaceBackgroundDefault"),value); return value; }
Rect client() { return gViewerWindow ? rect(gViewerWindow->getWindowRectRaw()) : Rect{}; }
Rect viewport() { return gViewerWindow ? rect(gViewerWindow->getWorldViewRectRaw()) : Rect{}; }
std::vector<Rect> displays()
{
    const auto& screens=service().displayGeometry;
    return screens.empty() ? std::vector<Rect>{client()} : screens;
}
void initialize()
{
    auto& s=service();
    if (!s.initialized && !s.stopped) { gIdleCallbacks.addFunction(Service::idle,&s); s.initialized=true; }
}
void refresh() { initialize(); service().poll(true); }
void shutdown()
{
    auto& s=service(); s.stopped=true;
    if (s.initialized) gIdleCallbacks.deleteFunction(Service::idle,&s);
    s.initialized=false; s.clear(); s.done={};
    if (s.importJob.valid())
    {
        try
        {
            const auto result=s.importJob.get();
            if (!result.asset.empty()) LLFile::remove(s.importDirectory+gDirUtilp->getDirDelimiter()+result.asset);
        }
        catch (...) {}
    }
}
void previewDemand(const Config* config)
{ initialize(); auto& s=service(); s.poll(true); s.hasExtra=config!=nullptr; if (config) s.extra=*config; s.poll(true); }
void preview(const Config& config, const Rect& target)
{
    const auto screens=displays(); const auto frame=bounds(screens); if (frame.empty()) return;
    const double scale=(std::min)(target.width()/frame.width(),target.height()/frame.height());
    auto project=[&](const Rect& r){return Rect{target.left+(r.left-frame.left)*scale,target.bottom+(r.bottom-frame.bottom)*scale,
        target.left+(r.right-frame.left)*scale,target.bottom+(r.top-frame.bottom)*scale};};
    const auto clips=outside(client(),viewport());
    if (config.common.mode == Mode::Span)
    { const auto image=project(bounds(clips)); for (const auto& clip:clips) paint(config.common,image,project(clip),scale); }
    else for (size_t i=0;i<screens.size();++i)
    {
        const auto regions=outside(intersect(client(),screens[i]),viewport()); const auto image=project(bounds(regions));
        for (const auto& clip:regions) paint(forDisplay(config,arrangement(screens),static_cast<int>(i)),image,project(clip),scale);
    }
}
std::string imageStatus(const Image& image)
{
    if (image.source == Source::Color) return {};
    const auto id=key(image); const auto& s=service(); const auto error=s.errors.find(id);
    if (error!=s.errors.end()) return error->second;
    const auto found=s.textures.find(id);
    if (found == s.textures.end()) return "Loading image…";
    if (found->second->isMissingAsset()) return "Texture unavailable.";
    return found->second->hasGLTexture() ? "" : "Loading image…";
}
bool importing() { return service().importJob.valid(); }
bool importImage(const std::string& filename, std::function<bool(Image,std::string)> done)
{
    if (!ready() || importing()) return false;
    initialize(); auto& s=service(); s.importAccount=gAgent.getID(); s.importLogin=gAgent.getSessionID();
    s.importGrid=LLGridManager::instance().getGrid(); s.importDirectory=directory(); s.done=std::move(done);
    try { s.importJob=std::async(std::launch::async,copyImage,filename,s.importDirectory); }
    catch (...) { s.done={}; return false; }
    return true;
}
void draw()
{
    initialize(); auto& s=service();
    if (!s.sourceCurrent() || !workspaceVisible() || gSnapshot || FSSessionWorker::monitorRendering()) return;
    const auto clips=outside(client(),viewport()); if (clips.empty()) return;
    GLint savedViewport[4]; std::copy(gGLViewport,gGLViewport+4,savedViewport);
    GLint polygon[2]; glGetIntegerv(GL_POLYGON_MODE,polygon);
    auto* shader=LLGLSLShader::sCurBoundShaderPtr;
    gGL.matrixMode(LLRender::MM_PROJECTION); gGL.pushMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW); gGL.pushMatrix();
    gGL.pushUIMatrix(); gGL.loadUIIdentity();
    {
        LLGLSUIDefault state;
        LLGLDisable scissor(GL_SCISSOR_TEST);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        gUIProgram.bind(); gViewerWindow->setup2DRender();
        const auto screens=displays(); const auto topology=arrangement(screens);
        if (s.config.common.mode == Mode::Span)
        { const auto target=bounds(clips); for (const auto& clip:clips) paint(s.config.common,target,clip); }
        else for (size_t i=0;i<screens.size();++i)
        {
            const auto regions=outside(intersect(client(),screens[i]),viewport()); const auto target=bounds(regions);
            for (const auto& clip:regions) paint(forDisplay(s.config,topology,static_cast<int>(i)),target,clip);
        }
        gGL.flush(); gGL.color4f(1.f,1.f,1.f,1.f);
    }
    gGL.popUIMatrix();
    glPolygonMode(GL_FRONT_AND_BACK,static_cast<GLenum>(polygon[0]));
    gGL.matrixMode(LLRender::MM_PROJECTION); gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW); gGL.popMatrix();
    std::copy(savedViewport,savedViewport+4,gGLViewport); glViewport(savedViewport[0],savedViewport[1],savedViewport[2],savedViewport[3]);
    if (shader) shader->bind(); else LLGLSLShader::unbind();
}
}
