/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fschatupload.h"
#include "fschatuploadpolicy.h"
#include "fschatmediacodec.h"
#include "fschatmediahttp.h"
#include "fsdata.h"
#include "fsfloaterim.h"
#include "fsfloaternearbychat.h"
#include "fssessionworker.h"
#include "llagent.h"
#include "llapp.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llchatentry.h"
#include "llcheckboxctrl.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llimage.h"
#include "llimview.h"
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
    LLUUID conversation;
    unsigned generation = 0;
    S32 rightInset = 0;
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
        for (const auto& id : session->mInitialTargetIDs) if (!RlvActions::canSendIM(id)) return false;
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
    std::shared_ptr<std::vector<unsigned char>> bytes;
    std::shared_ptr<fs_chat_media::Image> image;
    Config config;
    State state = State::Preparing;
    Reply reply;
    bool uploadWork = false, checkWork = false, inserted = false, invalidated = false;
    unsigned checks = 0;
    std::chrono::steady_clock::time_point checkAfter{};
    bool valid() const
    {
        return !invalidated && !LLApp::isExiting() && !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED &&
            account == gAgent.getID() && login == gAgent.getSessionID() && grid == LLGridManager::getInstance()->getGrid() &&
            binding && generation == binding->generation && binding->valid() && stamp == binding->stamp();
    }
};
Work prepare(std::shared_ptr<std::vector<unsigned char>> bytes, const std::string& path)
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
    if (bytes->empty() || bytes->size() > MaxFileBytes || mime(*bytes).empty()) return work;
    work.image = fs_chat_media::decode(*bytes, 320, true);
    if (work.image) { work.image->frames.resize(1); work.bytes = std::move(bytes); }
    return work;
}
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
        if (!explicitAction && editor->getText() != job->draft) return false;
        const auto& text = editor->getWText(); const auto link = utf8str_to_wstring(job->reply.url);
        if (wstring_utf8_length(text) + wstring_utf8_length(link) + 2 > editor->getMaxTextLength()) return false;
        const auto cursor = job->binding->conversation.isNull() ? static_cast<S32>(text.size()) : explicitAction ? editor->getCursorPos() : job->cursor;
        editor->deselect(); editor->setCursorPos(cursor);
        const auto before = cursor > 0 && !iswspace(text[static_cast<std::size_t>(cursor - 1)]) ? " " : "";
        const auto after = cursor < static_cast<S32>(text.size()) && !iswspace(text[static_cast<std::size_t>(cursor)]) ? " " : "";
        editor->insertText(before + job->reply.url + after); job->inserted = true;
        return true;
    }
    void poll()
    {
        bool busy = false;
        std::size_t retained = 0;
        for (const auto& job : jobs)
        {
            if (!job->valid() || !config.enabled || !sameDestination(config, job->config) || !connection())
            { if (!job->invalidated) cancel(job, true); }
            if (job->work.valid() && job->work.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                Work result; try { result = job->work.get(); } catch (...) { }
                if (job->checkWork)
                {
                    job->checkWork = false;
                    if (!job->progress->cancelled)
                    {
                        if (result.reply.state == State::Complete) { job->state = State::Complete; insert(job, false); job->bytes.reset(); }
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
                        if (job->state == State::Complete) { insert(job, false); job->bytes.reset(); }
                    }
                }
                else if (!job->progress->cancelled)
                {
                    job->bytes = std::move(result.bytes); job->image = std::move(result.image);
                    if (job->bytes) { job->type = mime(*job->bytes); job->state = config.automatic ? State::Queued : State::Review; }
                    else { job->state = State::Failed; job->status = tr("FSChatUploadInvalidImage"); }
                }
            }
            if (job->work.valid()) busy = true;
            if (job->bytes) retained += job->bytes->size();
        }
        if (!busy)
        {
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
                if (job->state == State::Preparing && retained <= MaxStagedBytes - MaxFileBytes)
                {
                    const auto path = std::move(job->path); const auto bytes = std::move(job->bytes);
                    try { job->work = std::async(std::launch::async, [path, bytes] { return prepare(bytes, path); }); }
                    catch (...) { job->state = State::Failed; job->status = tr("FSChatUploadFailed"); }
                    break;
                }
                if (job->state == State::Queued)
                {
                    const auto bytes = job->bytes; const auto progress = job->progress; const auto cfg = job->config;
                    const auto type = job->type, boundary = LLUUID::generateNewID().asString(), ca = gDirUtilp->getCAFile();
                    auto token = boundary; token.erase(std::remove(token.begin(), token.end(), '-'), token.end());
                    job->uploadWork = true; job->state = State::Uploading;
                    try { job->work = std::async(std::launch::async, [bytes, progress, cfg, type, token, ca]
                        { Work result; result.reply = upload(cfg, *bytes, type, token, ca, *progress); return result; }); }
                    catch (...) { job->state = State::Failed; job->status = tr("FSChatUploadFailed"); }
                    break;
                }
            }
        }
        for (const auto& binding : bindings)
        {
            auto* editor = binding->input(); auto* button = dynamic_cast<LLButton*>(binding->button.get());
            if (!editor || !button) continue;
            if (const auto* parent = editor->getParent())
            {
                const auto width = parent->getRect().getWidth() - binding->rightInset - 26 - editor->getRect().mLeft;
                button->setVisible(editor->getVisible() && width >= 40);
                if (width >= 40 && editor->getRect().getWidth() != width) editor->reshape(width, editor->getRect().getHeight());
            }
            const auto rect = editor->getRect(); button->setOrigin(rect.mRight + 2, rect.mBottom);
            button->reshape(24, (std::min)(24, rect.getHeight()));
            unsigned pending = 0; for (const auto& job : jobs) if (job->binding == binding && fs_chat_upload::blocksSend(job->state)) ++pending;
            button->setLabel(LLStringExplicit(pending ? std::to_string(pending) : ""));
            button->setEnabled(LLStartUp::getStartupState() == STATE_STARTED && !gDisconnected);
            button->setToolTip(tr(pending ? "FSChatUploadPending" : "FSChatUploadAttach"));
        }
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [](const auto& b) { return !b->input(); }), bindings.end());
    }
};
Controller* active = nullptr;
Controller& controller() { if (!active) active = new Controller; active->start(); return *active; }
void show(const std::shared_ptr<Binding>& binding)
{ LLFloaterReg::showInstance("chat_upload", binding->id, true); }
std::shared_ptr<Job> newJob(LLChatEntry* editor)
{
    auto& c = controller(); auto b = c.find(editor);
    if (!b || !b->valid() || !FSSessionWorker::inputAllowed() || !c.config.enabled || !c.config.valid() || !c.connection())
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
    c.jobs.push_back(job); show(b); return job;
}
class Thumbnail final : public LLPanel
{
public:
    Thumbnail() : LLPanel(Params()) {}
    std::shared_ptr<fs_chat_media::Image> image;
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
                mUV = LLRectf(0.f, static_cast<F32>(image->height) / h, static_cast<F32>(image->width) / w, 0.f);
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
        auto rect = editor->getRect(); if (rect.getWidth() < 70) return;
        b = std::make_shared<Binding>(); b->editor = editor->getHandle(); c.bindings.push_back(b);
        b->rightInset = editor->getParent()->getRect().getWidth() - rect.mRight;
        editor->reshape(rect.getWidth() - 26, rect.getHeight());
        LLButton::Params p; p.name = "chat_attach_image"; p.rect = LLRect(rect.mRight - 24, rect.mBottom + 22, rect.mRight, rect.mBottom);
        p.follows.flags = FOLLOWS_RIGHT | FOLLOWS_BOTTOM; p.image_overlay = LLUI::getUIImage("Icon_File_Upload"); p.tool_tip = tr("FSChatUploadAttach");
        auto* button = LLUICtrlFactory::create<LLButton>(p); editor->getParent()->addChild(button); b->button = button->getHandle();
        button->setCommitCallback([b](LLUICtrl*, const LLSD&) { show(b); });
    }
    if (b->conversation != conversation) ++b->generation;
    b->conversation = conversation; b->available = std::move(available); b->destination = std::move(destination);
}
void FSChatUpload::bindNearby(LLChatEntry* editor) { if (!registered(editor)) bind(editor); }
bool FSChatUpload::registered(LLChatEntry* editor) { return active && bool(active->find(editor)); }
bool FSChatUpload::blocksSend(LLChatEntry* editor)
{
    if (!active) return false;
    for (const auto& job : active->jobs) if (job->binding->input() == editor && job->valid() && fs_chat_upload::blocksSend(job->state)) return true;
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
{ for (const auto& path : paths) { const auto job = newJob(editor); if (!job) break; job->path = path; } }
void FSChatUpload::stageBytes(LLChatEntry* editor, std::vector<unsigned char> bytes)
{
    if (bytes.size() > MaxFileBytes) return;
    std::size_t retained = bytes.size();
    for (const auto& job : controller().jobs) if (job->bytes) retained += job->bytes->size();
    if (retained > MaxStagedBytes) return;
    if (const auto job = newJob(editor)) job->bytes = std::make_shared<std::vector<unsigned char>>(std::move(bytes));
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
    if (active->running) { gIdleCallbacks.deleteFunction(Controller::idle, active); active->running = false; }
    for (const auto& job : active->jobs) if (job->work.valid()) { try { job->work.get(); } catch (...) { } }
    active->jobs.clear();
}
bool FSFloaterChatUpload::postBuild()
{
    auto* preview = new Thumbnail; preview->setName("thumbnail"); preview->setRect(LLRect(16, 288, 192, 160)); addChild(preview);
    getChild<LLButton>("choose")->setCommitCallback([this](LLUICtrl*, const LLSD&) { const auto b = controller().find(mBinding); if (b) FSChatUpload::choose(b->input()); });
    getChild<LLButton>("upload")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        auto& c = controller(); const auto b = c.find(mBinding); if (!b || !b->valid()) return;
        if (getChild<LLCheckBoxCtrl>("automatic")->get()) { c.config.automatic = true; gSavedSettings.setBOOL("FSChatUploadAutomatic", true); }
        for (const auto& job : c.jobs) if (job->binding == b && job->valid() && job->state == State::Review) job->state = State::Queued;
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
void FSFloaterChatUpload::onOpen(const LLSD& key) { mBinding = key.asString(); getChild<LLCheckBoxCtrl>("automatic")->set(false); }
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
    getChild<LLTextBox>("status")->setText(lines.empty() ? tr("FSChatUploadChoose") : lines);
    getChild<LLButton>("choose")->setEnabled(usable); getChild<LLButton>("upload")->setEnabled(usable && review);
    getChild<LLButton>("cancel")->setEnabled(pending); getChild<LLButton>("insert")->setEnabled(usable && insert);
    getChild<LLButton>("copy")->setEnabled(copy);
    getChild<LLButton>("retry")->setEnabled(usable && retry);
    LLFloater::draw();
}
