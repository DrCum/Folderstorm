/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fspanelpreferencebackgrounds.h"
#include "fsworkspacebackground.h"
#include "fsworkspacecontroller.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcolorswatch.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "llsliderctrl.h"
#include "lltextbox.h"
#include "lltexturectrl.h"
#include "llviewermenufile.h"
#include "llviewernetwork.h"

using namespace FSWorkspaceBackground;
namespace
{
S32 pixel(double value) { return static_cast<S32>(std::lround(value)); }
class BackgroundDiagram : public LLPanel
{
public:
    BackgroundDiagram(const Params& p) : LLPanel(p) {}
    Config config;
    int selected = -1;
    std::function<void(int)> select;
    std::vector<LLRect> hit;
    void draw() override
    {
        const auto screens=displays(); const auto frame=bounds(screens); hit.clear();
        if (frame.empty()) return;
        const Rect area{8.,8.,double(getRect().getWidth()-8),double(getRect().getHeight()-8)};
        const auto scale=(std::min)(area.width()/frame.width(),area.height()/frame.height());
        Rect target{area.left+(area.width()-frame.width()*scale)*.5,area.bottom+(area.height()-frame.height()*scale)*.5,0.,0.};
        target.right=target.left+frame.width()*scale; target.top=target.bottom+frame.height()*scale;
        preview(config,target);
        auto project=[&](const Rect& r) { return LLRect(pixel(target.left+(r.left-frame.left)*scale),pixel(target.bottom+(r.top-frame.bottom)*scale),
            pixel(target.left+(r.right-frame.left)*scale),pixel(target.bottom+(r.bottom-frame.bottom)*scale)); };
        const auto world=intersect(viewport(),client());
        if (!world.empty()) gl_rect_2d(project(world),LLColor4(.12f,.12f,.16f,.85f));
        for (size_t i=0;i<screens.size();++i)
        {
            const auto r=project(screens[i]); hit.push_back(r);
            const auto color=LLUIColorTable::instance().getColor(static_cast<int>(i)==selected ? "EmphasisColor" : "LabelTextColor");
            gl_rect_2d(r,color,false);
            LLFontGL::getFontSansSerifSmall()->renderUTF8(std::to_string(i+1),0,static_cast<F32>(r.mLeft+5),static_cast<F32>(r.mTop-15),
                color,LLFontGL::LEFT,LLFontGL::BASELINE,LLFontGL::NORMAL,LLFontGL::NO_SHADOW);
        }
    }
    bool handleMouseDown(S32 x,S32 y,MASK mask) override
    {
        for (size_t i=0;i<hit.size();++i) if (hit[i].pointInRect(x,y)) { if (select) select(static_cast<int>(i)); return true; }
        return LLPanel::handleMouseDown(x,y,mask);
    }
};
static LLDefaultChildRegistry::Register<BackgroundDiagram> diagram("workspace_background_diagram");
static LLPanelInjector<FSPanelPreferenceBackgrounds> panel("panel_preference_backgrounds");
}
bool FSPanelPreferenceBackgrounds::postBuild()
{
    for (const char* name:{"background_source","background_mode"}) getChild<LLComboBox>(name)->setCommitCallback([this](LLUICtrl*,const LLSD&){changed();});
    getChild<LLSliderCtrl>("background_brightness")->setCommitCallback([this](LLUICtrl*,const LLSD&){changed();});
    auto* swatch=getChild<LLColorSwatchCtrl>("background_color");
    swatch->setCommitCallback([this](LLUICtrl*,const LLSD&){changed();});
    swatch->setPreviewCallback([this](LLUICtrl*,const LLSD&){changed();});
    swatch->setOnCancelCallback([this](LLUICtrl*,const LLSD&){changed();});
    auto* texture=getChild<LLTextureCtrl>("background_texture");
    texture->setAllowLocalTexture(false); texture->setFilterPermissionMasks(0); texture->setCanApply(true,true);
    texture->setCommitCallback([this](LLUICtrl*,const LLSD&){changed();});
    getChild<LLLineEditor>("background_uuid")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    {
        LLUUID id; const auto text=getChild<LLLineEditor>("background_uuid")->getText();
        if (!id.set(text,false) || id.isNull()) { mError="Enter a valid texture UUID."; return; }
        getChild<LLTextureCtrl>("background_texture")->setImageAssetID(id); changed();
    });
    getChild<LLButton>("background_file")->setCommitCallback([this](LLUICtrl*,const LLSD&){chooseFile();});
    getChild<LLComboBox>("background_edit")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    { ++mEdit; mDefault=getChild<LLComboBox>("background_edit")->getValue().asString()=="default"; load(); });
    getChild<LLComboBox>("background_display")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    { ++mEdit; mDisplay=getChild<LLComboBox>("background_display")->getValue().asInteger(); controls(); });
    getChild<BackgroundDiagram>("background_preview")->select=[this](int index)
    { ++mEdit; mDisplay=index; getChild<LLComboBox>("background_display")->setValue(index); controls(); };
    getChild<LLCheckBoxCtrl>("background_inherit")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    {
        ++mEdit; mChoice.custom=!getChild<LLCheckBoxCtrl>("background_inherit")->get();
        if (mChoice.custom) mConfig=resolve(current(),defaults());
        else mConfig=defaults();
        save(); controls();
    });
    getChild<LLCheckBoxCtrl>("background_override")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    {
        ++mEdit;
        if (getChild<LLCheckBoxCtrl>("background_override")->get())
        { mConfig.arrangement=mTopology; mConfig.displays[mDisplay]=mConfig.common; if (mConfig.displays[mDisplay].mode==Mode::Span) mConfig.displays[mDisplay].mode=Mode::Fill; }
        else mConfig.displays.erase(mDisplay);
        save(); controls();
    });
    getChild<LLButton>("background_reassign")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    { ++mEdit; mConfig.arrangement=mTopology; save(); controls(); });
    getChild<LLButton>("background_reset")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    { ++mEdit; if (mDefault) mConfig={}; else { mChoice={}; mConfig=defaults(); } save(); controls(); });
    load(); return LLPanel::postBuild();
}
bool FSPanelPreferenceBackgrounds::sameSource() const
{ return mAccount==gAgent.getID() && mLogin==gAgent.getSessionID() && FSWorkspaceController::instance().available(); }
void FSPanelPreferenceBackgrounds::load()
{
    ++mEdit; mAccount=gAgent.getID(); mLogin=gAgent.getSessionID();
    mRevision=FSWorkspaceController::instance().revision(); mChoice=current(); mConfig=mDefault ? defaults() : resolve(mChoice,defaults()); mError.clear();
    mTopology=arrangement(displays());
    auto* combo=getChild<LLComboBox>("background_display"); combo->removeall(); combo->add(getString("all_displays"),-1);
    const auto screens=displays();
    for (size_t i=0;i<screens.size();++i) combo->add(getString("display_label")+" "+std::to_string(i+1),static_cast<int>(i));
    if (mDisplay>=static_cast<int>(screens.size())) mDisplay=-1;
    combo->setValue(mDisplay); controls();
}
void FSPanelPreferenceBackgrounds::controls()
{
    mLoading=true;
    const bool enabled=sameSource();
    const bool custom=mDefault || mChoice.custom;
    const bool mismatch=!mConfig.displays.empty() && mConfig.arrangement!=mTopology;
    const auto found=mConfig.displays.find(mDisplay);
    const bool override=found!=mConfig.displays.end() && !mismatch;
    const bool canEdit=enabled && custom && (mDisplay<0 || (override && mConfig.common.mode!=Mode::Span));
    mImage=mDisplay<0 || !override ? mConfig.common : found->second;
    getChild<LLCheckBoxCtrl>("background_inherit")->setVisible(!mDefault);
    getChild<LLCheckBoxCtrl>("background_inherit")->set(!mChoice.custom);
    getChild<LLCheckBoxCtrl>("background_inherit")->setEnabled(enabled);
    getChild<LLCheckBoxCtrl>("background_override")->setVisible(mDisplay>=0);
    getChild<LLCheckBoxCtrl>("background_override")->set(override);
    getChild<LLCheckBoxCtrl>("background_override")->setEnabled(enabled && custom && !mismatch && mConfig.common.mode!=Mode::Span);
    getChild<LLButton>("background_reassign")->setVisible(mismatch);
    getChild<LLButton>("background_reassign")->setEnabled(enabled && custom);
    getChild<LLComboBox>("background_source")->setValue(static_cast<int>(mImage.source));
    getChild<LLComboBox>("background_mode")->setValue(static_cast<int>(mImage.mode));
    getChild<LLColorSwatchCtrl>("background_color")->set(LLColor4(mImage.color[0],mImage.color[1],mImage.color[2],1.f));
    getChild<LLSliderCtrl>("background_brightness")->setValue(ll_round(mImage.brightness*100.f));
    getChild<LLTextureCtrl>("background_texture")->setImageAssetID(mImage.source==Source::Texture ? LLUUID(mImage.asset) : LLUUID::null);
    getChild<LLLineEditor>("background_uuid")->setValue(mImage.source==Source::Texture ? mImage.asset : "");
    for (const char* name:{"background_source","background_color"}) getChild<LLView>(name)->setEnabled(canEdit);
    for (const char* name:{"background_mode","background_brightness"}) getChild<LLView>(name)->setEnabled(canEdit && mImage.source!=Source::Color);
    for (const char* name:{"background_texture","background_uuid"})
    { getChild<LLView>(name)->setVisible(mImage.source==Source::Texture); getChild<LLView>(name)->setEnabled(canEdit); }
    getChild<LLButton>("background_file")->setEnabled(canEdit && !importing());
    getChild<LLButton>("background_file")->setVisible(mImage.source==Source::Local);
    getChild<LLButton>("background_reset")->setEnabled(enabled);
    getChild<BackgroundDiagram>("background_preview")->config=mConfig;
    getChild<BackgroundDiagram>("background_preview")->selected=mDisplay;
    if (isInVisibleChain() && enabled) previewDemand(&mConfig);
    mLoading=false;
}
void FSPanelPreferenceBackgrounds::changed()
{
    if (mLoading || !sameSource()) return;
    ++mEdit; mError.clear();
    const auto source=static_cast<Source>(getChild<LLComboBox>("background_source")->getValue().asInteger());
    if (source!=mImage.source) { mImage.source=source; mImage.asset.clear(); mImage.grid.clear(); mImage.width=mImage.height=0; }
    mImage.mode=static_cast<Mode>(getChild<LLComboBox>("background_mode")->getValue().asInteger());
    if (mDisplay>=0 && mImage.mode==Mode::Span) { mImage.mode=Mode::Fill; getChild<LLComboBox>("background_mode")->setValue(static_cast<int>(Mode::Fill)); }
    const auto color=getChild<LLColorSwatchCtrl>("background_color")->get();
    mImage.color={color.mV[0],color.mV[1],color.mV[2]};
    mImage.brightness=static_cast<F32>(getChild<LLSliderCtrl>("background_brightness")->getValue().asReal()*.01);
    if (source==Source::Texture)
    { mImage.asset=getChild<LLTextureCtrl>("background_texture")->getImageAssetID().asString(); mImage.grid=LLGridManager::instance().getGrid(); }
    if (source==Source::Color) { mImage.asset.clear(); mImage.grid.clear(); }
    const bool incomplete=(source==Source::Local && mImage.asset.empty()) || (source==Source::Texture && LLUUID(mImage.asset).isNull());
    if (!incomplete)
    {
        if (mDisplay<0) mConfig.common=mImage; else { mConfig.arrangement=mTopology; mConfig.displays[mDisplay]=mImage; }
        save(); controls();
    }
    else
    {
        // Keep an incomplete picker choice local; never persist null UUIDs or file paths.
        for (const char* name:{"background_texture","background_uuid"}) getChild<LLView>(name)->setVisible(source==Source::Texture);
        getChild<LLButton>("background_file")->setVisible(source==Source::Local);
        getChild<LLView>("background_mode")->setEnabled(true); getChild<LLView>("background_brightness")->setEnabled(true);
        mError=source==Source::Local ? getString("choose_local") : getString("choose_texture");
    }
}
void FSPanelPreferenceBackgrounds::save()
{
    if (!sameSource()) return;
    if (!mDefault && mChoice.custom) mChoice.config=mConfig;
    if (!FSWorkspaceController::instance().editBackground(mChoice,mDefault ? &mConfig : nullptr)) mError=getString("save_failed");
    mRevision=FSWorkspaceController::instance().revision();
}
void FSPanelPreferenceBackgrounds::chooseFile()
{
    if (!sameSource() || importing()) return;
    const auto handle=getDerivedHandle<FSPanelPreferenceBackgrounds>(); const auto edit=mEdit, revision=mRevision;
    LLFilePickerReplyThread::startPicker([handle,edit,revision](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter)
    {
        auto* self=handle.get();
        if (!self || files.empty() || !self->sameSource() || self->mEdit!=edit || FSWorkspaceController::instance().revision()!=revision || !LLFloaterReg::instanceVisible("preferences")) return;
        self->mError=self->getString("importing");
        if (!importImage(files.front(),[handle,edit,revision](Image image,std::string error)
        {
            auto* p=handle.get();
            if (!p || !p->sameSource() || p->mEdit!=edit || FSWorkspaceController::instance().revision()!=revision || !LLFloaterReg::instanceVisible("preferences")) return false;
            if (image.asset.empty()) { p->mError=error; return false; }
            image.mode=p->mImage.mode; image.color=p->mImage.color; image.brightness=p->mImage.brightness;
            if (p->mDisplay<0) p->mConfig.common=image; else { p->mConfig.arrangement=p->mTopology; p->mConfig.displays[p->mDisplay]=image; }
            p->mError.clear(); p->save(); p->controls(); return true;
        })) self->mError=self->getString("import_busy");
    },LLFilePicker::FFLOAD_IMAGE,false);
}
void FSPanelPreferenceBackgrounds::draw()
{
    if (mTimer.getElapsedTimeF32()>=.25f)
    {
        mTimer.reset(); auto& controller=FSWorkspaceController::instance();
        controller.beginPreferencesSession();
        if (!sameSource() || mRevision!=controller.revision() || mTopology!=arrangement(displays())) load();
        auto status=mError;
        if (status.empty() && !mConfig.displays.empty() && mConfig.arrangement!=mTopology) status=getString("displays_changed");
        if (status.empty()) status=imageStatus(mImage);
        getChild<LLTextBox>("background_status")->setText(status);
        const bool editable=mDisplay<0 || (mConfig.arrangement==mTopology && mConfig.displays.count(mDisplay) && mConfig.common.mode!=Mode::Span);
        getChild<LLButton>("background_file")->setEnabled(sameSource() && (mDefault || mChoice.custom) && editable && !importing());
    }
    LLPanel::draw();
}
void FSPanelPreferenceBackgrounds::onVisibilityChange(bool visible)
{
    LLPanel::onVisibilityChange(visible);
    if (!findChild<BackgroundDiagram>("background_preview")) return;
    if (visible) load();
    else { ++mEdit; previewDemand(nullptr); }
}
