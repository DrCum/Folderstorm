/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fschatmedia.h"
#include "fschatmediacodec.h"
#include "fschatmediaresolver.h"
#include "fschathistory.h"
#include "llagent.h"
#include "llapp.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "llfloaterreg.h"
#include "llfocusmgr.h"
#include "llimage.h"
#include "llimview.h"
#include "lllocalcliprect.h"
#include "llmutelist.h"
#include "llpanel.h"
#include "llproxy.h"
#include "llscrollcontainer.h"
#include "llstartup.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#include "llviewertexture.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llweb.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include <chrono>
#include <future>
#include <mutex>
#include <cstring>
#if LL_WINDOWS
#include "llwin32headers.h"
#endif

namespace
{
using namespace fs_chat_media;
using Clock = std::chrono::steady_clock;
struct Preferences
{
    int mode = 1, size = 320;
    bool animate = false;
    std::string sites = DefaultSites;
};
Preferences savedPreferences()
{
    return {std::clamp(gSavedSettings.getS32("FSChatMediaMode"), 0, 3),
        std::clamp(gSavedSettings.getS32("FSChatMediaSize"), 160, 480),
        gSavedSettings.getBOOL("FSChatMediaAutoplay"), gSavedSettings.getString("FSChatMediaSites")};
}
struct Source
{
    LLUUID account, login, sender, conversation;
    std::string grid;
    bool nearby = false, group = false;
    bool valid() const
    {
        if (LLApp::isExiting() || gDisconnected || LLStartUp::getStartupState() != STATE_STARTED ||
            account != gAgent.getID() || login != gAgent.getSessionID() || grid != LLGridManager::getInstance()->getGrid() ||
            (LLProxy::instanceExists() && LLProxy::getInstance()->isHTTPProxyEnabled())) return false;
        if (sender.notNull() && LLMuteList::getInstance()->isMuted(sender, LLMute::flagTextChat)) return false;
        if (nearby)
            return !gRlvHandler.hasBehaviour(RLV_BHVR_RECVCHAT) && !gRlvHandler.hasBehaviour(RLV_BHVR_RECVEMOTE) &&
                !gRlvHandler.isException(RLV_BHVR_RECVCHATFROM, sender) && !gRlvHandler.isException(RLV_BHVR_RECVEMOTEFROM, sender) &&
                !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES) && !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC);
        return RlvActions::canReceiveIM(sender) && (!group || RlvActions::canReceiveIM(conversation));
    }
    std::string key(const std::string& url) const
    { return grid + '\n' + account.asString() + login.asString() + conversation.asString() + sender.asString() + (nearby ? "n" : "i") + url; }
};
struct Entry
{
    Source source;
    std::string url, token;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::future<std::shared_ptr<Image>> job;
    std::shared_ptr<Image> image;
    bool finished = false;
    Clock::time_point lastSeen = Clock::now();
    // Weak subscribers: false maintains offscreen views; true discards all.
    // Returning false removes dead or rebound cards from the subscriber list.
    std::vector<std::function<bool(bool)>> releaseViews;
    void discard()
    {
        *cancelled = true; image.reset();
        for (const auto& release : releaseViews) release(true);
        releaseViews.clear();
    }
};
std::mutex decodeMutex;
std::shared_ptr<Image> load(const std::string& address, const std::string& sites, const std::string& caFile, const std::atomic<bool>& cancelled)
{
    const auto response = resolveMedia(address, sites, [&](const std::string& url, std::size_t limit)
    { return fetch(url, caFile, limit, cancelled); });
    if (cancelled || response.status != 200) return {};
    // Never decode on the UI thread; serialize decoder working memory.
    std::lock_guard<std::mutex> lock(decodeMutex);
    return cancelled ? std::shared_ptr<Image>{} : decode(response.body);
}
class Service
{
public:
    Preferences prefs = savedPreferences();
    std::map<std::string, std::shared_ptr<Entry>> entries;
    bool registered = false;
    U32 animationFrame = ~0u;
    unsigned animations = 0;
    U32 layoutRevision = 0;
    bool animateInline()
    {
        const auto frame = LLFrameTimer::getFrameCount();
        if (frame != animationFrame) { animationFrame = frame; animations = 0; }
        return animations++ < 3;
    }
    static void idle(void* opaque) { static_cast<Service*>(opaque)->poll(); }
    void start()
    { if (!registered) { gIdleCallbacks.addFunction(idle, this); registered = true; } }
    void clear()
    {
        ++layoutRevision;
        for (auto& pair : entries) pair.second->discard();
        // Keep running futures here until completion; destroying a future on
        // the UI thread must not turn Clear/Apply into a blocking network wait.
    }
    std::size_t memory() const
    { std::size_t used = 0; for (const auto& pair : entries) if (pair.second->image) used += pair.second->image->bytes(); return used; }
    void evict(std::size_t needed, bool admission, const Entry* except = nullptr)
    {
        while ((admission && entries.size() >= MaxEntries) || needed > MaxDecodedBytes - memory())
        {
            auto oldest = entries.end();
            for (auto at = entries.begin(); at != entries.end(); ++at)
                if (at->second.get() != except && at->second->finished && Clock::now() - at->second->lastSeen > std::chrono::seconds(2) &&
                    (oldest == entries.end() || at->second->lastSeen < oldest->second->lastSeen)) oldest = at;
            if (oldest == entries.end()) return;
            oldest->second->discard(); entries.erase(oldest); ++layoutRevision;
        }
    }
    void poll()
    {
        std::size_t used = 0;
        for (const auto& pair : entries) if (pair.second->image) used += pair.second->image->bytes();
        for (auto iter = entries.begin(); iter != entries.end();)
        {
            auto& entry = *iter->second;
            entry.releaseViews.erase(std::remove_if(entry.releaseViews.begin(), entry.releaseViews.end(),
                [](const auto& maintain) { return !maintain(false); }), entry.releaseViews.end());
            if (!prefs.mode || !entry.source.valid() || (!entry.finished && iter->second.use_count() == 1))
            { if (entry.image) used -= entry.image->bytes(); if (!*entry.cancelled) ++layoutRevision; entry.discard(); }
            if (entry.job.valid() && entry.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                std::shared_ptr<Image> image;
                try { image = entry.job.get(); } catch (...) { }
                entry.finished = true;
                if (image && image->bytes() > MaxDecodedBytes - used) { evict(image->bytes(), false, &entry); used = memory(); }
                if (image && !*entry.cancelled && prefs.mode && entry.source.valid() && image->bytes() <= MaxDecodedBytes - used)
                { used += image->bytes(); entry.image = std::move(image); }
            }
            if ((!entry.job.valid() && *entry.cancelled) || (iter->second.use_count() == 1 && entry.finished)) iter = entries.erase(iter);
            else ++iter;
        }
    }
    std::shared_ptr<Entry> request(const Source& source, const std::string& address)
    {
        start(); poll();
        if (!prefs.mode || !source.valid()) return {};
        const auto key = source.key(address); const auto found = entries.find(key);
        if (found != entries.end()) return *found->second->cancelled ? std::shared_ptr<Entry>{} : found->second;
        unsigned jobs = 0; for (const auto& pair : entries) if (pair.second->job.valid()) ++jobs;
        if (jobs >= MaxJobs) return {};
        evict(0, true);
        if (entries.size() >= MaxEntries) return {};
        auto entry = std::make_shared<Entry>(); entry->source = source; entry->url = address; entry->token = LLUUID::generateNewID().asString();
        const auto cancelled = entry->cancelled; const auto sites = prefs.sites; const auto caFile = gDirUtilp->getCAFile();
        try { entry->job = std::async(std::launch::async, [address, sites, caFile, cancelled] { return load(address, sites, caFile, *cancelled); }); }
        catch (...) { entry->finished = true; }
        entries[key] = entry; return entry;
    }
};
Service* activeService = nullptr;
Service& service() { if (!activeService) activeService = new Service; return *activeService; }
std::string tr(const char* name) { return LLTrans::getString(name); }
bool automaticMotion()
{
#if LL_WINDOWS
    static auto checked = Clock::time_point{}; static BOOL enabled = TRUE;
    if (Clock::now() - checked > std::chrono::seconds(5))
    { SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0); checked = Clock::now(); }
    return enabled != FALSE;
#else
    return true; // Autoplay is opt-in; explicit per-card Pause is always honored.
#endif
}

class ImageCard final : public LLPanel
{
public:
    ImageCard(const Source& source, std::string address, bool large = false) : LLPanel(Params()), mSource(source), mUrl(std::move(address)), mLarge(large)
    {
        setMouseOpaque(true); setBackgroundVisible(false);
        LLButton::Params p; p.name = "image_preview"; p.rect = LLRect(0, 24, 220, 0); p.label = tr("FSChatMediaLoad");
        p.follows.flags = FOLLOWS_LEFT | FOLLOWS_BOTTOM; p.tool_tip = tr("FSChatMediaPrivacy");
        mAction = LLUICtrlFactory::create<LLButton>(p); addChild(mAction);
        mAction->setCommitCallback([this](LLUICtrl*, const LLSD&) { activate(); });
        p.name = "animation"; p.rect = LLRect(226, 24, 310, 0); p.label = "GIF";
        mPlay = LLUICtrlFactory::create<LLButton>(p); addChild(mPlay); mPlay->setVisible(false);
        mPlay->setToolTip(tr("FSChatMediaAnimationTip"));
        mPlay->setCommitCallback([this](LLUICtrl*, const LLSD&) { mPlaying = !playing(); mPlaybackSet = true; mEpoch = Clock::now(); });
        setToolTip(parse(mUrl)->host + "\n" + tr("FSChatMediaPrivacy"));
    }
    void bind(std::shared_ptr<Entry> entry) { attach(std::move(entry)); mRequested = true; mPlaying = mLarge; mPlaybackSet = mLarge; }
    void fitImage() { mZoom = 1.f; mPanX = mPanY = 0; }
    S32 desiredHeight() const
    {
        const auto& prefs = service().prefs; if (!prefs.mode) return 0;
        const auto url = parse(mUrl);
        const bool automatic = prefs.mode == 3 || (prefs.mode == 2 && url && selectedSite(*url, prefs.sites));
        return mRequested || (mEntry && !*mEntry->cancelled) || automatic ? 206 : 28;
    }
    void reshape(S32 width, S32 height, bool calledFromParent = true) override
    {
        LLPanel::reshape(width, height, calledFromParent);
        mAction->reshape((std::max)(40, width < 180 ? width : width - 90), 24); mPlay->setOrigin((std::max)(0, width - 84), 0);
    }
    void draw() override
    {
        auto& state = service();
        if (!state.prefs.mode || !mSource.valid() || (mEntry && *mEntry->cancelled))
        { mEntry.reset(); mTexture = nullptr; mFrame = -1; mRequested = false; }
        const auto url = parse(mUrl);
        const bool automatic = state.prefs.mode == 3 || (state.prefs.mode == 2 && url && selectedSite(*url, state.prefs.sites));
        if (!mEntry && visibleInChat() && (mRequested || automatic) && state.prefs.mode && mSource.valid()) attach(state.request(mSource, mUrl));
        if (mEntry && visibleInChat()) mEntry->lastSeen = Clock::now();
        const auto image = mEntry ? mEntry->image : std::shared_ptr<Image>{};
        mAction->setEnabled(state.prefs.mode && mSource.valid());
        const auto label = image ? tr(mLarge ? "FSChatMediaFit" : "FSChatMediaOpen") : mEntry && !mEntry->finished ? tr("FSChatMediaLoading") :
            mEntry && mEntry->finished ? tr("FSChatMediaRetry") : mRequested ? tr("FSChatMediaLoading") : tr("FSChatMediaLoad");
        if (mLabel != label) { mLabel = label; mAction->setLabel(LLStringExplicit(label)); }
        mPlay->setVisible(image && image->frames.size() > 1 && getRect().getWidth() >= 180);
        mPlay->setLabel(LLStringExplicit(playing() ? tr("FSChatMediaPause") : image && image->gif ? "GIF ▶" : tr("FSChatMediaPlay")));
        const auto bg = LLUIColorTable::instance().getColor("TextBgReadOnlyColor").get();
        gl_rect_2d(0, getRect().getHeight(), getRect().getWidth(), 28, bg);
        if (image && visibleInChat())
        {
            int frame = 0;
            if (playing() && (mLarge || state.animateInline()) && image->frames.size() > 1)
            {
                unsigned duration = 0; for (const auto& item : image->frames) duration += item.delay;
                auto elapsed = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - mEpoch).count()) % duration;
                while (frame + 1 < static_cast<int>(image->frames.size()) && elapsed >= image->frames[frame].delay) { elapsed -= image->frames[frame].delay; ++frame; }
            }
            if (mFrame != frame)
            {
                const auto texWidth = textureExtent(image->width), texHeight = textureExtent(image->height);
                LLPointer<LLImageRaw> raw = new LLImageRaw(static_cast<U16>(texWidth), static_cast<U16>(texHeight), 4);
                std::memset(raw->getData(), 0, std::size_t(texWidth) * texHeight * 4);
                for (int row = 0; row < image->height; ++row)
                    std::memcpy(raw->getData() + std::size_t(row) * texWidth * 4, image->frames[frame].rgba.data() + std::size_t(row) * image->width * 4, std::size_t(image->width) * 4);
                if (!mTexture) mTexture = LLViewerTextureManager::getLocalTexture(raw.get(), false);
                else mTexture->setSubImage(raw.get(), 0, 0, texWidth, texHeight);
                mUV = LLRectf(0.f, static_cast<F32>(image->height) / static_cast<F32>(texHeight), static_cast<F32>(image->width) / static_cast<F32>(texWidth), 0.f);
                mFrame = frame;
            }
            const auto size = fit(image->width, image->height, getRect().getWidth(), (std::max)(1, getRect().getHeight() - 30));
            const auto width = static_cast<S32>(size.width * mZoom), height = static_cast<S32>(size.height * mZoom);
            const auto x = (getRect().getWidth() - width) / 2 + mPanX;
            const auto y = 30 + (getRect().getHeight() - 30 - height) / 2 + mPanY;
            LLLocalClipRect clip(LLRect(0, getRect().getHeight(), getRect().getWidth(), 28));
            if (mTexture) gl_draw_scaled_image(x, y, width, height, mTexture, LLColor4::white, mUV);
        }
        LLPanel::draw();
    }
    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (y <= 28) return LLPanel::handleMouseDown(x, y, mask);
        if (!mLarge) { activate(); return true; }
        mDragX = x; mDragY = y; gFocusMgr.setMouseCapture(this); return true;
    }
    bool handleMouseUp(S32 x, S32 y, MASK mask) override
    { if (hasMouseCapture()) { gFocusMgr.setMouseCapture(nullptr); return true; } return LLPanel::handleMouseUp(x, y, mask); }
    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        if (hasMouseCapture()) { mPanX += x - mDragX; mPanY += y - mDragY; mDragX = x; mDragY = y; return true; }
        return LLPanel::handleHover(x, y, mask);
    }
    bool handleScrollWheel(S32 x, S32 y, S32 clicks) override
    { if (!mLarge) return false; mZoom = std::clamp(mZoom * (clicks < 0 ? 1.2f : 1.f / 1.2f), 1.f, 8.f); return true; }
private:
    void attach(std::shared_ptr<Entry> entry)
    {
        mEntry = std::move(entry); if (!mEntry) return;
        const auto handle = getHandle(); const auto* identity = mEntry.get();
        mEntry->releaseViews.emplace_back([handle, identity](bool discard)
        {
            if (auto* card = dynamic_cast<ImageCard*>(handle.get()); card && card->mEntry.get() == identity)
            {
                if (discard || !card->visibleInChat())
                {
                    card->mTexture = nullptr; card->mFrame = -1;
                    if (discard) card->mRequested = false;
                }
                return true;
            }
            return false;
        });
    }
    bool playing() const { return mPlaybackSet ? mPlaying : !mLarge && service().prefs.animate && automaticMotion(); }
    bool visibleInChat() const
    {
        if (!isInVisibleChain()) return false;
        auto rect = calcScreenRect();
        for (auto* parent = getParent(); parent; parent = parent->getParent())
            if (dynamic_cast<LLScrollContainer*>(parent)) rect.intersectWith(parent->calcScreenRect());
        return !rect.isEmpty();
    }
    void activate()
    {
        if (!mSource.valid() || !service().prefs.mode) return;
        if (mEntry && mEntry->image)
        {
            if (mLarge) { fitImage(); return; }
            if (LLFloaterReg::findInstance("chat_image_preview", mEntry->token) || LLFloaterReg::getFloaterList("chat_image_preview").size() < 4)
                LLFloaterReg::showInstance("chat_image_preview", mEntry->token);
        }
        else if (mEntry && !mEntry->finished) return;
        else
        {
            if (mEntry) *mEntry->cancelled = true;
            mEntry.reset(); mTexture = nullptr; mFrame = -1; mRequested = true;
            for (auto* parent = getParent(); parent; parent = parent->getParent())
                if (auto* history = dynamic_cast<FSChatHistory*>(parent)) { FSChatMedia::resize(history); break; }
        }
    }
    Source mSource; std::string mUrl, mLabel; bool mLarge = false, mRequested = false, mPlaying = false, mPlaybackSet = false;
    std::shared_ptr<Entry> mEntry;
    LLPointer<LLViewerTexture> mTexture;
    LLRectf mUV;
    LLButton* mAction = nullptr; LLButton* mPlay = nullptr;
    int mFrame = -1; float mZoom = 1.f; S32 mPanX = 0, mPanY = 0, mDragX = 0, mDragY = 0;
    Clock::time_point mEpoch = Clock::now();
};
}
void FSChatMedia::append(FSChatHistory* history, const LLChat& chat, bool nearby)
{
    auto& state = service(); state.start();
    if (!state.prefs.mode || chat.mMuted || chat.mNotifId.notNull() || chat.mRlvNamesFiltered || chat.mRlvLocFiltered || chat.mFromID.isNull()) return;
    const bool im = !nearby || chat.mChatType == CHAT_TYPE_IM || chat.mChatType == CHAT_TYPE_IM_GROUP;
    // Old Nearby IM logs may lack a conversation ID; do not invent ownership.
    if (im && chat.mSessionID.isNull()) return;
    const auto* session = im ? LLIMModel::getInstance()->findIMSession(chat.mSessionID) : nullptr;
    const bool group = session ? session->isGroupSessionType() : chat.mChatType == CHAT_TYPE_IM_GROUP;
    Source source{gAgent.getID(), gAgent.getSessionID(), chat.mFromID, chat.mSessionID, LLGridManager::getInstance()->getGrid(), !im, group};
    if (!source.valid()) return;
    for (const auto& url : discover(chat.mText, state.prefs.sites))
    {
        auto* card = new ImageCard(source, url);
        card->reshape((std::min)(state.prefs.size, (std::max)(80, history->getRect().getWidth() - 32)), card->desiredHeight());
        LLInlineViewSegment::Params p; p.view = card; p.left_pad = 4; p.right_pad = 4; p.top_pad = 4; p.bottom_pad = 4;
        history->appendWidget(p, "\n", false);
    }
}
void FSChatMedia::resize(FSChatHistory* history)
{
    if (!history->getDocumentView()) return;
    const auto width = (std::min)(service().prefs.size, (std::max)(80, history->getRect().getWidth() - 32));
    bool changed = false;
    for (auto* child : *history->getDocumentView()->getChildList())
        if (auto* card = dynamic_cast<ImageCard*>(child); card && (card->getRect().getWidth() != width || card->getRect().getHeight() != card->desiredHeight()))
        { const auto height = card->desiredHeight(); card->setVisible(height != 0); card->reshape(width, height); changed = true; }
    if (changed) history->needsReflow();
}
int FSChatMedia::layoutKey()
{ const auto& state = service(); return static_cast<int>((state.layoutRevision << 12) | static_cast<U32>(state.prefs.size * 4 + state.prefs.mode)); }
void FSChatMedia::applyPreferences()
{
    auto& state = service(); const auto next = savedPreferences();
    if (next.mode != state.prefs.mode || next.sites != state.prefs.sites) state.clear();
    state.prefs = next;
}
void FSChatMedia::preferencesOpening() { (void)service(); } // Capture committed values before controls edit their draft.
void FSChatMedia::clear() { service().clear(); }
void FSChatMedia::shutdown()
{
    if (!activeService) return;
    auto& state = *activeService; state.clear();
    if (state.registered) { gIdleCallbacks.deleteFunction(Service::idle, &state); state.registered = false; }
    for (const auto& pair : state.entries) if (pair.second->job.valid())
    { try { pair.second->job.get(); } catch (...) { } }
    state.entries.clear();
}
void FSFloaterChatMedia::onOpen(const LLSD& key)
{
    std::shared_ptr<Entry> entry;
    for (const auto& pair : service().entries) if (pair.second->token == key.asString()) entry = pair.second;
    auto* area = getChild<LLPanel>("image_area"); area->deleteAllChildren();
    if (!entry || !entry->source.valid() || !service().prefs.mode) { closeFloater(); return; }
    auto* card = new ImageCard(entry->source, entry->url, true); card->setFollowsAll();
    card->reshape(area->getRect().getWidth(), area->getRect().getHeight()); card->bind(entry); area->addChild(card);
    getChild<LLButton>("copy")->setCommitCallback([entry](LLUICtrl*, const LLSD&)
    { if (entry->source.valid()) gViewerWindow->getWindow()->copyTextToClipboard(utf8str_to_wstring(entry->url)); });
    getChild<LLButton>("original")->setCommitCallback([entry](LLUICtrl*, const LLSD&)
    { if (entry->source.valid()) LLWeb::loadURLExternal(entry->url); });
}
