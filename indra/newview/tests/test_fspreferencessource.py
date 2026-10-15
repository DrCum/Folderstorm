"""Native transaction entry-point checks complement the portable layer model."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(name):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", (ROOT / name).read_text(), flags=re.S)


class PreferencesNativeBoundaryTests(unittest.TestCase):
    def test_exact_native_manifest_and_guarded_dispatch(self):
        text = source("fspreferenceslistener.cpp")
        registration = text[text.index("FSPreferencesListener::FSPreferencesListener()"):text.index("LLSD FSPreferencesListener::assistantLimits()")]
        operations = re.search(r'for \(const char\* op : \{([^}]+)', registration)[1]
        self.assertEqual(set(re.findall(r'"([a-z]+)"', operations)), {"schema", "get", "status", "preview", "accept", "cancel"})
        dispatch = text[text.index("void FSPreferencesListener::dispatch("):]
        self.assertLess(dispatch.index("fs_assistant_request_allowed(request)"), dispatch.index("fs_assistant_async_context(request)"))
        self.assertLess(dispatch.index("fs_assistant::AsyncScope scope(guard)"), dispatch.index("self.transaction.begin("))
        self.assertIn('request["__assistant_epoch"].asString() != identity().viewerSession', dispatch)
        self.assertIn('getPermissionLevel("preferences_read") != "allow"', dispatch)
        self.assertIn('getPermissionLevel("preferences_write") == "deny"', dispatch)
        self.assertIn('std::vector<std::string>{"preferences_write","preferences_read"}', text)

    def test_native_open_cancels_before_every_panel_snapshot(self):
        text = source("llfloaterpreference.cpp")
        on_open = text[text.index("void LLFloaterPreference::onOpen("):text.index("void LLFloaterPreference::onClose(")]
        for snapshot in ("panel->onOpen(key)", "saveSettings()", "saveGraphicsPreset("):
            self.assertLess(on_open.index("FSPreferencesNativeOpening assistant_preview"), on_open.index(snapshot))
        scope = source("fspreferenceslistener.h")
        self.assertIn("FSPreferencesNativeOpening() { fs_preferences_native_opening(); }", scope)
        self.assertIn("~FSPreferencesNativeOpening() { fs_preferences_native_opened(); }", scope)
        # No generic apply/saveSettings/OK/Cancel hook can approve an MCP draft.
        self.assertNotIn("transaction.accept", on_open)
        adapter = source("fspreferenceslistener.cpp")
        self.assertNotIn("LLFloaterPreference::apply", adapter)
        self.assertNotIn("saveSettings(", adapter)
        self.assertNotIn("openFloater(", adapter)

    def test_callbacks_preserve_saved_preset_metadata_and_restore_last(self):
        presets = source("llpresetsmanager.cpp")
        camera = presets[presets.index("void LLPresetsManager::settingChanged()"):presets.index("void LLPresetsManager::getControlNames")]
        graphics = presets[presets.index("void LLPresetsManager::handleGraphicPresetControlChanged("):]
        self.assertIn('if (!fs_preferences_preset_write("PresetCameraActive", ""))', camera)
        self.assertIn('preview_label_hidden = fs_preferences_preset_invalidated(true)', camera)
        self.assertIn('(!preset_name.empty() || preview_label_hidden) && !mIgnoreChangedSignal', camera)
        self.assertIn('if (!fs_preferences_preset_write("PresetGraphicActive", ""))', graphics)
        self.assertIn('fs_preferences_preset_invalidated(false)', graphics)
        adapter = source("fspreferenceslistener.cpp")
        self.assertIn('if (!self.writing || (name != "PresetCameraActive" && name != "PresetGraphicActive")) return false', adapter)
        self.assertIn('var->setValue(value,false)', adapter)
        invalidation = adapter[adapter.index('bool fs_preferences_preset_invalidated('):]
        self.assertIn('if (self.writing || !self.transaction.busy()) return false', invalidation)
        self.assertIn('self.transaction.changed(key)', invalidation)
        preview = adapter[adapter.index('if (op == "preview")', adapter.index("void FSPreferencesListener::dispatch(")):]
        self.assertLess(preview.index("std::vector<std::string> keys = ids"), preview.index("keys.emplace_back(CAMERA_PRESET)"))
        self.assertLess(preview.index("keys.emplace_back(CAMERA_PRESET)"), preview.index("self.transaction.begin("))

    def test_constructed_camera_floater_callback_keeps_type_runtime_only(self):
        camera = source("llfloatercamera.cpp")
        registration = camera[camera.index("bool LLFloaterCamera::postBuild()"):camera.index("void LLFloaterCamera::populatePresetCombo()")]
        self.assertIn('setPresetListChangeCameraCallback(boost::bind(&LLFloaterCamera::populatePresetCombo, this))', registration)
        populate = camera[camera.index("void LLFloaterCamera::populatePresetCombo()"):camera.index("void LLFloaterCamera::onSavePreset()")]
        self.assertIn('if (!fs_preferences_camera_preset_type_write(CAMERA_PRESET_CUSTOM))', populate)
        self.assertIn('gSavedSettings.setU32("CameraPresetType", CAMERA_PRESET_CUSTOM)', populate)
        adapter = source("fspreferenceslistener.cpp")
        self.assertIn('constexpr const char* CAMERA_PRESET_TYPE = "@camera_preset_type"', adapter)
        self.assertIn('if (key == CAMERA_PRESET_TYPE) return gSavedSettings.getControl("CameraPresetType")', adapter)
        self.assertIn('keys.emplace_back(CAMERA_PRESET); keys.emplace_back(CAMERA_PRESET_TYPE)', adapter)
        self.assertIn('key == CAMERA_PRESET_TYPE && !var->isType(TYPE_U32)', adapter)
        self.assertIn('if (camera) self.transaction.changed(CAMERA_PRESET_TYPE)', adapter)
        hook = adapter[adapter.index('bool fs_preferences_camera_preset_type_write('):]
        self.assertIn('dependentPreviewWrite(self.writing,self.metadataAllowed.count(CAMERA_PRESET_TYPE) != 0,value', hook)
        self.assertIn('var->setValue(LLSD(type),saved)', hook)
        self.assertNotIn('switchCameraPreset(', hook)
        # The scoped runtime hook belongs only to this UI metadata callback.
        self.assertEqual(camera.count('fs_preferences_camera_preset_type_write('), 1)
        model = source("fspreferencesmodel.h")
        helper = model[model.index('bool dependentPreviewWrite('):model.index('template<class Value, class Equal> class Transaction')]
        self.assertIn('if (!writing) return false', helper)
        self.assertIn('if (owned) write(value,false)', helper)

    def test_runtime_native_ownership_guards_and_no_audio_input_writes(self):
        adapter = source("fspreferenceslistener.cpp")
        for guard in ("FSSessionWorker::assistantActiveAllowed()", "FSWorkspaceContext::cameraAllowed()", "RLV_BHVR_SHOWNAMES", "RlvActions::canShowNearbyAgents()", "userAutoTuneEnabled", 'instanceVisible("preferences")', "preferencesBusy()", "getPolicyGeneration() != policyGeneration"):
            self.assertIn(guard, adapter)
        for forbidden in ("audio_update_volume", "setMicGain", "setMuteMic", "saveToFile", "setPersist", "setFocus", "requestJump", "startAutoPilot"):
            self.assertNotIn(forbidden, adapter)
        self.assertNotRegex(adapter, r'result\[[^\n]+\]\s*=\s*(?:gAgent\.getSessionID\(\)|source\.login)')
        workspace = source("fsworkspacecontroller.cpp")
        for entry in ("beginPreferencesSession()", "startPreview(", "placePending(", "finishPlacement()", "recoverWindows("):
            method = re.search(r'(?:void|bool) FSWorkspaceController::' + re.escape(entry) + r'[^\n]*\n\{([^}]+)', workspace)
            self.assertIsNotNone(method, entry)
            self.assertIn("fs_preferences_busy()", method[1], entry)

    def test_only_untouched_dormant_workspace_baseline_is_discarded(self):
        workspace = source("fsworkspacecontroller.cpp")
        dormant = workspace[workspace.index("bool FSWorkspaceController::dormantPreferencesBaseline()"):workspace.index("bool FSWorkspaceController::preparePreferencesPreview()")]
        for guard in ("mContextTouched", "mCameraTouched", "mToolbarTouched", "mInboxTouched", "mTouched.empty()", "mCreated.empty()", "mRenamed.empty()", "mReportPreview", "entry.touched", "mPendingPlacement", "mQuickSwitch", "mStartupScheduled", "mDisplayPending", "mHUDPreviewPending", "mPendingHUDs.empty()", "mPendingHandles.empty()", "mPendingExtraInventory.empty()", "mRestoreLayout", "mPreserveCoordinates", "sameSession()", 'instanceVisible("preferences")', "fs_preferences_native_opening_in_progress()", 'mProfiles == gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles")', 'mActive == gSavedPerAccountSettings.getString("FSActiveWorkspace")'):
            self.assertIn(guard, dormant)
        prepare = workspace[workspace.index("bool FSWorkspaceController::preparePreferencesPreview()"):workspace.index("bool FSWorkspaceController::quickSwitchWorkspace(")]
        self.assertIn("if (dormantPreferencesBaseline()) abandon()", prepare)
        for forbidden in ("setValue", "setString", "setLLSD", "commitPendingSettings", "cancelPreferencesSession", "finishPlacement", "apply", "restore"):
            self.assertNotIn(forbidden, prepare)
        adapter = source("fspreferenceslistener.cpp")
        dispatch = adapter[adapter.index("void FSPreferencesListener::dispatch("):]
        self.assertLess(dispatch.index("preparePreferencesPreview()"), dispatch.index("transaction.begin("))


if __name__ == "__main__":
    unittest.main()
