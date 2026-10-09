/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "fssessionworker.h"

#if LL_WINDOWS
#include "fssessionpipe.h"
#include "fseventapibridge.h"
#include "llagent.h"
#include "llagentdata.h"
#include "llappviewer.h"
#include "llfocusmgr.h"
#include "llkeyboard.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"
#include "llviewerinput.h"
#include "llviewerjoystick.h"
#include "llviewernetwork.h"
#include "llviewerwindow.h"
#include "llvoiceclient.h"
#include "llwindow.h"
#include "pipeline.h"
#include <cstdlib>
#include <memory>
#include <map>

namespace
{
using namespace fs_session;
struct Worker
{
    Pipe pipe;
    HANDLE profileLease = INVALID_HANDLE_VALUE;
    WorkerId id{};
    DWORD host = 0;
    std::uint64_t generation = 1, sequence = 0, frames = 0, maintenance = 0, promotionStart = 0;
    ULONGLONG lastCommand = 0;
    LLUUID session;
    Mode mode = Mode::Warm;
    std::string loginGrid, loginName;
    FSSessionWorker::LoginGate gate = FSSessionWorker::LoginGate::Wait;
    bool loginRequested = false, readyApplied = false, promoting = false, detached = false, inputGranted = false;
    bool economyTrimmed = false;
    Message pendingPromotion;
    HWND window = nullptr, parent = nullptr;
    LONG_PTR style = 0, exStyle = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    struct Override { LLSD effective; bool saved = false; };
    std::map<std::string, Override> controls;
    std::array<bool, KEY_COUNT> heldKeys{};

    ~Worker() { if (profileLease != INVALID_HANDLE_VALUE) CloseHandle(profileLease); }

    bool ready() const { return LLStartUp::getStartupState() == STATE_STARTED && !gDisconnected; }
    bool embedded() const { return parent != nullptr; }
    void releaseInput()
    {
        inputGranted = false;
        if (window) RemovePropW(window, InputLeaseProperty);
        gViewerInput.releaseHeldInputs();
        gFocusMgr.setMouseCapture(nullptr);
        gAgent.resetControlFlags(); // Preserves Away, Fly and Mouselook.
        if (LLViewerJoystick::instanceExists()) LLViewerJoystick::instance().setNeedsReset();
        if (LLVoiceClient::instanceExists())
        {
            LLVoiceClient::instance().setUserPTTState(false);
            LLVoiceClient::instance().setMuteMic(true);
        }
    }
    bool grantInput()
    {
        if (!window || !SetPropW(window, InputLeaseProperty, reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(host)))) return false;
        inputGranted = true;
        return true;
    }
    bool unembed()
    {
        if (!embedded()) return true;
        SetLastError(0);
        if (!SetParent(window, nullptr) && GetLastError()) return false;
        SetWindowLongPtrW(window, GWL_STYLE, style);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
        SetWindowPlacement(window, &placement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        parent = nullptr;
        return true;
    }
    void fitSurface()
    {
        if (!embedded() || !IsWindow(parent)) return;
        RECT target{}, current{};
        GetClientRect(parent, &target); GetClientRect(window, &current);
        if (current.right != target.right || current.bottom != target.bottom)
            SetWindowPos(window, nullptr, 0, 0, (std::max)(1L, target.right), (std::max)(1L, target.bottom), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void focusHostedClient() const
    {
        if (!ready() || detached || !embedded() || mode != Mode::Active ||
            !inputGranted || promoting || !window || GetParent(window) != parent ||
            !gViewerWindow || !gViewerWindow->getWindow()) return;
        const HWND root = GetAncestor(parent, GA_ROOT);
        DWORD owner = 0;
        if (!root || !GetWindowThreadProcessId(root, &owner) || owner != host ||
            root != GetForegroundWindow() || !IsWindowVisible(root) || IsIconic(root)) return;
        // The viewer owns a separate native window thread. Its existing API
        // posts SetFocus there, delivering normal focus/IME/timer callbacks.
        // Never synthesize keystrokes or force the application's focus flag.
        gViewerWindow->getWindow()->focusClient();
    }
    bool embed(std::uint64_t value)
    {
        HWND target = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
        DWORD owner = 0;
        if (!window || !IsWindow(target) || !GetWindowThreadProcessId(target, &owner) || owner != host || !ready() || mode != Mode::Active) return false;
        if (parent == target) return true;
        if (!unembed()) return false;
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(window, &placement)) return false;
        style = GetWindowLongPtrW(window, GWL_STYLE); exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_STYLE, (style & ~(WS_OVERLAPPEDWINDOW | WS_POPUP)) | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle & ~WS_EX_APPWINDOW);
        SetLastError(0);
        const HWND previous = SetParent(window, target);
        if (!previous && GetLastError())
        {
            SetWindowLongPtrW(window, GWL_STYLE, style); SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
            SetWindowPlacement(window, &placement);
            return false;
        }
        parent = target;
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        fitSurface();
        focusHostedClient();
        return true;
    }
    bool detach()
    {
        const bool previousGrant = inputGranted;
        releaseInput();
        if (!unembed()) { if (previousGrant) grantInput(); return false; }
        promoting = false; mode = Mode::Active;
        detached = true;
        if (window) { EnableWindow(window, TRUE); ShowWindow(window, SW_RESTORE); }
        // Restore only our temporary overrides. Saved values were never changed.
        for (const auto& control : controls)
            if (auto value = gSavedSettings.getControl(control.first))
            {
                const LLSD saved = value->getSaveValue();
                value->setValue(saved.asBoolean() == control.second.saved ? control.second.effective : saved, false);
            }
        controls.clear();
        // These backends were masked, rather than overwriting their preference
        // values. Reapply the ordinary choices now that management has ended.
        if (LLVoiceClient::instanceExists()) LLVoiceClient::setVoiceEnabled(LLVoiceClient::instance().voiceEnabled(true));
        if (FSEventAPIBridge::instanceExists()) FSEventAPIBridge::instance().applyEnabled(gSavedSettings.getBOOL("EnableLocalEventAPIBridge"));
        return true;
    }
    Message status(const Message& request, const std::string& error = {}) const
    {
        Message reply;
        reply.kind = Kind::Status; reply.worker = id; reply.sequence = request.sequence; reply.generation = generation;
        reply.mode = mode; reply.pid = GetCurrentProcessId();
        reply.frames = frames; reply.maintenance = maintenance;
        reply.surface = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
        if (window)
        {
            RECT rect{}; GetClientRect(window, &rect);
            reply.width = static_cast<std::uint32_t>((std::max)(0L, rect.right));
            reply.height = static_cast<std::uint32_t>((std::max)(0L, rect.bottom));
        }
        const auto startup = LLStartUp::getStartupState();
        reply.state = gDisconnected ? State::Disconnected : ready() ? State::Ready :
            startup >= STATE_LOGIN_AUTH_INIT ? State::Connecting : State::Login;
        reply.grid = loginGrid; reply.name = loginName;
        if (ready() && !gAgentID.isNull()) reply.account = gAgentID.asString();
        if (loginRequested && gate == FSSessionWorker::LoginGate::Wait) reply.flags |= LoginPending;
        if (embedded()) reply.flags |= Embedded;
        if (promoting) reply.flags |= Promoting;
        if (inputGranted && gFocusMgr.getAppHasFocus()) reply.flags |= ClientFocused;
        if (economyTrimmed) reply.flags |= EconomyTrimmed;
        if (!error.empty()) { reply.flags |= Error; reply.detail = error; }
        return reply;
    }
    void reply(const Message& request, const std::string& error = {})
    {
        if (!pipe.send(status(request, error))) { detach(); pipe.close(); }
    }
    void command(const Message& request)
    {
        if (request.worker != id || request.kind == Kind::Status || request.sequence <= sequence)
        { detach(); pipe.close(); return; }
        sequence = request.sequence; lastCommand = GetTickCount64();
        if (request.kind != Kind::Poll && request.generation != generation)
        { reply(request, "Session changed; action was not applied."); return; }
        switch (request.kind)
        {
        case Kind::Poll: reply(request); break;
        case Kind::SetMode:
        {
            if ((request.mode == Mode::Active && !ready()) || !readyApplied || gFocusMgr.focusLocked())
            { reply(request, "Close the modal dialog or wait for login before switching."); break; }
            const bool previousGrant = inputGranted;
            releaseInput();
            const Mode previousMode = mode;
            mode = request.mode;
            if (mode != Mode::Active)
            {
                if (!unembed())
                {
                    mode = previousMode;
                    if (previousGrant) grantInput();
                    reply(request, "Unable to detach the hosted window; finish in this viewer before switching."); break;
                }
                EnableWindow(window, FALSE); ShowWindow(window, SW_HIDE);
                reply(request);
            }
            else
            {
                promoting = true; pendingPromotion = request; promotionStart = frames;
                if (gKeyboard)
                    for (std::size_t key = 0; key < heldKeys.size(); ++key)
                    {
                        const auto native = gKeyboard->inverseTranslateKey(static_cast<KEY>(key));
                        heldKeys[key] = native < 256 && (GetAsyncKeyState(static_cast<int>(native)) & 0x8000) != 0;
                    }
                EnableWindow(window, FALSE); ShowWindow(window, SW_RESTORE);
                // Reply/grant input only after an actual normal buffer swap.
            }
            break;
        }
        case Kind::PermitLogin:
        case Kind::DenyLogin:
            if (!loginRequested || request.grid != loginGrid || request.name != loginName || gate != FSSessionWorker::LoginGate::Wait)
            { reply(request, "Login request changed."); break; }
            gate = request.kind == Kind::PermitLogin ? FSSessionWorker::LoginGate::Allow : FSSessionWorker::LoginGate::Deny;
            reply(request); break;
        case Kind::Embed:
            reply(request, embed(request.surface) ? "" : "Window hosting is unavailable; use the separate viewer window."); break;
        case Kind::Unembed: reply(request, unembed() ? "" : "Unable to detach this window. Close the viewer before its controller."); break;
        case Kind::Focus: focusHostedClient(); reply(request); break;
        case Kind::Detach:
            reply(request, detach() ? "" : "Unable to detach this window. Close the viewer before its controller."); break;
        case Kind::Quit:
            if (!detach()) { reply(request, "Unable to detach this window before logout."); break; }
            reply(request); LLAppViewer::instance()->userQuit(); break;
        default: detach(); pipe.close(); break;
        }
    }
};
std::unique_ptr<Worker> worker;
std::wstring env(const wchar_t* name)
{
    wchar_t buffer[128]{};
    const DWORD size = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return size && size < std::size(buffer) ? std::wstring(buffer, size) : std::wstring();
}
}

bool FSSessionWorker::initialize()
{
    const auto inherited = env(L"FOLDERSTORM_SESSION_PIPE");
    const auto identity = env(L"FOLDERSTORM_SESSION_ID");
    const auto lease = env(L"FOLDERSTORM_SESSION_LOCK");
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_PIPE", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_ID", nullptr);
    SetEnvironmentVariableW(L"FOLDERSTORM_SESSION_LOCK", nullptr);
    if (inherited.empty() && identity.empty() && lease.empty()) return true;
    if (inherited.empty() || identity.size() != 32 || lease.empty()) return false;
    auto value = std::make_unique<Worker>();
    for (std::size_t i = 0; i < 16; ++i)
    {
        const auto digit = [](wchar_t c) -> int { return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1; };
        const int high = digit(identity[i * 2]), low = digit(identity[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        value->id[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    wchar_t* end = nullptr;
    const auto number = _wcstoui64(inherited.c_str(), &end, 16);
    if (!number || !end || *end || number > (std::numeric_limits<std::uintptr_t>::max)()) return false;
    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(number));
    end = nullptr;
    const auto lockNumber = _wcstoui64(lease.c_str(), &end, 16);
    if (!lockNumber || !end || *end || lockNumber > (std::numeric_limits<std::uintptr_t>::max)()) return false;
    const HANDLE lockHandle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(lockNumber));
    if (GetFileType(lockHandle) != FILE_TYPE_DISK || !SetHandleInformation(lockHandle, HANDLE_FLAG_INHERIT, 0)) return false;
    value->profileLease = lockHandle;
    if (!GetNamedPipeServerProcessId(handle, &value->host) || !value->host || !value->pipe.open(handle)) return false;
    value->lastCommand = GetTickCount64();
    worker = std::move(value);
    return true;
}
bool FSSessionWorker::managed() { return worker && !worker->detached; }
void FSSessionWorker::configure()
{
    if (!managed()) return;
    gNonInteractive = false;
    const std::pair<const char*, bool> changes[] = {
        {"AllowMultipleViewers", true}, {"SLURLPassToOtherInstance", false}, {"AutoLogin", false},
        {"HeadlessClient", false}, {"NonInteractive", false}};
    for (const auto& change : changes)
        if (auto control = gSavedSettings.getControl(change.first))
        {
            worker->controls.emplace(change.first, Worker::Override{control->getValue(), control->getSaveValue().asBoolean()});
            control->setValue(LLSD(change.second), false);
        }
}
bool FSSessionWorker::temporaryControl(const std::string& name)
{
    return managed() && worker->controls.find(name) != worker->controls.end();
}
void FSSessionWorker::tick()
{
    if (!managed()) return;
    auto& value = *worker;
    ++value.maintenance;
    if (gViewerWindow && gViewerWindow->getWindow()) value.window = static_cast<HWND>(gViewerWindow->getWindow()->getPlatformWindow());
    if (value.session != gAgentSessionID)
    {
        const bool wasPromoting = value.promoting;
        const bool wasReady = value.readyApplied;
        value.releaseInput(); value.unembed();
        if (wasReady && value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
        value.session = gAgentSessionID; ++value.generation;
        value.promoting = false; value.mode = Mode::Warm; value.readyApplied = false;
        if (wasPromoting) value.reply(value.pendingPromotion, "Session changed before the character was ready.");
    }
    if (gDisconnected && value.readyApplied && value.mode == Mode::Active)
    {
        value.releaseInput(); value.unembed(); value.mode = Mode::Warm;
        if (value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
        if (value.promoting)
        {
            value.promoting = false;
            value.reply(value.pendingPromotion, "Character disconnected before switching completed.");
        }
    }
    if (gKeyboard)
        for (std::size_t key = 0; key < value.heldKeys.size(); ++key)
            if (value.heldKeys[key])
            {
                const auto native = gKeyboard->inverseTranslateKey(static_cast<KEY>(key));
                if ((GetAsyncKeyState(static_cast<int>(native)) & 0x8000) == 0 && !gKeyboard->getKeyDown(static_cast<KEY>(key))) value.heldKeys[key] = false;
            }
    if (LLStartUp::getStartupState() < STATE_LOGIN_AUTH_INIT)
    {
        value.loginRequested = false; value.gate = LoginGate::Wait;
        value.loginName.clear(); value.loginGrid.clear();
    }
    if (value.ready() && !value.readyApplied)
    {
        value.readyApplied = true;
        value.releaseInput();
        if (value.mode != Mode::Active && value.window) { EnableWindow(value.window, FALSE); ShowWindow(value.window, SW_HIDE); }
    }
    value.fitSurface();
    Message request;
    // A pending promotion owns its response slot until first frame/timeout.
    if (!value.promoting && !value.pipe.writing() && value.pipe.receive(request)) value.command(request);
    if (!value.pipe.alive() || GetTickCount64() - value.lastCommand > 15000)
    {
        if (!value.detach()) { value.releaseInput(); value.mode = Mode::Warm; value.promoting = false; }
        value.pipe.close();
    }
}
bool FSSessionWorker::renderAllowed()
{
    return !managed() || !worker->readyApplied || worker->mode == Mode::Active;
}
void FSSessionWorker::prepareDisplay()
{
    if (!worker || !gPipeline.isInit()) return;
    auto& value = *worker;
    if (managed() && value.readyApplied && value.mode == Mode::Economy && !value.promoting)
    {
        // Only disposable screen/shadow targets. Scene objects, textures,
        // shader programs, avatar state and live network work remain intact.
        // Recheck each display: graphics/DPI callbacks may allocate new targets.
        gPipeline.releaseScreenBuffers();
        gPipeline.releaseShadowBuffers();
        gResizeScreenTexture = true;
        gResizeShadowTexture = true;
        value.economyTrimmed = true;
    }
    else if (value.economyTrimmed)
    {
        // The ordinary allocator honors the worker's current graphics choices,
        // including settings changed in standby. Never save a quality downgrade.
        gPipeline.resizeScreenTexture();
        value.economyTrimmed = false;
    }
}
bool FSSessionWorker::inputAllowed()
{
    return !managed() || !worker->readyApplied || (worker->mode == Mode::Active && worker->inputGranted && !worker->promoting);
}
bool FSSessionWorker::keyAllowed(unsigned int key)
{
    return inputAllowed() && (!managed() || key >= worker->heldKeys.size() || !worker->heldKeys[key]);
}
void FSSessionWorker::keyReleased(unsigned int key)
{
    if (managed() && key < worker->heldKeys.size()) worker->heldKeys[key] = false;
}
bool FSSessionWorker::textAllowed()
{
    return inputAllowed() && (!managed() || std::none_of(worker->heldKeys.begin(), worker->heldKeys.end(), [](bool held) { return held; }));
}
void FSSessionWorker::focusHostedClient()
{
    if (managed()) worker->focusHostedClient();
}
int FSSessionWorker::backgroundYield(int normal)
{
    // Standby services the main loop frequently, never at a preview frame rate.
    return managed() && worker->ready() && worker->mode != Mode::Active ? (std::max)(1, (std::min)(normal, 40)) : normal;
}
void FSSessionWorker::framePresented()
{
    if (!managed()) return;
    ++worker->frames;
    if (worker->promoting && worker->ready() && worker->frames - worker->promotionStart >= 2)
    {
        worker->promoting = false;
        if (!worker->grantInput())
        {
            worker->mode = Mode::Warm;
            worker->reply(worker->pendingPromotion, "Unable to establish this window's input owner.");
            return;
        }
        EnableWindow(worker->window, TRUE);
        worker->reply(worker->pendingPromotion);
    }
}
FSSessionWorker::LoginGate FSSessionWorker::loginGate(const std::string& grid, const std::string& name)
{
    if (!managed()) return LoginGate::Allow;
    if (grid.empty() || name.empty() || grid.size() >= 128 || name.size() >= 128 || !validUtf8(grid) || !validUtf8(name)) return LoginGate::Deny;
    if (!worker->loginRequested || worker->loginGrid != grid || worker->loginName != name)
    {
        worker->loginGrid = grid; worker->loginName = name;
        worker->loginRequested = true; worker->gate = LoginGate::Wait;
    }
    return worker->gate;
}
void FSSessionWorker::shutdown()
{
    if (worker)
    {
        worker->inputGranted = false;
        if (worker->window) RemovePropW(worker->window, InputLeaseProperty);
        worker->pipe.close(); worker->detached = true;
    }
    // Keep the inherited profile lease until process/static teardown, after
    // ordinary viewer cleanup has finished saving account/settings files.
}
#else
bool FSSessionWorker::initialize() { return true; }
bool FSSessionWorker::managed() { return false; }
void FSSessionWorker::configure() {}
bool FSSessionWorker::temporaryControl(const std::string&) { return false; }
void FSSessionWorker::tick() {}
bool FSSessionWorker::renderAllowed() { return true; }
void FSSessionWorker::prepareDisplay() {}
bool FSSessionWorker::inputAllowed() { return true; }
bool FSSessionWorker::keyAllowed(unsigned int) { return true; }
void FSSessionWorker::keyReleased(unsigned int) {}
bool FSSessionWorker::textAllowed() { return true; }
void FSSessionWorker::focusHostedClient() {}
int FSSessionWorker::backgroundYield(int normal) { return normal; }
void FSSessionWorker::framePresented() {}
FSSessionWorker::LoginGate FSSessionWorker::loginGate(const std::string&, const std::string&) { return LoginGate::Allow; }
void FSSessionWorker::shutdown() {}
#endif
