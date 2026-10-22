/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fschatmedia.h"
#include "fschatmediacodec.h"
#include "fschatanimation.h"
#include "fsgiflibrary.h"
#include "fsfloatergifboard.h"
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
#include "llmenugl.h"
#include "llviewermenu.h"
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
#include <condition_variable>
#include <queue>
#include <thread>
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
    bool hideLinks = false;
    FSChatMedia::BoardOptions board;
};
Preferences savedPreferences()
{
    return {std::clamp(gSavedSettings.getS32("FSChatMediaMode"), 0, 3),
        std::clamp(gSavedSettings.getS32("FSChatMediaSize"), 160, 480),
        gSavedSettings.getBOOL("FSChatMediaAutoplay"), gSavedSettings.getString("FSChatMediaSites"), gSavedSettings.getBOOL("FSChatMediaHideLinks"),
        {std::clamp(gSavedSettings.getS32("FSGifBoardPlayback"),0,2), std::clamp(gSavedSettings.getS32("FSGifBoardVisiblePercent"),1,100), std::clamp(gSavedSettings.getS32("FSGifBoardTileSize"),100,240)}};
}
struct Source
{
    LLUUID account, login, sender, conversation;
    std::string grid;
    bool nearby = false, group = false;
    bool personal = false;
    int dimension = 1024;
    bool valid() const
    {
        if (LLApp::isExiting() || gDisconnected || LLStartUp::getStartupState() != STATE_STARTED ||
            account != gAgent.getID() || login != gAgent.getSessionID() || grid != LLGridManager::getInstance()->getGrid() ||
            (LLProxy::instanceExists() && LLProxy::getInstance()->isHTTPProxyEnabled())) return false;
        if (sender.notNull() && LLMuteList::getInstance()->isMuted(sender, LLMute::flagTextChat)) return false;
        if (personal) return true;
        if (nearby)
            return !gRlvHandler.hasBehaviour(RLV_BHVR_RECVCHAT) && !gRlvHandler.hasBehaviour(RLV_BHVR_RECVEMOTE) &&
                !gRlvHandler.isException(RLV_BHVR_RECVCHATFROM, sender) && !gRlvHandler.isException(RLV_BHVR_RECVEMOTEFROM, sender) &&
                !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES) && !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC);
        return RlvActions::canReceiveIM(sender) && (!group || RlvActions::canReceiveIM(conversation));
    }
    std::string key(const std::string& url) const
    { return grid + '\n' + account.asString() + login.asString() + conversation.asString() + sender.asString() + (personal ? "p" + std::to_string(dimension) : nearby ? "n" : "i") + url; }
};
struct Entry
{
    struct AnimationResult { std::shared_ptr<AnimationDecoder> decoder; std::shared_ptr<const Frame> frame; unsigned loop = 0; bool stopped = false; };
    Source source;
    std::string url, token;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::future<std::shared_ptr<Image>> job;
    std::shared_ptr<Image> image;
    std::future<AnimationResult> animationJob;
    std::shared_ptr<AnimationDecoder> decoder;
    std::shared_ptr<const Frame> animationFrame;
    unsigned animationLoop = 0;
    int animationSerial = 0;
    bool animationStopped = false;
    Clock::time_point animationDemand{}, animationDue{};
    bool finished = false;
    Clock::time_point lastSeen = Clock::now();
    // Weak subscribers: false maintains offscreen views; true discards all.
    // Returning false removes dead or rebound cards from the subscriber list.
    std::vector<std::function<bool(bool)>> releaseViews;
    void discard()
    {
        *cancelled = true; image.reset(); decoder.reset(); animationFrame.reset();
        for (const auto& release : releaseViews) release(true);
        releaseViews.clear();
    }
};
std::mutex decodeMutex;
std::shared_ptr<Image> load(const std::string& address, const std::string& sites, const std::string& caFile, const std::atomic<bool>& cancelled, int dimension)
{
    const auto response = resolveMedia(address, sites, [&](const std::string& url, std::size_t limit)
    { return fetch(url, caFile, limit, cancelled); });
    if (cancelled || response.status != 200) return {};
    // Never decode on the UI thread; serialize decoder working memory.
    std::lock_guard<std::mutex> lock(decodeMutex);
    return cancelled ? std::shared_ptr<Image>{} : decode(response.body, dimension);
}
class FrameWorkers
{
public:
    FrameWorkers()
    {
        try { for (int i = 0; i < 2; ++i) threads.emplace_back([this]
        {
            for (;;)
            {
                std::function<void()> task;
                { std::unique_lock<std::mutex> lock(mutex); ready.wait(lock, [this] { return stopping || !tasks.empty(); });
                  if (stopping && tasks.empty()) return;
                  task = std::move(tasks.front()); tasks.pop(); }
                task();
            }
        }); }
        catch (...) { stop(); throw; }
    }
    ~FrameWorkers() { stop(); }
    std::future<Entry::AnimationResult> submit(std::function<Entry::AnimationResult()> function)
    {
        auto task = std::make_shared<std::packaged_task<Entry::AnimationResult()>>(std::move(function)); auto future = task->get_future();
        { std::lock_guard<std::mutex> lock(mutex); if (stopping || tasks.size() >= 2) throw std::runtime_error("Animation worker capacity"); tasks.emplace([task] { (*task)(); }); }
        ready.notify_one(); return future;
    }
private:
    void stop()
    { { std::lock_guard<std::mutex> lock(mutex); stopping = true; } ready.notify_all(); for (auto& thread : threads) if (thread.joinable()) thread.join(); }
    std::mutex mutex; std::condition_variable ready; bool stopping = false;
    std::queue<std::function<void()>> tasks; std::vector<std::thread> threads;
};
class Service
{
public:
    Preferences prefs = savedPreferences();
    std::map<std::string, std::shared_ptr<Entry>> entries;
    bool registered = false;
    U32 animationFrame = ~0u;
    unsigned animations = 0;
    U32 layoutRevision = 0;
    std::string animationCursor;
    std::unique_ptr<FrameWorkers> frameWorkers;
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
                if (at->second.get() != except && at->second->finished && !at->second->animationJob.valid() && Clock::now() - at->second->lastSeen > std::chrono::seconds(2) &&
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
            if ((!prefs.mode && !entry.source.personal) || !entry.source.valid() || (!entry.finished && iter->second.use_count() == 1))
            { if (entry.image) used -= entry.image->bytes(); if (!*entry.cancelled) ++layoutRevision; entry.discard(); }
            if (entry.job.valid() && entry.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                std::shared_ptr<Image> image;
                try { image = entry.job.get(); } catch (...) { }
                entry.finished = true; ++layoutRevision;
                if (image && image->bytes() > MaxDecodedBytes - used) { evict(image->bytes(), false, &entry); used = memory(); }
                if (image && !*entry.cancelled && (prefs.mode || entry.source.personal) && entry.source.valid() && image->bytes() <= MaxDecodedBytes - used)
                { used += image->bytes(); entry.image = std::move(image); }
            }
            if (entry.animationJob.valid() && entry.animationJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                Entry::AnimationResult result;
                try { result = entry.animationJob.get(); } catch (...) { result.stopped = true; }
                if (!*entry.cancelled)
                {
                    entry.decoder = std::move(result.decoder); entry.animationLoop = result.loop; entry.animationStopped = result.stopped;
                    if (result.frame)
                    {
                        entry.animationFrame = std::move(result.frame); ++entry.animationSerial;
                        entry.animationDue = Clock::now() + std::chrono::milliseconds(entry.animationFrame->delay);
                    }
                }
            }
            if (!entry.animationJob.valid() && Clock::now() - entry.animationDemand > std::chrono::seconds(1))
            { entry.decoder.reset(); entry.animationFrame.reset(); entry.animationLoop = 0; }
            entry.releaseViews.erase(std::remove_if(entry.releaseViews.begin(), entry.releaseViews.end(),
                [](const auto& maintain) { return !maintain(false); }), entry.releaseViews.end());
            if (!entry.animationJob.valid() && ((!entry.job.valid() && *entry.cancelled) || (iter->second.use_count() == 1 && entry.finished))) iter = entries.erase(iter);
            else ++iter;
        }
        unsigned active = 0; for (const auto& pair : entries) if (pair.second->animationJob.valid()) ++active;
        // Rotate admission so more than two visible animations all get turns.
        auto at = entries.upper_bound(animationCursor);
        for (std::size_t visited = 0; visited < entries.size() && active < 2; ++visited)
        {
            if (at == entries.end()) at = entries.begin();
            auto& entry = *at->second; const auto key = at->first; ++at;
            if (*entry.cancelled || !entry.image || !entry.image->animation || entry.animationStopped || entry.animationJob.valid() ||
                Clock::now() - entry.animationDemand > std::chrono::milliseconds(250) || Clock::now() < entry.animationDue) continue;
            const auto source = entry.image->animation; const auto cancelled = entry.cancelled; auto decoder = entry.decoder; const auto loop = entry.animationLoop;
            try
            {
                if (!frameWorkers) frameWorkers = std::make_unique<FrameWorkers>();
                entry.animationJob = frameWorkers->submit([source, cancelled, decoder, loop]() mutable
                {
                    Entry::AnimationResult result; result.loop = loop;
                    if (!decoder) decoder = std::make_shared<AnimationDecoder>(source);
                    result.frame = decoder->next(*cancelled);
                    if (!result.frame && decoder->ended() && !*cancelled && (!source->loops || loop + 1 < source->loops))
                    { decoder = std::make_shared<AnimationDecoder>(source); ++result.loop; result.frame = decoder->next(*cancelled); }
                    result.stopped = !result.frame; result.decoder = std::move(decoder); return result;
                });
                ++active; animationCursor = key;
            }
            catch (...) { entry.animationStopped = true; }
        }
    }
    std::shared_ptr<Entry> request(const Source& source, const std::string& address)
    {
        start(); poll();
        if ((!prefs.mode && !source.personal) || !source.valid()) return {};
        const auto key = source.key(address); const auto found = entries.find(key);
        if (found != entries.end()) return *found->second->cancelled ? std::shared_ptr<Entry>{} : found->second;
        unsigned jobs = 0; for (const auto& pair : entries) if (pair.second->job.valid()) ++jobs;
        if (jobs >= MaxJobs) return {};
        evict(0, true);
        if (entries.size() >= MaxEntries) return {};
        auto entry = std::make_shared<Entry>(); entry->source = source; entry->url = address; entry->token = LLUUID::generateNewID().asString();
        const auto cancelled = entry->cancelled; const auto sites = prefs.sites; const auto caFile = gDirUtilp->getCAFile();
        const auto dimension=source.dimension;
        try { entry->job = std::async(std::launch::async, [address, sites, caFile, cancelled, dimension] { return load(address, sites, caFile, *cancelled, dimension); }); }
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
    ImageCard(const Source& source, std::string address, bool large = false, FSChatHistory* history = nullptr) : LLPanel(Params()), mSource(source), mUrl(std::move(address)), mLarge(large)
    {
        setMouseOpaque(true); setBackgroundVisible(false); setTabStop(true);
        if (history) mHistory = history->getHandle();
        LLButton::Params p; p.name = "image_preview"; p.rect = LLRect(0, 24, 220, 0); p.label = tr("FSChatMediaLoad");
        p.follows.flags = FOLLOWS_LEFT | FOLLOWS_BOTTOM; p.tool_tip = tr("FSChatMediaPrivacy");
        mAction = LLUICtrlFactory::create<LLButton>(p); addChild(mAction);
        mAction->setCommitCallback([this](LLUICtrl*, const LLSD&) { activate(); });
        p.name = "animation"; p.rect = LLRect(4, 24, 24, 4); p.label = ""; p.image_overlay = LLUI::getUIImage("Play_Off");
        mPlay = LLUICtrlFactory::create<LLButton>(p); addChild(mPlay); mPlay->setVisible(false);
        mPlay->setToolTip(tr("FSChatMediaAnimationTip"));
        mPlay->setCommitCallback([this](LLUICtrl*, const LLSD&)
        {
            mPlaying = !playing(); mPlaybackSet = true;
            if (mPlaying && mEntry && mEntry->animationStopped && !mEntry->animationJob.valid())
            { mEntry->animationStopped = false; mEntry->decoder.reset(); mEntry->animationLoop = 0; }
        });
        setToolTip(parse(mUrl)->host + "\n" + tr("FSChatMediaPrivacy"));
    }
    ~ImageCard() override
    {
        setLinkHidden(false);
        if (auto* menu = mMenu.get()) menu->die();
    }
    const std::shared_ptr<bool>& linkHidden() const { return mLinkHidden; }
    void board(std::function<void()> select, std::function<void()> insert)
    { mBoard = true; mSelect = std::move(select); mInsert = std::move(insert); }
    void boardPlay(bool play, bool visible) { mBoardPlay = play; mBoardVisible = visible; }
    void preview()
    {
        if (!mSource.valid()) return;
        if (mEntry && mEntry->image)
        { if (LLFloaterReg::findInstance("chat_image_preview", mEntry->token) || LLFloaterReg::getFloaterList("chat_image_preview").size() < 4) LLFloaterReg::showInstance("chat_image_preview", mEntry->token); }
        else mRequested = true;
    }
    void bind(std::shared_ptr<Entry> entry) { attach(std::move(entry)); mRequested = true; mPlaying = mLarge; mPlaybackSet = mLarge; }
    void fitImage() { mZoom = 1.f; mPanX = mPanY = 0; }
    S32 desiredHeight(S32 width = 0) const
    {
        const auto& prefs = service().prefs; if (!prefs.mode) return 0;
        if (mEntry && !*mEntry->cancelled && mEntry->image)
            return (std::max)(24, fit(mEntry->image->width, mEntry->image->height, width ? width : getRect().getWidth(), 200).height);
        return 28;
    }
    void reshape(S32 width, S32 height, bool calledFromParent = true) override
    {
        LLPanel::reshape(width, height, calledFromParent);
        mAction->reshape(mLarge ? (std::min)(width, 110) : (std::max)(24, width), 24);
    }
    void draw() override
    {
        auto& state = service();
        if ((!state.prefs.mode && !mBoard && !mSource.personal) || !mSource.valid() || (mEntry && *mEntry->cancelled))
        { mEntry.reset(); mTexture = nullptr; mTextureReservation.reset(); mFrame = -1; mRequested = false; }
        const auto url = parse(mUrl);
        const bool automatic = mBoard || state.prefs.mode == 3 || (state.prefs.mode == 2 && url && selectedSite(*url, state.prefs.sites));
        if (!mEntry && visibleInChat() && (mRequested || automatic) && (mBoard || state.prefs.mode) && mSource.valid()) attach(state.request(mSource, mUrl));
        if (mEntry && visibleInChat()) mEntry->lastSeen = Clock::now();
        const auto image = mEntry ? mEntry->image : std::shared_ptr<Image>{};
        if (!mBoard && !mLarge && image && getRect().getHeight() != desiredHeight()) ++state.layoutRevision;
        mAction->setEnabled((mBoard || mSource.personal || state.prefs.mode) && mSource.valid());
        mAction->setVisible(!image || mLarge);
        syncLinkVisibility();
        const auto label = image ? tr("FSChatMediaFit") : mEntry && !mEntry->finished ? tr("FSChatMediaLoading") :
            mEntry && mEntry->finished ? tr("FSChatMediaRetry") : mRequested ? tr("FSChatMediaLoading") : tr("FSChatMediaLoad");
        if (mLabel != label) { mLabel = label; mAction->setLabel(LLStringExplicit(label)); }
        mPlay->setVisible(image && image->animation && getRect().getWidth() >= 24 && getRect().getHeight() >= 24);
        mPlay->setImageOverlay(playing() ? "Pause_Off" : "Play_Off");
        mPlay->setToolTip(tr(playing() ? "FSChatMediaPause" : "FSChatMediaPlay") + " · " + tr("FSChatMediaAnimationTip"));
        const auto bg = LLUIColorTable::instance().getColor("TextBgReadOnlyColor").get();
        const S32 footer = mLarge ? 28 : 0;
        gl_rect_2d(0, getRect().getHeight(), getRect().getWidth(), footer, bg);
        if (image && visibleInChat())
        {
            int frame = 0;
            const Frame* pixels = &image->frames.front();
            if (image->animation && playing() && (mLarge || mBoard || state.animateInline()))
            { mEntry->animationDemand = Clock::now(); if (mEntry->animationFrame) { pixels = mEntry->animationFrame.get(); frame = mEntry->animationSerial; } }
            else if (mFrame >= 0) frame = mFrame; // Pause retains the currently uploaded pixels.
            if (mFrame != frame)
            {
                const auto texWidth = textureExtent(image->width), texHeight = textureExtent(image->height);
                if (!mTextureReservation) mTextureReservation = reserveAnimationTexture(std::size_t(texWidth) * texHeight * 4);
                if (!mTextureReservation) { mRenderUnavailable = true; setLinkHidden(false); LLPanel::draw(); return; }
                mRenderUnavailable = false;
                LLPointer<LLImageRaw> raw = new LLImageRaw(static_cast<U16>(texWidth), static_cast<U16>(texHeight), 4);
                std::memset(raw->getData(), 0, std::size_t(texWidth) * texHeight * 4);
                for (int row = 0; row < image->height; ++row)
                    std::memcpy(raw->getData() + std::size_t(row) * texWidth * 4, pixels->rgba.data() + std::size_t(row) * image->width * 4, std::size_t(image->width) * 4);
                if (!mTexture) mTexture = LLViewerTextureManager::getLocalTexture(raw.get(), false);
                else mTexture->setSubImage(raw.get(), 0, 0, texWidth, texHeight);
                mUV = LLRectf(0.f, static_cast<F32>(image->height) / static_cast<F32>(texHeight), static_cast<F32>(image->width) / static_cast<F32>(texWidth), 0.f);
                mFrame = frame;
            }
            const auto size = fit(image->width, image->height, getRect().getWidth(), (std::max)(1, getRect().getHeight() - footer));
            const auto width = static_cast<S32>(size.width * mZoom), height = static_cast<S32>(size.height * mZoom);
            const auto x = (getRect().getWidth() - width) / 2 + mPanX;
            const auto y = footer + (getRect().getHeight() - footer - height) / 2 + mPanY;
            mPlay->setOrigin(std::clamp(x + 4, 0, (std::max)(0, getRect().getWidth() - 20)),
                std::clamp(y + 4, footer, (std::max)(footer, getRect().getHeight() - 20)));
            LLLocalClipRect clip(LLRect(0, getRect().getHeight(), getRect().getWidth(), footer));
            if (mTexture) gl_draw_scaled_image(x, y, width, height, mTexture, LLColor4::white, mUV);
        }
        LLPanel::draw();
    }
    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (childrenHandleMouseDown(x, y, mask)) return true;
        if (mLarge && y <= 28) return true;
        if (!mLarge) { setFocus(true); activate(); return true; }
        mDragX = x; mDragY = y; gFocusMgr.setMouseCapture(this); return true;
    }
    bool handleKeyHere(KEY key, MASK mask) override
    {
        if (mBoard && mask == MASK_NONE && key == ' ') { mPlaying = !playing(); mPlaybackSet = true; return true; }
        if (mBoard && mask == MASK_NONE && key == KEY_RETURN && mInsert) { mInsert(); return true; }
        if (mask == MASK_NONE && (key == KEY_RETURN || key == ' ')) { activate(); return true; }
        return LLPanel::handleKeyHere(key, mask);
    }
    bool handleDoubleClick(S32, S32, MASK) override
    { if (mBoard && mInsert) { mInsert(); return true; } preview(); return true; }
    bool handleRightMouseDown(S32 x, S32 y, MASK) override
    {
        if (!mSource.valid() || !gMenuHolder) return true;
        if (auto* prior = mMenu.get()) prior->die();
        LLContextMenu::Params p; p.name = "chat_image_menu";
        auto* menu = LLUICtrlFactory::create<LLContextMenu>(p); gMenuHolder->addChild(menu); mMenu = menu->getHandle();
        const auto handle = getHandle();
        for (const char* action : {"FSChatMediaCopyLink", "FSChatMediaOriginal", "FSGifSave"})
        {
            if (std::string(action) == "FSGifSave" && (!mEntry || !mEntry->image || (!mEntry->image->animation && !mEntry->image->gif))) continue;
            LLMenuItemCallGL::Params item; item.name = action; item.label = tr(action);
            item.on_click.function([handle, action = std::string(action)](LLUICtrl*, const LLSD&)
            {
                auto* card = dynamic_cast<ImageCard*>(handle.get()); if (!card || !card->mSource.valid()) return;
                if (action == "FSGifSave") FSFloaterGifBoard::saveLink(card->mUrl);
                else if (action == "FSChatMediaCopyLink") gViewerWindow->getWindow()->copyTextToClipboard(utf8str_to_wstring(card->mUrl));
                else LLWeb::loadURLExternal(card->mUrl);
            });
            menu->addChild(LLUICtrlFactory::create<LLMenuItemCallGL>(item));
        }
        S32 sx, sy; localPointToScreen(x, y, &sx, &sy); menu->show(sx, sy, this); return true;
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
                    card->mTexture = nullptr; card->mTextureReservation.reset(); card->mFrame = -1;
                    if (discard) card->mRequested = false;
                }
                card->syncLinkVisibility();
                return true;
            }
            return false;
        });
    }
    void setLinkHidden(bool hidden)
    {
        if (*mLinkHidden == hidden) return;
        *mLinkHidden = hidden;
        if (auto* history = dynamic_cast<FSChatHistory*>(mHistory.get())) history->needsReflow();
    }
    void syncLinkVisibility()
    { setLinkHidden(!mRenderUnavailable && service().prefs.hideLinks && service().prefs.mode && mEntry && !*mEntry->cancelled && mEntry->image && mSource.valid()); }
    bool playing() const { return mBoard ? mBoardVisible && (mPlaybackSet ? mPlaying : mBoardPlay) : mPlaybackSet ? mPlaying : !mLarge && service().prefs.animate && automaticMotion(); }
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
        if (mBoard && mSelect) { mSelect(); return; }
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
            mEntry.reset(); mTexture = nullptr; mTextureReservation.reset(); mFrame = -1; mRequested = true;
            for (auto* parent = getParent(); parent; parent = parent->getParent())
                if (auto* history = dynamic_cast<FSChatHistory*>(parent)) { FSChatMedia::resize(history); break; }
        }
    }
    Source mSource; std::string mUrl, mLabel; bool mLarge = false, mRequested = false, mPlaying = false, mPlaybackSet = false;
    std::shared_ptr<Entry> mEntry;
    LLPointer<LLViewerTexture> mTexture;
    std::shared_ptr<void> mTextureReservation;
    LLRectf mUV;
    LLHandle<LLView> mHistory;
    LLHandle<LLContextMenu> mMenu;
    std::shared_ptr<bool> mLinkHidden = std::make_shared<bool>(false);
    bool mRenderUnavailable = false;
    bool mBoard = false, mBoardPlay = false, mBoardVisible = false;
    std::function<void()> mSelect, mInsert;
    LLButton* mAction = nullptr; LLButton* mPlay = nullptr;
    int mFrame = -1; float mZoom = 1.f; S32 mPanX = 0, mPanY = 0, mDragX = 0, mDragY = 0;
};
}
LLPanel* FSChatMedia::createBoardTile(const std::string& url, std::function<void()> select, std::function<void()> insert)
{
    if (!parse(url)) return nullptr;
    Source source{gAgent.getID(), gAgent.getSessionID(), LLUUID::null, LLUUID::null, LLGridManager::getInstance()->getGrid(), false, false, true};
    source.dimension=320;
    auto* card = new ImageCard(source, url); card->board(std::move(select), std::move(insert)); return card;
}
void FSChatMedia::boardPlayback(LLPanel* tile, bool play, bool visible) { if (auto* card = dynamic_cast<ImageCard*>(tile)) card->boardPlay(play, visible); }
void FSChatMedia::boardPreview(LLPanel* tile) { if (auto* card = dynamic_cast<ImageCard*>(tile)) card->preview(); }
bool FSChatMedia::automaticMotionEnabled() { return automaticMotion(); }
FSChatMedia::BoardOptions FSChatMedia::boardOptions() { return service().prefs.board; }
void FSChatMedia::boardSize(int size) { service().prefs.board.size=std::clamp(size,100,240);gSavedSettings.setS32("FSGifBoardTileSize",service().prefs.board.size); }
void FSChatMedia::append(FSChatHistory* history, const LLChat& chat, bool nearby, S32 bodyStart)
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
    const auto bodyEnd = history->getLength();
    for (const auto& url : discover(chat.mText, state.prefs.sites))
    {
        auto* card = new ImageCard(source, url, false, history);
        history->collapseMediaLink(bodyStart, bodyEnd, url, card->linkHidden());
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
        if (auto* card = dynamic_cast<ImageCard*>(child))
        {
            const auto height = card->desiredHeight(width);
            if (card->getRect().getWidth() != width || card->getRect().getHeight() != height)
            { card->setVisible(height != 0); card->reshape(width, height); changed = true; }
        }
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
    for (const auto& pair : state.entries)
    {
        if (pair.second->job.valid()) { try { pair.second->job.get(); } catch (...) { } }
        if (pair.second->animationJob.valid()) { try { pair.second->animationJob.get(); } catch (...) { } }
    }
    state.entries.clear();
    state.frameWorkers.reset();
}
void FSFloaterChatMedia::onOpen(const LLSD& key)
{
    std::shared_ptr<Entry> entry;
    for (const auto& pair : service().entries) if (pair.second->token == key.asString()) entry = pair.second;
    auto* area = getChild<LLPanel>("image_area"); area->deleteAllChildren();
    if (!entry || !entry->source.valid() || (!service().prefs.mode && !entry->source.personal)) { closeFloater(); return; }
    auto* card = new ImageCard(entry->source, entry->url, true); card->setFollowsAll();
    card->reshape(area->getRect().getWidth(), area->getRect().getHeight()); card->bind(entry); area->addChild(card);
    getChild<LLButton>("copy")->setCommitCallback([entry](LLUICtrl*, const LLSD&)
    { if (entry->source.valid()) gViewerWindow->getWindow()->copyTextToClipboard(utf8str_to_wstring(entry->url)); });
    getChild<LLButton>("original")->setCommitCallback([entry](LLUICtrl*, const LLSD&)
    { if (entry->source.valid()) LLWeb::loadURLExternal(entry->url); });
}
