#include "llviewerprecompiledheaders.h"
#include "fspanelpreferencelocalassistant.h"
#include "fseventapibridge.h"
#include "fsassistantconfiguration.h"
#include "llapp.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldir.h"
#include "llfile.h"
#include "llinventorymodel.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llsdjson.h"
#include "llstartup.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "llviewercontrol.h"
#include "llweb.h"
#include <boost/json.hpp>
#include <filesystem>

static LLPanelInjector<FSPanelPreferenceLocalAssistant> t_pref_local_assistant("panel_preference_local_assistant");

const FSPanelPreferenceLocalAssistant::LocalAssistantRow FSPanelPreferenceLocalAssistant::kLocalAssistantRows[] = {
    {"read", "local_assistant_perm_read", "allow", false},
    {"camera", "local_assistant_perm_camera", "allow", true},
    {"create", "local_assistant_perm_create", "allow", true},
    {"edit", "local_assistant_perm_edit", "allow", true},
    {"move", "local_assistant_perm_move", "allow", true},
    {"trash", "local_assistant_perm_trash", "allow", true},
    {"nocopy", "local_assistant_perm_nocopy", "ask", true},
    {"wear", "local_assistant_perm_wear", "ask", true},
    {"links", "local_assistant_perm_links", "ask", true},
};

FSPanelPreferenceLocalAssistant::~FSPanelPreferenceLocalAssistant() { stopConnection(); }

bool FSPanelPreferenceLocalAssistant::postBuild()
{
    getChild<LLCheckBoxCtrl>("local_assistant_enable")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onLocalAssistantToggled(); });
    for (const LocalAssistantRow& row : kLocalAssistantRows)
    {
        getChild<LLComboBox>(row.widget)->setCommitCallback(
            [this, key = row.key](LLUICtrl*, const LLSD&) { onLocalAssistantPermission(key); });
    }
    getChild<LLCheckBoxCtrl>("show_details")->setCommitCallback([this](LLUICtrl*, const LLSD& value) {
        getChild<LLTextBox>("bridge_details")->setVisible(value.asBoolean());
    });
    getChild<LLComboBox>("client")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshConfiguration(); });
    getChild<LLButton>("copy_configuration")->setCommitCallback([this](LLUICtrl*, const LLSD&) { copyConfiguration(); });
    getChild<LLButton>("check_connection")->setCommitCallback([this](LLUICtrl*, const LLSD&) { checkConnection(); });
    getChild<LLButton>("setup_guide")->setCommitCallback([](LLUICtrl*, const LLSD&) {
        LLWeb::loadURLExternal("https://github.com/DrCum/Folderstorm/blob/main/doc/help.md#the-local-assistant-bridge");
    });
    refreshLocalAssistantControls();
    refreshConfiguration();
    return LLPanelPreference::postBuild();
}

void FSPanelPreferenceLocalAssistant::onOpen(const LLSD& key)
{
    LLPanelPreference::onOpen(key);
    refreshLocalAssistantControls();
    refreshConfiguration();
}

void FSPanelPreferenceLocalAssistant::saveSettings()
{
    LLPanelPreference::saveSettings();
    // Keep saved and session override layers separate: Cancel must not save --mcp-api.
    snapshotLocalAssistant();
}

void FSPanelPreferenceLocalAssistant::cancel(const std::vector<std::string> settings_to_skip)
{
    stopConnection();
    LLPanelPreference::cancel(settings_to_skip);
    restoreLocalAssistant();
}

void FSPanelPreferenceLocalAssistant::draw()
{
    if (!isInVisibleChain()) { stopConnection(); return; }
    pollConnection();
    if (mStatusTimer.getElapsedTimeF32() >= 1.f)
    {
        refreshLocalAssistantStatus();
        mStatusTimer.reset();
    }
    LLPanelPreference::draw();
}

void FSPanelPreferenceLocalAssistant::onVisibilityChange(bool visible)
{
    LLPanelPreference::onVisibilityChange(visible);
    if (!visible && mDiagnostic)
    {
        stopConnection();
        getChild<LLTextBox>("diagnostic_status")->setText(getString("check_not_run"));
    }
}

bool FSPanelPreferenceLocalAssistant::levelAllowed(const LocalAssistantRow& row, const std::string& level)
{
    if (level == "allow" || level == "deny")
    {
        return true;
    }
    return row.ask && level == "ask";
}

void FSPanelPreferenceLocalAssistant::refreshLocalAssistantControls()
{
    const bool enabled = gSavedSettings.getBOOL("EnableLocalEventAPIBridge");
    getChild<LLCheckBoxCtrl>("local_assistant_enable")->setValue(enabled);
    LLSD configured = gSavedSettings.getLLSD("LocalEventAPIPermissionClasses");
    if (!configured.isMap())
    {
        configured = LLSD::emptyMap();
    }
    for (const LocalAssistantRow& row : kLocalAssistantRows)
    {
        std::string level = row.fallback;
        if (configured.has(row.key) && levelAllowed(row, configured[row.key].asString()))
        {
            level = configured[row.key].asString();
        }
        getChild<LLComboBox>(row.widget)->setValue(level);
    }
    refreshLocalAssistantStatus();
}

void FSPanelPreferenceLocalAssistant::onLocalAssistantToggled()
{
    const bool enabled = getChild<LLCheckBoxCtrl>("local_assistant_enable")->getValue().asBoolean();
    stopConnection();
    getChild<LLTextBox>("diagnostic_status")->setText(getString("check_not_run"));
    gSavedSettings.setBOOL("EnableLocalEventAPIBridge", enabled);
    refreshLocalAssistantStatus();
}

void FSPanelPreferenceLocalAssistant::onLocalAssistantPermission(const char* key)
{
    const LocalAssistantRow* row = nullptr;
    for (const LocalAssistantRow& candidate : kLocalAssistantRows)
    {
        if (std::string(candidate.key) == key)
        {
            row = &candidate;
            break;
        }
    }
    if (!row)
    {
        return;
    }
    const std::string level = getChild<LLComboBox>(row->widget)->getValue().asString();
    if (!levelAllowed(*row, level))
    {
        return;
    }
    LLSD configured = gSavedSettings.getLLSD("LocalEventAPIPermissionClasses");
    if (!configured.isMap())
    {
        configured = LLSD::emptyMap();
    }
    configured[key] = level;
    gSavedSettings.setLLSD("LocalEventAPIPermissionClasses", configured);
}

void FSPanelPreferenceLocalAssistant::snapshotLocalAssistant()
{
    LLControlVariable* bridge = gSavedSettings.getControl("EnableLocalEventAPIBridge");
    LLControlVariable* perms = gSavedSettings.getControl("LocalEventAPIPermissionClasses");
    if (!bridge || !perms)
    {
        mLocalAssistantSnapshotted = false;
        return;
    }
    mBridgeRuntime = bridge->getValue().asBoolean();
    mBridgeSaved = bridge->getSaveValue().asBoolean();
    mBridgeUnsaved = bridge->hasUnsavedValue();
    mPermsRuntime = perms->getValue();
    mPermsSaved = perms->getSaveValue();
    mPermsUnsaved = perms->hasUnsavedValue();
    mLocalAssistantSnapshotted = true;
}

void FSPanelPreferenceLocalAssistant::restoreSavedControl(const char* name, const LLSD& saved, const LLSD& runtime, bool hadUnsaved)
{
    LLControlVariable* control = gSavedSettings.getControl(name);
    if (!control)
    {
        return;
    }
    if (llsd_equals(control->getValue(), runtime) &&
        llsd_equals(control->getSaveValue(), saved) &&
        control->hasUnsavedValue() == hadUnsaved)
    {
        return;
    }
    if (hadUnsaved)
    {
        if (!llsd_equals(control->getSaveValue(), saved) || !control->hasUnsavedValue())
        {
            control->setValue(saved, true);
        }
        if (!llsd_equals(control->getValue(), runtime) || !control->hasUnsavedValue())
        {
            control->setValue(runtime, false);
        }
    }
    else
    {
        control->setValue(runtime, true);
    }
}

void FSPanelPreferenceLocalAssistant::restoreLocalAssistant()
{
    if (!mLocalAssistantSnapshotted)
    {
        return;
    }
    restoreSavedControl("EnableLocalEventAPIBridge", mBridgeSaved, mBridgeRuntime, mBridgeUnsaved);
    restoreSavedControl("LocalEventAPIPermissionClasses", mPermsSaved, mPermsRuntime, mPermsUnsaved);
    refreshLocalAssistantControls();
}



void FSPanelPreferenceLocalAssistant::refreshLocalAssistantStatus()
{
    std::string access = getString("access_off");
    std::string activity = getString("activity_none");
    std::string details;
    if (FSEventAPIBridge::instanceExists())
    {
        const auto status = FSEventAPIBridge::instance().getStatusSnapshot();
        if (mStatusObserved && (mLastReady != status.ready || mLastPolicyGeneration != status.policyGeneration))
        {
            stopConnection();
            getChild<LLTextBox>("diagnostic_status")->setText(getString("check_not_run"));
        }
        mStatusObserved = true;
        mLastReady = status.ready;
        mLastPolicyGeneration = status.policyGeneration;
        if (status.enabled) access = getString(status.ready ? "access_ready" : "access_failed");
        if (!status.error.empty()) details = status.error;
        if (status.lastExternalRequestAt > 0)
        {
            LLStringUtil::format_map_t args;
            args["[SECONDS]"] = std::to_string(static_cast<int>(llmax(0., static_cast<F64>(totalTime()) / 1000000.0 - status.lastExternalRequestAt)));
            activity = getString("activity_recent", args);
        }
        if (status.ready) details = llformat("127.0.0.1:%d", status.port);
    }
    LLControlVariable* enabled = gSavedSettings.getControl("EnableLocalEventAPIBridge");
    getChild<LLTextBox>("session_override")->setText(enabled && enabled->hasUnsavedValue() ? getString("session_override_text") : "");
    getChild<LLTextBox>("local_assistant_status")->setText(access);
    getChild<LLTextBox>("inventory_status")->setText(getString(
        LLStartUp::getStartupState() != STATE_STARTED ? "inventory_login" :
        gInventory.isInventoryUsable() && !LLInventoryModelBackgroundFetch::instance().inventoryFetchInProgress() ? "inventory_ready" : "inventory_loading"));
    getChild<LLTextBox>("activity_status")->setText(activity);
    getChild<LLTextBox>("bridge_details")->setText(details);
}

std::string FSPanelPreferenceLocalAssistant::sidecarPath() const
{
#if LL_WINDOWS
    return gDirUtilp->getExecutableDir() + gDirUtilp->getDirDelimiter() + "fs-mcp.exe";
#elif LL_DARWIN
    // The package keeps the sidecar in Contents/Resources, viewer in Contents/MacOS.
    return gDirUtilp->getAppRODataDir() + gDirUtilp->getDirDelimiter() + "fs-mcp";
#else
    auto root = std::filesystem::path(gDirUtilp->getExecutableDir());
    if (root.filename() == "bin") root = root.parent_path();
    return (root / "fs-mcp").string();
#endif
}

void FSPanelPreferenceLocalAssistant::refreshConfiguration()
{
    const std::string path = sidecarPath();
    const bool found = LLFile::isfile(path);
    LLStringUtil::format_map_t args;
    args["[PATH]"] = path;
    getChild<LLTextBox>("sidecar_status")->setText(getString(found ? "sidecar_found" : "sidecar_missing", args));
    getChild<LLButton>("copy_configuration")->setEnabled(found);
    getChild<LLButton>("check_connection")->setEnabled(found && !mDiagnostic);

    std::string command = path;
    bool cursor_adapter = false;
    const std::string client = getChild<LLComboBox>("client")->getValue().asString();
#if LL_WINDOWS
    // Use the shared link only if it resolves to this installation, not a different channel.
    const auto program_data = LLStringUtil::getoptenv("ProgramData");
    if (program_data)
    {
        const std::string shared = *program_data + "\\Folderstorm\\fs-mcp.exe";
        std::error_code ec;
        if (std::filesystem::equivalent(std::filesystem::path(ll_convert<std::wstring>(shared)), std::filesystem::path(ll_convert<std::wstring>(path)), ec) && !ec)
        {
            command = shared;
        }
    }
    cursor_adapter = client == "cursor" && command.find(' ') != std::string::npos;
#endif
    const std::string discovery = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "");
    mConfiguration = FSAssistantConfiguration::generate(client, command, discovery, cursor_adapter);
    getChild<LLTextEditor>("configuration")->setText(mConfiguration);
}

void FSPanelPreferenceLocalAssistant::copyConfiguration()
{
    refreshConfiguration();
    if (!LLFile::isfile(sidecarPath())) return;
    const LLWString text = utf8str_to_wstring(mConfiguration);
    LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
}

void FSPanelPreferenceLocalAssistant::checkConnection()
{
    stopConnection();
    mDiagnosticOutput.clear();
    LLProcess::Params params;
    params.executable = sidecarPath();
    params.args.add("--diagnose").add("--viewer-pid").add(std::to_string(LLApp::getPid()))
        .add("--discovery").add(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, ""));
    params.files.add(LLProcess::FileParam("pipe")).add(LLProcess::FileParam("pipe")).add(LLProcess::FileParam("pipe"));
    params.attached = true;
    params.autokill = true;
    params.desc = "Local assistant connection check";
    mDiagnostic = LLProcess::create(params);
    getChild<LLTextBox>("diagnostic_status")->setText(getString(mDiagnostic ? "check_running" : "check_launch_failed"));
    if (mDiagnostic)
    {
        mDiagnosticTimer.reset();
        getChild<LLButton>("check_connection")->setEnabled(false);
        mDiagnosticListener = LLEventPumps::instance().obtain("mainloop").listen(
            "local-assistant-check", [this](const LLSD&) {
                if (!isInVisibleChain()) stopConnection(); else pollConnection();
                return false;
            });
    }
}

void FSPanelPreferenceLocalAssistant::pollConnection()
{
    if (!mDiagnostic) return;
    auto& output = mDiagnostic->getReadPipe(LLProcess::STDOUT);
    auto& errors = mDiagnostic->getReadPipe(LLProcess::STDERR);
    if (output.size() + mDiagnosticOutput.size() > 8192 || errors.size() > 8192)
    {
        getChild<LLTextBox>("diagnostic_status")->setText(getString("check_invalid"));
        stopConnection();
        return;
    }
    mDiagnosticOutput += output.read(output.size());
    errors.read(errors.size()); // Raw process error output is never surfaced or logged by this page.
    if (mDiagnostic->isRunning())
    {
        if (mDiagnosticTimer.getElapsedTimeF32() > 5.f)
        {
            getChild<LLTextBox>("diagnostic_status")->setText(getString("check_timeout"));
            stopConnection();
        }
        return;
    }
    std::string result = getString("check_invalid");
    try
    {
        const auto data = LlsdFromJson(boost::json::parse(mDiagnosticOutput));
        if (data["ok"].asBoolean() && data["pid"].asInteger() == LLApp::getPid() &&
            mDiagnostic->getStatus().mState == LLProcess::EXITED && mDiagnostic->getStatus().mData == 0)
        {
            result = getString("check_passed");
        }
        else
        {
            const std::string stage = data["stage"].asString();
            // Only our fixed localized categories enter the UI, never arbitrary response data.
            if (stage == "discovery") result = getString("check_discovery");
            else if (stage == "authorization") result = getString("check_authorization");
            else if (stage == "timeout") result = getString("check_timeout");
            else if (stage == "bridge") result = getString("check_bridge");
        }
    }
    catch (const std::exception&) { }
    getChild<LLTextBox>("diagnostic_status")->setText(result);
    stopConnection();
}

void FSPanelPreferenceLocalAssistant::stopConnection()
{
    mDiagnosticListener.disconnect();
    if (mDiagnostic)
    {
        mDiagnostic->kill("Local assistant preferences closed or check completed");
        mDiagnostic.reset();
        getChild<LLButton>("check_connection")->setEnabled(LLFile::isfile(sidecarPath()));
    }
}
