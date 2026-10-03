/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "fspanelpreferencelocalassistant.h"
#include "fseventapibridge.h"
#include "fsassistantconfiguration.h"
#include "fsassistantpermissions.h"
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

namespace
{
// Viewer-process state, deliberately absent from settings.xml and workspace profiles.
struct SessionScope
{
    bool initialized = false;
    bool session = false;
    bool externalOverride = false;
    LLSD accessSaved, accessRuntime, permissionsSaved, permissionsRuntime;
    bool accessUnsaved = false, permissionsUnsaved = false;
    bool matches(LLControlVariable& access, LLControlVariable& permissions) const
    {
        return llsd_equals(accessSaved, access.getSaveValue()) && llsd_equals(accessRuntime, access.getValue()) &&
            llsd_equals(permissionsSaved, permissions.getSaveValue()) && llsd_equals(permissionsRuntime, permissions.getValue()) &&
            accessUnsaved == access.hasUnsavedValue() && permissionsUnsaved == permissions.hasUnsavedValue();
    }
    void remember(LLControlVariable& access, LLControlVariable& permissions)
    {
        accessSaved = access.getSaveValue(); accessRuntime = access.getValue();
        permissionsSaved = permissions.getSaveValue(); permissionsRuntime = permissions.getValue();
        accessUnsaved = access.hasUnsavedValue(); permissionsUnsaved = permissions.hasUnsavedValue();
    }
} sAssistantScope;
struct SettingsWriteGuard
{
    explicit SettingsWriteGuard(bool& writing) : mWriting(writing) { mWriting = true; }
    ~SettingsWriteGuard() { mWriting = false; }
    bool& mWriting;
};
}

FSPanelPreferenceLocalAssistant::~FSPanelPreferenceLocalAssistant()
{
    stopConnection();
    mAccessConnection.disconnect();
    mPermissionsConnection.disconnect();
    if (mEditor && mEditor->dirty())
    {
        SettingsWriteGuard guard(mWritingSettings);
        if (mEditor->changedElsewhere())
        {
            mEditor->discardForExternalChange([](const LLSD& access) { return access.asBoolean(); });
            sAssistantScope.session = mEditor->session();
            sAssistantScope.externalOverride = sAssistantScope.session;
            sAssistantScope.remember(*gSavedSettings.getControl("EnableLocalEventAPIBridge"),
                *gSavedSettings.getControl("LocalEventAPIPermissionClasses"));
        }
        else mEditor->cancel(mEditor->baselineAccess().asBoolean());
    }
}

bool FSPanelPreferenceLocalAssistant::postBuild()
{
    getChild<LLCheckBoxCtrl>("local_assistant_enable")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onLocalAssistantToggled(); });
    for (const auto& row : fs_assistant::permissionClasses)
    {
        getChild<LLComboBox>(row.widget)->setCommitCallback(
            [this, key = row.id](LLUICtrl*, const LLSD&) { onLocalAssistantPermission(key); });
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
    getChild<LLComboBox>("permission_preset")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onPreset(); });
    getChild<LLCheckBoxCtrl>("session_only")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onSessionScope(); });
    getChild<LLCheckBoxCtrl>("individual_permissions")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { onIndividualPermissions(); });
    snapshotLocalAssistant();
    auto listener = [this](LLControlVariable*, const LLSD&, const LLSD&) { settingsChangedElsewhere(); };
    mAccessConnection = gSavedSettings.getControl("EnableLocalEventAPIBridge")->getSignal()->connect(listener);
    mPermissionsConnection = gSavedSettings.getControl("LocalEventAPIPermissionClasses")->getSignal()->connect(listener);
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
    settingsChangedElsewhere(); // Also catches equal-value saved-layer changes, which emit no signal.
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

void FSPanelPreferenceLocalAssistant::refreshLocalAssistantControls()
{
    getChild<LLCheckBoxCtrl>("local_assistant_enable")->setValue(gSavedSettings.getBOOL("EnableLocalEventAPIBridge"));
    const LLSD configured = gSavedSettings.getLLSD("LocalEventAPIPermissionClasses");
    for (const auto& row : fs_assistant::permissionClasses)
    {
        getChild<LLComboBox>(row.widget)->setValue(fs_assistant::effectivePermissionLevel(row,
            configured.isMap() && configured.has(row.id) ? configured[row.id].asString() : ""));
    }
    getChild<LLCheckBoxCtrl>("session_only")->setValue(mEditor && mEditor->session());
    getChild<LLCheckBoxCtrl>("individual_permissions")->setValue(mIndividualPermissions);
    getChild<LLView>("permissions_scroll")->setVisible(mIndividualPermissions);
    refreshPreset();
    refreshLocalAssistantStatus();
}

void FSPanelPreferenceLocalAssistant::refreshPreset()
{
    const LLSD configured = gSavedSettings.getLLSD("LocalEventAPIPermissionClasses");
    const auto preset = fs_assistant::permissionPreset([&configured](const char* key) {
        return configured.isMap() && configured.has(key) ? configured[key].asString() : "";
    });
    const char* name = preset == fs_assistant::PermissionPreset::ReadOnly ? "read_only" :
        preset == fs_assistant::PermissionPreset::AskBeforeChanges ? "ask_changes" : "custom";
    getChild<LLComboBox>("permission_preset")->setValue(name);
    getChild<LLTextBox>("preset_summary")->setText(getString(std::string("summary_") + name));
}

void FSPanelPreferenceLocalAssistant::previewSettings(const LLSD& access, const LLSD& permissions)
{
    if (!mEditor) return;
    SettingsWriteGuard guard(mWritingSettings);
    mEditor->preview(access, permissions, access.asBoolean());
    mSettingsConflict = false;
    refreshLocalAssistantControls();
}

void FSPanelPreferenceLocalAssistant::onLocalAssistantToggled()
{
    if (settingsChangedElsewhere()) return;
    stopConnection();
    getChild<LLTextBox>("diagnostic_status")->setText(getString("check_not_run"));
    previewSettings(getChild<LLCheckBoxCtrl>("local_assistant_enable")->getValue(),
        gSavedSettings.getLLSD("LocalEventAPIPermissionClasses"));
}

void FSPanelPreferenceLocalAssistant::onLocalAssistantPermission(const char* key)
{
    if (settingsChangedElsewhere()) return;
    for (const auto& row : fs_assistant::permissionClasses)
    {
        if (std::string(row.id) != key) continue;
        const std::string level = getChild<LLComboBox>(row.widget)->getValue().asString();
        if (!fs_assistant::permissionLevelAllowed(row, level)) return;
        LLSD configured = gSavedSettings.getLLSD("LocalEventAPIPermissionClasses");
        if (!configured.isMap()) configured = LLSD::emptyMap();
        configured[key] = level;
        previewSettings(gSavedSettings.getLLSD("EnableLocalEventAPIBridge"), configured);
        return;
    }
}

void FSPanelPreferenceLocalAssistant::onPreset()
{
    if (settingsChangedElsewhere()) return;
    const std::string name = getChild<LLComboBox>("permission_preset")->getValue().asString();
    if (name == "custom")
    {
        mIndividualPermissions = true;
        refreshLocalAssistantControls();
        return;
    }
    if (name != "read_only" && name != "ask_changes") return;
    LLSD configured = gSavedSettings.getLLSD("LocalEventAPIPermissionClasses");
    if (!configured.isMap()) configured = LLSD::emptyMap();
    fs_assistant::applyPermissionPreset(name == "read_only" ? fs_assistant::PermissionPreset::ReadOnly :
        fs_assistant::PermissionPreset::AskBeforeChanges,
        [&configured](const char* key, const char* level) { configured[key] = level; });
    previewSettings(gSavedSettings.getLLSD("EnableLocalEventAPIBridge"), configured);
}

void FSPanelPreferenceLocalAssistant::onSessionScope()
{
    if (settingsChangedElsewhere()) return;
    if (!mEditor) return;
    mEditor->setSession(getChild<LLCheckBoxCtrl>("session_only")->getValue().asBoolean());
    mSettingsConflict = false;
    refreshLocalAssistantStatus();
}

void FSPanelPreferenceLocalAssistant::onIndividualPermissions()
{
    mIndividualPermissions = getChild<LLCheckBoxCtrl>("individual_permissions")->getValue().asBoolean();
    getChild<LLView>("permissions_scroll")->setVisible(mIndividualPermissions);
}

void FSPanelPreferenceLocalAssistant::snapshotLocalAssistant()
{
    if (mEditor && mEditor->dirty())
    {
        settingsChangedElsewhere();
        return; // Generic baseline capture must not accept an unpublished preview.
    }
    auto bridge = gSavedSettings.getControl("EnableLocalEventAPIBridge");
    auto permissions = gSavedSettings.getControl("LocalEventAPIPermissionClasses");
    if (!bridge || !permissions) return;
    if (!mEditor) mEditor.reset(new AssistantEditor(*bridge, *permissions, EqualSettings()));
    if (!sAssistantScope.initialized || !sAssistantScope.matches(*bridge, *permissions))
    {
        sAssistantScope.initialized = true;
        sAssistantScope.session = mEditor->hasRuntimeOverrides();
        sAssistantScope.externalOverride = sAssistantScope.session;
    }
    mEditor->captureBaseline(sAssistantScope.session);
    sAssistantScope.remember(*bridge, *permissions);
}

void FSPanelPreferenceLocalAssistant::commitPendingSettings()
{
    if (settingsChangedElsewhere()) return;
    if (!mEditor) return;
    SettingsWriteGuard guard(mWritingSettings);
    mEditor->accept(gSavedSettings.getBOOL("EnableLocalEventAPIBridge"));
    sAssistantScope.session = mEditor->session();
    sAssistantScope.externalOverride = false;
    sAssistantScope.remember(*gSavedSettings.getControl("EnableLocalEventAPIBridge"),
        *gSavedSettings.getControl("LocalEventAPIPermissionClasses"));
    mSettingsConflict = false;
}

bool FSPanelPreferenceLocalAssistant::settingsChangedElsewhere()
{
    if (mWritingSettings || !mEditor || !mEditor->changedElsewhere()) return false;
    // Keep external changes and discard our unpublished preview. Cancel must
    // never resurrect that preview or overwrite a newer external value.
    mSettingsConflict = mSettingsConflict || mEditor->dirty();
    {
        SettingsWriteGuard guard(mWritingSettings);
        mEditor->discardForExternalChange([](const LLSD& access) { return access.asBoolean(); });
    }
    sAssistantScope.session = mEditor->hasRuntimeOverrides();
    sAssistantScope.externalOverride = sAssistantScope.session;
    mEditor->snapshot(sAssistantScope.session);
    sAssistantScope.remember(*gSavedSettings.getControl("EnableLocalEventAPIBridge"),
        *gSavedSettings.getControl("LocalEventAPIPermissionClasses"));
    stopConnection();
    getChild<LLTextBox>("diagnostic_status")->setText(getString("check_not_run"));
    refreshLocalAssistantControls();
    return true;
}

void FSPanelPreferenceLocalAssistant::restoreLocalAssistant()
{
    settingsChangedElsewhere();
    if (!mEditor) return;
    {
        SettingsWriteGuard guard(mWritingSettings);
        mEditor->cancel(mEditor->baselineAccess().asBoolean());
    }
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
    const bool session = mEditor && mEditor->session();
    const char* scope_message = mSettingsConflict ? "settings_conflict" :
        session ? (sAssistantScope.externalOverride ? "external_override_text" : "session_override_text") : "saved_scope_text";
    getChild<LLTextBox>("session_override")->setText(getString(scope_message));
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
