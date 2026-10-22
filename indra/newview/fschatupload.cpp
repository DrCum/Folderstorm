/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fschatupload.h"
#include "fschatuploadpolicy.h"
#include "fschatmediacodec.h"
#include "fschatmediahttp.h"
#include "fsfloatergifboard.h"
#include "fsgiflibrary.h"
#include "fsdata.h"
#include "fsfloaterim.h"
#include "fsfloaternearbychat.h"
#include "fssessionworker.h"
#include "llagent.h"
#include "llavatarnamecache.h"
#include "llapp.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llchatentry.h"
#include "llclipboardimage.h"
#include "llimagepng.h"
#include "llcheckboxctrl.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llimage.h"
#include "llimview.h"
#include "llspeakers.h"
#include "llproxy.h"
#include "llspinctrl.h"
#include "llstartup.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#include "llviewertexture.h"
#include "llviewerwindow.h"
#include "llviewermenufile.h"
#include "llwindow.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include <future>
#include <chrono>

namespace
{
using namespace fs_chat_upload;
Config savedConfig()
{ return {gSavedSettings.getBOOL("FSChatUploadEnabled"), gSavedSettings.getBOOL("FSChatUploadAutomatic"), gSavedSettings.getString("FSChatUploadHost"), gSavedSettings.getString("FSChatUploadFolder")}; }
std::string tr(const char* key) { return LLTrans::getString(key); }
struct Binding
{
    std::string id = LLUUID::generateNewID().asString();
    LLHandle<LLView> editor, button;
    LLHandle<LLView> gifButton;
    LLUUID conversation;
    unsigned generation = 0;
    S32 rightInset = 0;
    bool layoutReady = false;
    bool dragOver = false;
    std::string error;
    std::function<bool()> available;
    std::function<std::string()> destination;
    LLChatEntry* input() const { return dynamic_cast<LLChatEntry*>(editor.get()); }
    bool valid() const
    {
        const auto* e = input();
        if (!e || !e->getEnabled() || e->getReadOnly() || (available && !available())) return false;
        if (conversation.isNull())
            return !gRlvHandler.hasBehaviour(RLV_BHVR_SENDCHAT) && !gRlvHandler.hasBehaviour(RLV_BHVR_REDIRCHAT);
        const auto* session = LLIMModel::getInstance()->findIMSession(conversation);
        if (!session || !session->mTextIMPossible || !FSData::instance().canSendToIMContact(session->mType, gAgent.getID(), session->mOtherParticipantID)) return false;
        if (session->isGroupSessionType()) return gAgent.isInGroup(conversation) && RlvActions::canSendIM(conversation);
        if (session->isP2PSessionType()) return RlvActions::canSendIM(session->mOtherParticipantID);
        if (!session->mSpeakers) return false;
        LLSpeakerMgr::speaker_list_t speakers;
        session->mSpeakers->getSpeakerList(&speakers, true);
        for (const auto& speaker : speakers)
            if (speaker->mID != gAgent.getID() && !RlvActions::canSendIM(speaker->mID)) return false;
        return true;
    }
    std::string stamp() const
    {
        if (conversation.notNull()) return conversation.asString();
        const auto* e = input(); if (!e) return {};
        auto text = e->getText(); LLStringUtil::trimHead(text);
        const auto prefix = text.empty() || text.front() != '/' ? "" : text.substr(0, text.find_first_of(" \t\n"));
        return "Nearby:" + (destination ? destination() : "0") + ":" + prefix;
    }
    std::string label() const
    {
        if (conversation.notNull())
        { const auto* session = LLIMModel::getInstance()->findIMSession(conversation); return session ? session->mName : tr("FSChatUploadUnavailable"); }
        return tr("FSChatUploadNearby") + (destination ? " · " + destination() : "");
    }
};
// Registration must work before an auto-sized layout panel gets its final width.
// Only the visual button's space reservation waits for a usable layout.
void layoutAttachment(const std::shared_ptr<Binding>& binding)
{
    auto* editor = binding->input(); auto* button = dynamic_cast<LLButton*>(binding->button.get());
    if (!editor || !button || !editor->getParent()) return;
    const auto rect = editor->getRect(); const auto parentWidth = editor->getParent()->getRect().getWidth();
    if (!binding->layoutReady)
    {
        if (rect.getWidth() < 70 || parentWidth < 70) { button->setVisible(false); return; }
        binding->rightInset = (std::max)(0, parentWidth - rect.mRight); binding->layoutReady = true;
    }
    const auto width = parentWidth - binding->rightInset - 60 - rect.mLeft;
    button->setVisible(editor->getVisible() && width >= 40);
    if (auto* gif = dynamic_cast<LLButton*>(binding->gifButton.get())) gif->setVisible(editor->getVisible() && width >= 40);
    if (width < 40) return;
    if (rect.getWidth() != width) editor->reshape(width, rect.getHeight());
    const auto placed = editor->getRect(); button->setOrigin(placed.mRight + 2, placed.mBottom);
    button->reshape(24, (std::min)(24, placed.getHeight()));
    if (auto* gif = dynamic_cast<LLButton*>(binding->gifButton.get()))
    { gif->setOrigin(placed.mRight + 28, placed.mBottom); gif->reshape(32, (std::min)(24, placed.getHeight())); }
}

struct Work
{
    std::shared_ptr<std::vector<unsigned char>> bytes;
    std::shared_ptr<fs_chat_media::Image> image;
    Reply reply;
};
struct Job
{
    std::string id = LLUUID::generateNewID().asString(), stamp, draft, path, type, status;
    LLUUID account, login;
    std::string grid;
    unsigned generation = 0;
    S32 cursor = 0;
    std::shared_ptr<Binding> binding;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    std::future<Work> work;
    std::size_t workBytes = 0; // Encoded bytes retained/reserved by outstanding work.
    std::shared_ptr<std::vector<unsigned char>> bytes;
    std::shared_ptr<fs_chat_media::Image> image;
    Config config;
    State state = State::Preparing;
    Reply reply;
    bool uploadWork = false, checkWork = false, inserted = false, invalidated = false, dib = false;
    bool saveGif = true, libraryHandled = false;
    unsigned checks = 0;
    std::chrono::steady_clock::time_point checkAfter{};
    bool valid() const
    {
        return !invalidated && !LLApp::isExiting() && !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED &&
            account == gAgent.getID() && login == gAgent.getSessionID() && grid == LLGridManager::getInstance()->getGrid() &&
            binding && generation == binding->generation && binding->valid() && stamp == binding->stamp();
    }
};
Work prepare(std::shared_ptr<std::vector<unsigned char>> bytes, const std::string& path, bool dib)
{
    Work work;
    if (!bytes)
    {
        llstat info{};
        if (path.compare(0, 2, "\\\\") == 0 || LLFile::stat(path, &info) || (info.st_mode & S_IFMT) != S_IFREG ||
            info.st_size < 1 || static_cast<std::uint64_t>(info.st_size) > MaxFileBytes) return work;
        LLFILE* file = LLFile::fopen(path, "rb"); if (!file) return work;
        const auto close = [](LLFILE* f) { fclose(f); };
        std::unique_ptr<LLFILE, decltype(close)> owned(file, close);
        // Read an immutable bounded snapshot, not a path reopened at POST time.
        bytes = std::make_shared<std::vector<unsigned char>>(); unsigned char block[16384];
        while (const auto count = fread(block, 1, sizeof(block), file))
        { if (bytes->size() > MaxFileBytes - count) return work; bytes->insert(bytes->end(), block, block + count); }
        if (ferror(file)) return work;
    }
    if (dib)
    {
        ll_clipboard_image::Dib info;
        if (!ll_clipboard_image::inspect(*bytes, info)) return work;
        LLPointer<LLImageRaw> raw = new LLImageRaw(static_cast<U16>(info.width), static_cast<U16>(info.height), 4);
        if (!ll_clipboard_image::unpack(*bytes, raw->getData(), static_cast<std::size_t>(raw->getDataSize()))) return work;
        bytes.reset(); // Release the copied DIB before allocating encoded PNG data.
        LLPointer<LLImagePNG> png = new LLImagePNG;
        if (!png->encode(raw.get(), 0.f) || png->getDataSize() < 1 || static_cast<std::size_t>(png->getDataSize()) > MaxFileBytes) return work;
        bytes = std::make_shared<std::vector<unsigned char>>(png->getData(), png->getData() + png->getDataSize());
    }
    if (bytes->empty() || bytes->size() > MaxFileBytes || mime(*bytes).empty()) return work;
    work.image = fs_chat_media::decode(*bytes, 320, true);
    if (work.image) { work.image->frames.resize(1); work.bytes = std::move(bytes); }
    return work;
}
void saveGifLink(const std::string& url) { FSFloaterGifBoard::saveLink(url); }
struct Controller
{
    Config config = savedConfig();
    std::vector<std::shared_ptr<Binding>> bindings;
    std::vector<std::shared_ptr<Job>> jobs;
    bool running = false;
    static void idle(void* self) { static_cast<Controller*>(self)->poll(); }
    void start() { if (!running) { gIdleCallbacks.addFunction(idle, this); running = true; } }
    std::shared_ptr<Binding> find(LLChatEntry* editor)
    { for (const auto& b : bindings) if (b->input() == editor) return b; return {}; }
    std::shared_ptr<Binding> find(const std::string& id)
    { for (const auto& b : bindings) if (b->id == id) return b; return {}; }
    bool connection() const
    { return !(LLProxy::instanceExists() && LLProxy::getInstance()->isHTTPProxyEnabled()); }
    std::size_t retainedBytes() const
    {
        std::size_t retained = 0;
        for (const auto& job : jobs)
            retained += (std::max)(job->workBytes, job->bytes && !job->dib ? job->bytes->size() : std::size_t{0});
        return retained;
    }
    void cancel(const std::shared_ptr<Job>& job, bool invalid = false)
    {
        job->progress->cancelled = true; job->invalidated |= invalid;
        job->state = State::Cancelled; job->path.clear(); job->bytes.reset(); job->image.reset();
        job->status = tr(job->progress->submitted ? "FSChatUploadMayRemain" : "FSChatUploadCancelled");
    }
    bool insert(const std::shared_ptr<Job>& job, bool explicitAction)
    {
        auto* editor = job->binding->input();
        if (!job->valid() || !editor || job->reply.url.empty() || job->inserted || !config.enabled || !sameDestination(config, job->config)) return false;
        if (!explicitAction && (editor->getText() != job->draft || editor->getCursorPos() != job->cursor || !editor->getSelectionString().empty())) return false;
        const auto beforeText = editor->getText();
        const auto& text = editor->getWText(); const auto link = utf8str_to_wstring(job->reply.url);
        if (wstring_utf8_length(text) + wstring_utf8_length(link) + 2 > editor->getMaxTextLength()) return false;
        const auto cursor = job->binding->conversation.isNull() ? static_cast<S32>(text.size()) : explicitAction ? editor->getCursorPos() : job->cursor;
        editor->deselect(); editor->setCursorPos(cursor);
        const auto before = cursor > 0 && !LLStringOps::isSpace(text[static_cast<std::size_t>(cursor - 1)]) ? " " : "";
        const auto after = cursor < static_cast<S32>(text.size()) && !LLStringOps::isSpace(text[static_cast<std::size_t>(cursor)]) ? " " : "";
        editor->insertText(before + job->reply.url + after); job->inserted = true;
        // Our own insertion advances siblings' anchors; user edits do not.
        for (const auto& sibling : jobs)
            if (sibling != job && sibling->binding == job->binding && sibling->draft == beforeText && !sibling->inserted)
            { sibling->draft = editor->getText(); sibling->cursor = editor->getCursorPos(); }
        return true;
    }
    void poll()
    {
        bool busy = false;
        for (const auto& job : jobs)
        {
            if (!job->valid() || !config.enabled || !sameDestination(config, job->config) || !connection())
            { if (!job->invalidated) cancel(job, true); }
            if (job->work.valid() && job->work.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                Work result; try { result = job->work.get(); } catch (...) { }
                job->workBytes = 0;
                if (job->checkWork)
                {
                    job->checkWork = false;
                    if (!job->progress->cancelled)
                    {
                        if (result.reply.state == State::Complete) { job->state = State::Complete; job->bytes.reset(); }
                        else if (job->checks >= 5) { job->state = State::Uncertain; job->status = tr("FSChatUploadProcessing"); }
                        job->checkAfter = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                    }
                }
                else if (job->uploadWork)
                {
                    job->reply = std::move(result.reply);
                    if (!job->progress->cancelled)
                    {
                        job->state = job->reply.state; job->status = job->reply.error;
                        if (job->state == State::Complete) job->bytes.reset();
                    }
                }
                else if (!job->progress->cancelled)
                {
                    job->bytes = std::move(result.bytes); job->image = std::move(result.image); job->dib = false;
                    if (job->bytes) { job->type = mime(*job->bytes); job->state = config.automatic ? State::Queued : State::Review; }
                    else { job->state = State::Failed; job->status = tr("FSChatUploadInvalidImage"); }
                }
            }
            if (job->work.valid()) busy = true;
        }
        // Flush each conversation in staging order after collecting all completions.
        // Pending work holds back only later attachments in the same draft.
        std::vector<const Binding*> pendingBindings;
        for (const auto& job : jobs)
        {
            if (!job->libraryHandled && job->state == State::Complete && job->valid() && job->type == "image/gif")
            { job->libraryHandled = true; if (job->saveGif) saveGifLink(job->reply.url); }
            const auto* binding = job->binding.get();
            if (std::find(pendingBindings.begin(), pendingBindings.end(), binding) != pendingBindings.end()) continue;
            if (fs_chat_upload::blocksSend(job->state)) pendingBindings.push_back(binding);
            else if (job->state == State::Complete) insert(job, false);
        }
        if (!busy)
        {
            const auto retained = retainedBytes();
            for (const auto& job : jobs)
            {
                if (job->progress->cancelled || !job->valid()) continue;
                if (job->state == State::Processing && std::chrono::steady_clock::now() >= job->checkAfter)
                {
                    const auto url = job->reply.url, type = job->type, ca = gDirUtilp->getCAFile(); const auto progress = job->progress;
                    job->checkWork = true; ++job->checks;
                    try { job->work = std::async(std::launch::async, [url, type, ca, progress]
                    { Work result; const auto response = fs_chat_media::fetch(url, ca, 0, progress->cancelled, true); if (response.status == 200 && response.type == type) result.reply.state = State::Complete; return result; }); }
                    catch (...) { job->state = State::Uncertain; job->status = tr("FSChatUploadProcessing"); }
                    break;
                }
                const auto extra = job->bytes && !job->dib ? std::size_t{0} : MaxFileBytes;
                if (job->state == State::Preparing && retained <= MaxStagedBytes - extra)
                {
                    // Moving encoded input to the worker needs no second allocation.
                    // Keep its bytes (or a file/DIB output reservation) counted until get().
                    job->workBytes = extra ? extra : job->bytes->size();
                    const auto path = std::move(job->path); auto bytes = std::move(job->bytes); const auto dib = job->dib;
                    try { job->work = std::async(std::launch::async, [path, bytes = std::move(bytes), dib]() mutable { return prepare(std::move(bytes), path, dib); }); }
                    catch (...) { job->workBytes = 0; job->state = State::Failed; job->status = tr("FSChatUploadFailed"); }
                    break;
                }
                if (job->state == State::Queued)
                {
                    const auto bytes = job->bytes; const auto progress = job->progress; const auto cfg = job->config;
                    const auto type = job->type, boundary = LLUUID::generateNewID().asString(), ca = gDirUtilp->getCAFile();
                    auto token = boundary; token.erase(std::remove(token.begin(), token.end(), '-'), token.end());
                    job->uploadWork = true; job->state = State::Uploading;
                    job->workBytes = bytes->size(); // Cancellation can drop the UI's shared reference.
                    try { job->work = std::async(std::launch::async, [bytes, progress, cfg, type, token, ca]
                        { Work result; result.reply = upload(cfg, *bytes, type, token, ca, *progress); return result; }); }
                    catch (...) { job->workBytes = 0; job->state = State::Failed; job->status = tr("FSChatUploadFailed"); }
                    break;
                }
            }
        }
        for (const auto& binding : bindings)
        {
            auto* editor = binding->input(); auto* button = dynamic_cast<LLButton*>(binding->button.get());
            if (!editor || !button) continue;
            layoutAttachment(binding);
            unsigned pending = 0; for (const auto& job : jobs) if (job->binding == binding && fs_chat_upload::blocksSend(job->state)) ++pending;
            if (!pending && binding->error == tr("FSChatUploadPending")) binding->error.clear();
            button->setLabel(LLStringExplicit(pending ? std::to_string(pending) : ""));
            button->setEnabled(LLStartUp::getStartupState() == STATE_STARTED && !gDisconnected);
            button->setToolTip(tr(binding->dragOver ? "FSChatUploadDrop" : pending ? "FSChatUploadPending" : "FSChatUploadAttach"));
            button->setToggleState(binding->dragOver);
        }
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [](const auto& b) { return !b->input(); }), bindings.end());
    }
};
Controller* active = nullptr;
Controller& controller() { if (!active) active = new Controller; active->start(); return *active; }
void show(const std::shared_ptr<Binding>& binding, bool focus = true)
{ LLFloaterReg::showInstance("chat_upload", binding->id, focus); }
std::shared_ptr<Job> newJob(LLChatEntry* editor)
{
    auto& c = controller(); auto b = c.find(editor);
    if (!b || !b->valid() || gDisconnected || LLStartUp::getStartupState() != STATE_STARTED || !FSSessionWorker::inputAllowed() || !c.config.enabled || !c.config.valid() || !c.connection())
    { if (b) show(b); return {}; }
    unsigned count = 0;
    for (const auto& job : c.jobs) if (job->binding == b && fs_chat_upload::blocksSend(job->state)) ++count;
    if (count >= MaxAttachments) { show(b); return {}; }
    // Evict only settled receipts at admission, never active native work.
    if (c.jobs.size() >= 24)
    {
        const auto old = std::find_if(c.jobs.begin(), c.jobs.end(), [](const auto& job) { return !job->work.valid() && !fs_chat_upload::blocksSend(job->state); });
        if (old == c.jobs.end()) return {};
        c.jobs.erase(old);
    }
    auto job = std::make_shared<Job>(); job->binding = b; job->generation = b->generation;
    job->account = gAgent.getID(); job->login = gAgent.getSessionID(); job->grid = LLGridManager::getInstance()->getGrid();
    job->stamp = b->stamp(); job->draft = editor->getText(); job->cursor = editor->getCursorPos(); job->config = c.config;
    b->error.clear(); c.jobs.push_back(job); show(b, !c.config.automatic); return job;
}
class Thumbnail final : public LLPanel
{
public:
    Thumbnail() : LLPanel(Params()) {}
    std::shared_ptr<fs_chat_media::Image> image;
    void clear() { image.reset(); mTexture = nullptr; mImage = nullptr; }
    void draw() override
    {
        if (mImage != image.get())
        {
            mTexture = nullptr; mImage = image.get();
            if (image)
            {
                const auto w = fs_chat_media::textureExtent(image->width), h = fs_chat_media::textureExtent(image->height);
                LLPointer<LLImageRaw> raw = new LLImageRaw(static_cast<U16>(w), static_cast<U16>(h), 4);
                memset(raw->getData(), 0, std::size_t(w) * h * 4);
                for (int row = 0; row < image->height; ++row) memcpy(raw->getData() + std::size_t(row) * w * 4, image->frames[0].rgba.data() + std::size_t(row) * image->width * 4, std::size_t(image->width) * 4);
                mTexture = LLViewerTextureManager::getLocalTexture(raw.get(), false);
                mUV = LLRectf(0.f, static_cast<F32>(image->height) / static_cast<F32>(h), static_cast<F32>(image->width) / static_cast<F32>(w), 0.f);
            }
        }
        if (image && mTexture)
        { const auto fit = fs_chat_media::fit(image->width, image->height, getRect().getWidth(), getRect().getHeight()); gl_draw_scaled_image(0, 0, fit.width, fit.height, mTexture, LLColor4::white, mUV); }
        LLPanel::draw();
    }
private:
    const fs_chat_media::Image* mImage = nullptr;
    LLPointer<LLViewerTexture> mTexture;
    LLRectf mUV;
};
}
void FSChatUpload::bind(LLChatEntry* editor, const LLUUID& conversation, std::function<bool()> available, std::function<std::string()> destination)
{
    if (!editor || !editor->getParent()) return;
    auto& c = controller(); auto b = c.find(editor);
    if (!b)
    {
        b = std::make_shared<Binding>(); b->editor = editor->getHandle(); c.bindings.push_back(b);
        LLButton::Params p; p.name = "chat_attach_image"; p.rect = LLRect(0, 22, 24, 0);
        p.follows.flags = FOLLOWS_RIGHT | FOLLOWS_BOTTOM; p.image_overlay = LLUI::getUIImage("Icon_File_Upload"); p.tool_tip = tr("FSChatUploadAttach");
        auto* button = LLUICtrlFactory::create<LLButton>(p); button->setVisible(false); editor->getParent()->addChild(button); b->button = button->getHandle();
        button->setCommitCallback([b](LLUICtrl*, const LLSD&) { show(b); });
        p.name = "chat_gif_picker"; p.image_overlay = LLUI::getUIImage(""); p.label = "GIF"; p.tool_tip = tr("FSGifOpen");
        auto* gif = LLUICtrlFactory::create<LLButton>(p); gif->setVisible(false); editor->getParent()->addChild(gif); b->gifButton = gif->getHandle();
        gif->setCommitCallback([b](LLUICtrl*, const LLSD&) { FSFloaterGifBoard::open(FSChatUpload::captureTarget(b->input())); });
    }
    if (b->conversation != conversation) ++b->generation;
    b->conversation = conversation; b->available = std::move(available); b->destination = std::move(destination);
    editor->setImagePasteCallbacks([b]
    {
        auto& c = controller();
        return b->valid() && !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED && c.config.enabled && c.config.valid() && c.connection() && FSSessionWorker::inputAllowed() &&
            gViewerWindow && gViewerWindow->getWindow()->isClipboardImageAvailable();
    }, [b] { if (b->input()) FSChatUpload::pasteImage(b->input()); });
}
void FSChatUpload::bindNearby(LLChatEntry* editor) { if (!registered(editor)) bind(editor); }
bool FSChatUpload::registered(LLChatEntry* editor) { return active && bool(active->find(editor)); }
LLSD FSChatUpload::captureTarget(LLChatEntry* editor)
{
    const auto b = controller().find(editor); LLSD target; if (!b) return target;
    target["editor"] = b->id; target["account"] = gAgent.getID(); target["login"] = gAgent.getSessionID();
    target["grid"] = LLGridManager::getInstance()->getGrid(); target["generation"] = static_cast<S32>(b->generation); target["stamp"] = b->stamp(); return target;
}
namespace
{
std::shared_ptr<Binding> gifTarget(const LLSD& target)
{
    if (!active || gDisconnected || LLStartUp::getStartupState() != STATE_STARTED || !FSSessionWorker::inputAllowed()) return {};
    auto b = active->find(target["editor"].asString());
    if (!b) return {};
    const fs_gif_board::Identity current{gAgent.getID().asString(),gAgent.getSessionID().asString(),LLGridManager::getInstance()->getGrid(),b->id,b->stamp(),b->generation};
    const fs_gif_board::Identity expected{target["account"].asUUID().asString(),target["login"].asUUID().asString(),target["grid"].asString(),target["editor"].asString(),target["stamp"].asString(),static_cast<unsigned>(target["generation"].asInteger())};
    return current==expected && b->valid() ? b : std::shared_ptr<Binding>{};
}
}
std::string FSChatUpload::targetLabel(const LLSD& target)
{
    const auto b = gifTarget(target); if (!b) return tr("FSGifNoTarget");
    LLAvatarName name; LLAvatarNameCache::get(gAgent.getID(), &name);
    return name.getUserName() + " → " + b->label();
}
bool FSChatUpload::insertLink(const LLSD& target, const std::string& url)
{
    const auto b = gifTarget(target); if (!b || !fs_chat_media::parse(url)) return false;
    auto* editor = b->input(); const auto value = utf8str_to_wstring(url); const auto before = editor->getWText();
    const auto selection = utf8str_to_wstring(editor->getSelectionString());
    if (wstring_utf8_length(before) + wstring_utf8_length(value) + 2 > editor->getMaxTextLength() + wstring_utf8_length(selection)) return false;
    // Native user insertion preserves undo and replaces the current selection.
    editor->setFocus(true); editor->insertText(" " + url + " "); return true;
}
bool FSChatUpload::blocksSend(LLChatEntry* editor)
{
    if (!active) return false;
    for (const auto& job : active->jobs) if (job->binding->input() == editor && job->valid() && fs_chat_upload::blocksSend(job->state))
    { job->binding->error = tr("FSChatUploadPending"); show(job->binding, false); return true; }
    return false;
}
void FSChatUpload::choose(LLChatEntry* editor)
{
    auto& c = controller(); const auto b = c.find(editor); if (!b || !b->valid() || !c.config.enabled || !c.config.valid() || !c.connection()) return;
    const auto generation = b->generation; const auto account = gAgent.getID(), login = gAgent.getSessionID(); const auto stamp = b->stamp();
    LLFilePickerReplyThread::startPicker([b, generation, account, login, stamp](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter)
    { if (b->input() && b->generation == generation && gAgent.getID() == account && gAgent.getSessionID() == login && b->stamp() == stamp) FSChatUpload::stageFiles(b->input(), files); }, LLFilePicker::FFLOAD_ALL, true);
}
void FSChatUpload::stageFiles(LLChatEntry* editor, const std::vector<std::string>& paths)
{
    const auto b = controller().find(editor); if (!b || paths.empty()) return;
    unsigned count = 0;
    for (const auto& job : controller().jobs) if (job->binding == b && fs_chat_upload::blocksSend(job->state)) ++count;
    if (paths.size() > MaxAttachments - (std::min)(count, MaxAttachments))
    { b->error = tr("FSChatUploadTooMany"); show(b); return; }
    for (const auto& path : paths) { const auto job = newJob(editor); if (!job) break; job->path = path; }
}
void FSChatUpload::stageBytes(LLChatEntry* editor, std::vector<unsigned char> bytes)
{
    const auto b = controller().find(editor);
    if (bytes.size() > MaxFileBytes) { if (b) { b->error = tr("FSChatUploadInvalidImage"); show(b); } return; }
    const auto retained = bytes.size() + controller().retainedBytes();
    if (retained > MaxStagedBytes) { if (b) { b->error = tr("FSChatUploadTooMany"); show(b); } return; }
    if (const auto job = newJob(editor)) job->bytes = std::make_shared<std::vector<unsigned char>>(std::move(bytes));
}
void FSChatUpload::pasteImage(LLChatEntry* editor)
{
    auto& c = controller(); const auto binding = c.find(editor);
    if (!binding || !binding->valid() || !gViewerWindow || !editor->canPasteImage()) return;
    for (const auto& job : c.jobs) if (job->dib && (job->work.valid() || job->state == State::Preparing))
    { binding->error = tr("FSChatUploadClipboardBusy"); show(binding); return; }
    const auto account = gAgent.getID(), login = gAgent.getSessionID(); const auto generation = binding->generation; const auto stamp = binding->stamp();
    LLClipboardImage image;
    if (!gViewerWindow->getWindow()->pasteImageFromClipboard(image)) { binding->error = tr("FSChatUploadInvalidImage"); show(binding); return; }
    if (account != gAgent.getID() || login != gAgent.getSessionID() || generation != binding->generation || stamp != binding->stamp()) return;
    if (!image.files.empty()) { stageFiles(editor, image.files); return; }
    if (image.bytes.empty()) return;
    if (image.dib)
    {
        if (image.bytes.size() > ll_clipboard_image::MaxBitmapBytes) return;
        if (const auto job = newJob(editor)) { job->dib = true; job->bytes = std::make_shared<std::vector<unsigned char>>(std::move(image.bytes)); }
    }
    else stageBytes(editor, std::move(image.bytes));
}
namespace
{
struct DragBinding
{
    std::shared_ptr<Binding> binding;
    LLUUID account, login;
    unsigned generation = 0;
    std::string stamp, grid, destination;
    bool invalid = false;
} dragBinding;
LLView* hitView(LLView* root, S32 x, S32 y)
{
    if (!root || !root->getVisible()) return nullptr;
    for (auto* child : *root->getChildList())
    {
        const auto localX = x - child->getRect().mLeft, localY = y - child->getRect().mBottom;
        if (!child->getVisible() || !child->pointInView(localX, localY)) continue;
        if (auto* hit = hitView(child, localX, localY)) return hit;
        if (child->getMouseOpaque()) return child;
    }
    return nullptr;
}
}
bool FSChatUpload::fileDrop(S32 x, S32 y, LLWindowCallbacks::DragNDropAction action, const std::vector<std::string>& paths)
{
    auto& c = controller();
    for (const auto& b : c.bindings) b->dragOver = false;
    if (action == LLWindowCallbacks::DNDA_START_TRACKING || action == LLWindowCallbacks::DNDA_STOP_TRACKING) dragBinding = {};
    if (action == LLWindowCallbacks::DNDA_STOP_TRACKING) return false;
    if (gDisconnected || LLStartUp::getStartupState() != STATE_STARTED || !FSSessionWorker::inputAllowed() || !c.config.enabled || !c.config.valid() || !c.connection() || paths.empty() || paths.size() > ll_clipboard_image::MaxFiles) return false;
    for (const auto& path : paths)
    {
        const auto value = fs_chat_media::lower(path);
        if (path.empty() || path.size() > 16384 || path.compare(0, 2, "\\\\") == 0 ||
            !(fs_chat_media::ends(value, ".png") || fs_chat_media::ends(value, ".jpg") || fs_chat_media::ends(value, ".jpeg") || fs_chat_media::ends(value, ".gif") || fs_chat_media::ends(value, ".webp"))) return false;
    }
    if (dragBinding.binding && (dragBinding.account != gAgent.getID() || dragBinding.login != gAgent.getSessionID() || dragBinding.grid != LLGridManager::getInstance()->getGrid() || dragBinding.destination != c.config.destination() ||
        dragBinding.generation != dragBinding.binding->generation || dragBinding.stamp != dragBinding.binding->stamp() || !dragBinding.binding->valid())) dragBinding.invalid = true;
    if (dragBinding.invalid) return false;
    auto* leaf = hitView(LLUI::getInstance()->getRootView(), x, y);
    std::shared_ptr<Binding> target;
    for (const auto& b : c.bindings)
    {
        auto* input = b->input(); if (!input || !input->isInVisibleChain() || !b->valid()) continue;
        auto* owner = input->getParentByType<LLFloater>();
        for (auto* parent = leaf; parent; parent = parent->getParent())
            if (parent == input || parent == b->button.get() || (owner && parent == owner)) { target = b; break; }
        if (target) break;
    }
    if (!target) return false;
    if (dragBinding.binding != target)
    { dragBinding.binding = target; dragBinding.account = gAgent.getID(); dragBinding.login = gAgent.getSessionID(); dragBinding.generation = target->generation; dragBinding.stamp = target->stamp(); dragBinding.grid = LLGridManager::getInstance()->getGrid(); dragBinding.destination = c.config.destination(); }
    target->dragOver = true;
    if (action == LLWindowCallbacks::DNDA_DROPPED) { stageFiles(target->input(), paths); target->dragOver = false; }
    return true;
}
void FSChatUpload::preferencesOpening() { (void)controller(); }
void FSChatUpload::applyPreferences()
{
    auto& c = controller(); auto next = savedConfig();
    if (!sameDestination(next, c.config)) { next.automatic = false; gSavedSettings.setBOOL("FSChatUploadAutomatic", false); }
    if (!next.enabled || !sameDestination(next, c.config) || (c.config.automatic && !next.automatic)) for (const auto& job : c.jobs) c.cancel(job, true);
    c.config = std::move(next);
}
void FSChatUpload::shutdown()
{
    if (!active) return;
    for (const auto& job : active->jobs) active->cancel(job, true);
    const auto panels = LLFloaterReg::getFloaterList("chat_upload");
    for (auto* panel : panels) panel->closeFloater(); // Drop popup textures before GL cleanup.
    if (active->running) { gIdleCallbacks.deleteFunction(Controller::idle, active); active->running = false; }
    for (const auto& job : active->jobs) if (job->work.valid()) { try { job->work.get(); } catch (...) { } }
    active->jobs.clear();
}
bool FSFloaterChatUpload::postBuild()
{
    auto* preview = new Thumbnail; preview->setName("thumbnail"); preview->setRect(LLRect(16, 288, 192, 160)); addChild(preview);
    getChild<LLButton>("choose")->setCommitCallback([this](LLUICtrl*, const LLSD&) { const auto b = controller().find(mBinding); if (b) FSChatUpload::choose(b->input()); });
    getChild<LLButton>("paste")->setCommitCallback([this](LLUICtrl*, const LLSD&) { const auto b = controller().find(mBinding); if (b && b->input()) b->input()->pasteImage(); });
    getChild<LLButton>("upload")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        auto& c = controller(); const auto b = c.find(mBinding); if (!b || !b->valid()) return;
        if (getChild<LLCheckBoxCtrl>("automatic")->get()) { c.config.automatic = true; gSavedSettings.setBOOL("FSChatUploadAutomatic", true); }
        for (const auto& job : c.jobs) if (job->binding == b && job->valid() && job->state == State::Review)
        { job->saveGif = getChild<LLCheckBoxCtrl>("save_gif")->get(); job->state = State::Queued; }
        if (b->input() && FSSessionWorker::inputAllowed()) b->input()->setFocus(true);
    });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    { for (const auto& job : controller().jobs) if (job->binding->id == mBinding && (fs_chat_upload::blocksSend(job->state) || job->bytes)) controller().cancel(job); });
    getChild<LLButton>("insert")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    { for (const auto& job : controller().jobs) if (job->binding->id == mBinding && job->state == State::Complete) controller().insert(job, true); });
    getChild<LLButton>("copy")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    { std::string links; for (const auto& job : controller().jobs) if (job->binding->id == mBinding && job->valid() && !job->reply.url.empty()) { if (!links.empty()) links += '\n'; links += job->reply.url; } if (!links.empty()) gViewerWindow->getWindow()->copyTextToClipboard(utf8str_to_wstring(links)); });
    getChild<LLButton>("retry")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        for (const auto& job : controller().jobs)
            if (job->binding->id == mBinding && job->valid() && !job->work.valid() && job->bytes &&
                (job->state == State::Failed || job->state == State::Uncertain))
            {
                job->state = State::Review; job->progress = std::make_shared<Progress>();
                job->status = tr("FSChatUploadRetryWarning"); job->checks = 0;
            }
    });
    getChild<LLButton>("settings")->setCommitCallback([](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("preferences"); });
    return true;
}
void FSFloaterChatUpload::onOpen(const LLSD& key) { mBinding = key.asString(); getChild<LLCheckBoxCtrl>("automatic")->set(false); getChild<LLCheckBoxCtrl>("save_gif")->set(true); }
void FSFloaterChatUpload::onClose(bool app_quitting)
{
    auto& c = controller();
    for (const auto& job : c.jobs)
        if (job->binding->id == mBinding && !c.config.automatic && (job->state == State::Preparing || job->state == State::Review)) c.cancel(job);
    if (auto* preview = dynamic_cast<Thumbnail*>(findChildView("thumbnail"))) preview->clear();
    LLFloater::onClose(app_quitting);
}
void FSFloaterChatUpload::draw()
{
    auto& c = controller(); const auto b = c.find(mBinding); std::string lines; bool review = false, pending = false, insert = false, copy = false, retry = false;
    std::shared_ptr<fs_chat_media::Image> image;
    for (const auto& job : c.jobs) if (job->binding->id == mBinding)
    {
        if (job->invalidated) continue;
        std::string status = job->status;
        if (job->state == State::Preparing) status = tr("FSChatUploadPreparing");
        if (job->state == State::Review && status.empty()) status = tr("FSChatUploadReady");
        if (job->state == State::Queued) status = tr("FSChatUploadQueued");
        if (job->state == State::Uploading) status = tr("FSChatUploadUploading") + " " + std::to_string(job->progress->percent.load()) + "%";
        if (job->state == State::Complete) status = tr(job->inserted ? "FSChatUploadInserted" : "FSChatUploadComplete");
        if (!lines.empty()) lines += '\n'; lines += status;
        if (!job->reply.expiry.empty()) lines += " · " + job->reply.expiry;
        review |= job->state == State::Review; pending |= fs_chat_upload::blocksSend(job->state) || bool(job->bytes);
        insert |= job->state == State::Complete && !job->inserted && job->valid(); copy |= !job->reply.url.empty() && job->valid();
        retry |= job->bytes && !job->work.valid() && (job->state == State::Failed || job->state == State::Uncertain) && job->valid();
        if (job->image) image = job->image;
    }
    if (auto* preview = dynamic_cast<Thumbnail*>(findChildView("thumbnail"))) preview->image = image;
    std::string name; gAgent.buildFullname(name);
    getChild<LLTextBox>("source")->setText(name + " → " + (b ? b->label() : tr("FSChatUploadUnavailable")));
    getChild<LLTextBox>("host")->setText(c.config.base());
    const bool usable = b && b->valid() && c.config.enabled && c.config.valid() && c.connection();
    if (!usable) lines = tr(!c.config.valid() ? "FSChatUploadSetup" : !c.connection() ? "FSChatUploadProxy" : "FSChatUploadUnavailable");
    if (b && !b->error.empty()) lines = b->error + (lines.empty() ? "" : "\n" + lines);
    getChild<LLTextBox>("status")->setText(lines.empty() ? tr("FSChatUploadChoose") : lines);
    getChild<LLButton>("paste")->setEnabled(usable && b->input()->canPasteImage());
    getChild<LLButton>("choose")->setEnabled(usable); getChild<LLButton>("upload")->setEnabled(usable && review);
    getChild<LLButton>("cancel")->setEnabled(pending); getChild<LLButton>("insert")->setEnabled(usable && insert);
    getChild<LLButton>("copy")->setEnabled(copy);
    getChild<LLButton>("retry")->setEnabled(usable && retry);
    LLFloater::draw();
}
