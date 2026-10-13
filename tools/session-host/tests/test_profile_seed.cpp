/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionprofileseed.h"
#include <cassert>
#include <iostream>
#include <set>
#ifdef NDEBUG
#error Session regressions require assertions, including Release configurations.
#endif
using namespace fs_session;
int main()
{
    assert(profilePromptWasOffered("FSC1") && profilePromptWasOffered("FSC1\n"));
    for(const auto* invalid : {"", "FSC", "FSC2", "FSC1\nextra", "FSC1\r\n"}) assert(!profilePromptWasOffered(invalid));
    assert(knownProfilePreset("settings_phoenix.xml") && knownProfilePreset("settings_firestorm.xml"));
    for(const auto* invalid : {"../settings_firestorm.xml", "C:/settings_firestorm.xml", "settings_custom.xml", "account_settings_firestorm.xml"})assert(!knownProfilePreset(invalid));
    // Same-machine UI/graphics preferences are portable, but permission to
    // persist in the main viewer is insufficient to copy account/local data.
    assert(seedPreference("SkinCurrent","String",true,true,true));
    assert(seedPreference("SkinCurrentTheme","String",true,true,true));
    assert(seedPreference("RenderQualityPerformance","U32",true,true,true));
    assert(seedPreference("RenderShadowDetail","S32",true,true,true));
    assert(seedPreference("UIScaleFactor","F32",true,true,true));
    assert(seedPreference("ShowFPS","Boolean",true,true,true));
    assert(seedPreference("ChatFontSize","String",true,true,true));
    assert(seedPreference("FSChromeLeftToolbarPlacement","S32",true,true,true));
    assert(seedPreference("FSWorldViewInsetLeft","F32",true,true,true));
    assert(seedPreference("FSMenuBackgroundAlpha","F32",true,true,true));
    // The real native controls below are backupable, but never portable
    // appearance choices. Future unrelated controls also fail closed.
    for (const auto* policy : {"BrowserFileAccessFromFileUrls","BrowserIgnoreSSLCertErrors",
        "BrowserWebSecurityDisabled","BrowserJavascriptEnabled","AutoAcceptNewInventory",
        "FSPermissionDebitDefaultDeny","PermissionsCautionEnabled","AutomaticallyAcceptPermissions",
        "ParcelMediaAutoPlayEnable","FutureAutomaticAction","UIAutoAcceptDebit","LastGPUString","LastFeatureVersion"})
        assert(!seedPreference(policy,"Boolean",true,true,true));
    assert(!seedPreference("NewNonAppearanceSetting","String",true,true,true));
    const char* isolated[] = {"UserLoginInfo","AutoLogin","RememberPassword","FirstName","LastName",
        "CurrentGrid","LastConnectedGrid","CacheLocation","ClientSettingsFile","InstantMessageLogPath",
        "FSAssistantReviewBulkOperations","EnableLocalEventAPIBridge","EnableVoiceChat","FSWorkspaceStartup",
        "FSWorkspaceInventoryFolders","FSGestureBoardData","MyAccountUUID","TrustedCertificate","HistoryFile",
        "GoogleTranslateAPIKey","FSPrimfeedViewerApiKey","Socks5ProxyHost","BrowserProxyAddress","DiscordWebhook"};
    for (const auto* key : isolated) assert(!seedPreference(key,"String",true,true,true));
    assert(!seedPreference("rEnDeRcAcHePaTh","String",true,true,true));
    assert(!seedPreference("ShowFPS","Boolean",false,true,true));
    assert(!seedPreference("ShowFPS","Boolean",true,false,true));
    assert(!seedPreference("ShowFPS","Boolean",true,true,false));
    assert(!seedPreference("SkinCurrent","LLSD",true,true,true));
    assert(!seedPreference("","Boolean",true,true,true));
    assert(!seedPreference(std::string(129,'a'),"String",true,true,true));
    // Checkbox groups are independent: toolbar/files-only imports must never
    // rewrite global preferences, and graphics-only leaves the skin/layout.
    assert(seedPreference("RenderShadowDetail","S32",true,true,true,CopyGraphics));
    assert(seedPreference("TextureMemory","S32",true,true,true,CopyGraphics));
    assert(!seedPreference("UIScaleFactor","F32",true,true,true,CopyGraphics));
    assert(!seedPreference("SkinCurrent","String",true,true,true,CopyGraphics));
    assert(seedPreference("SkinCurrent","String",true,true,true,CopyInterface));
    assert(seedPreference("FSChromeLeftToolbarPlacement","S32",true,true,true,CopyInterface));
    assert(!seedPreference("RenderShadowDetail","S32",true,true,true,CopyInterface));
    for (const auto group : {CopyColors,CopyBindings,CopyQuickPreferences,CopyToolbars})
    {
        assert(!seedPreference("RenderShadowDetail","S32",true,true,true,group));
        assert(!seedPreference("UIScaleFactor","F32",true,true,true,group));
    }
    for (std::uint32_t groups = 1; groups <= CopyAll; ++groups)
    {
        assert(validProfileCopyGroups(groups));
        assert(!seedPreference("AutoAcceptNewInventory","Boolean",true,true,true,groups));
        assert(!seedPreference("BrowserFileAccessFromFileUrls","Boolean",true,true,true,groups));
        assert(!seedPreference("FSPermissionDebitDefaultDeny","Boolean",true,true,true,groups));
        assert(copyUiFile(groups,"colors.xml") == bool(groups&CopyColors));
        assert(copyUiFile(groups,"key_bindings.xml") == bool(groups&CopyBindings));
        assert(copyUiFile(groups,"quick_preferences.xml") == bool(groups&CopyQuickPreferences));
        assert(!copyUiFile(groups,"toolbars.xml")); // Dedicated account/login path only.
        assert(!copyUiFile(groups,"settings_per_account.xml"));
        assert(!copyUiFile(groups,"ignorable_dialogs.xml"));
    }
    for (const auto invalid : {0u,CopyAll+1u,0xffffffffu})
    {
        assert(!validProfileCopyGroups(invalid));
        assert(!seedPreference("SkinCurrent","String",true,true,true,invalid));
        assert(!copyUiFile(invalid,"colors.xml"));
    }
    std::uint32_t parsed = CopyLegacy;
    for (std::uint32_t group = 1; group <= CopyAll; ++group)
    { assert(parseProfileCopyGroups(std::to_wstring(group),parsed)); assert(parsed == group); }
    for (const auto* invalid : {L"",L"0",L"2048",L"-1",L"+1",L" 1",L"1 ",L"1x",L"4294967295",L"\uFF11"})
    { const auto original = parsed; assert(!parseProfileCopyGroups(invalid,parsed)); assert(parsed == original); }
    assert(validProfileXmlText("<?xml version=\"1.0\"?><toolbars>\n<command name=\"chat\"/>\t</toolbars>"));
    assert(validProfileXmlText("<colors name=\"\xc3\xa9\"/>"));
    assert(!validProfileXmlText(""));
    assert(!validProfileXmlText("<!DOCTYPE toolbars [<!ENTITY text 'unsafe'>]><toolbars/>"));
    assert(!validProfileXmlText("<!ENTITY text 'unsafe'><toolbars/>"));
    assert(!validProfileXmlText(std::string("<\0t\0",4))); // UTF-16 cannot bypass the declaration check.
    assert(!validProfileXmlText("<colors name=\"\xff\"/>"));
    const std::set<std::string> files(seedUiFiles().begin(),seedUiFiles().end());
    assert(files.count("colors.xml") && files.count("key_bindings.xml") && files.count("quick_preferences.xml"));
    for (const auto* forbidden : {"bin_conf.dat","settings_per_account.xml","grids.user.xml","workspaces.xml",
        "gesture_boards.xml","inventory.cache","chat.txt","settings_crash_behavior.xml","ignorable_dialogs.xml"}) assert(!files.count(forbidden));
    for(const auto* control : {"LetterKeysAffectsMovementNotFocusChatBar", "FSIgnoreFinishAnimation", "AutomaticFly"})
        assert(seedPreference(control,"Boolean",true,true,true,CopyMovement));
    assert(!seedPreference("LetterKeysAffectsMovementNotFocusChatBar","Boolean",true,true,true,CopyInterface));
    assert(seedPreference("DisableCameraConstraints","Boolean",true,true,true,CopyCamera));
    assert(seedPreference("ShowSelectionBeam","Boolean",true,true,true,CopyAvatar));
    assert(seedPreference("PrivateLookAtTarget","Boolean",true,true,true,CopyAvatar));
    assert(seedPreference("RestrainedLove","Boolean",true,true,true,CopyRLVa));
    assert(!seedPreference("RestrainedLove","Boolean",true,true,true,CopyAvatar));
    assert(accountPreferenceAllowed("DebugLookAt",CopyAvatar));
    assert(accountPreferenceAllowed("DebugLookAtHideOwn",CopyAvatar));
    assert(accountPreferenceAllowed("FSCrouchToggle",CopyMovement));
    assert(accountPreferenceAllowed("ConversationsListPaneWidth",CopyAccountUI));
    for(const auto* control : {"FSAutorespondMode", "FSProtectedFolders", "FSStoredCameraFocusObjectId", "FSFavoriteGroups", "FSLastOpenIMs", "FSWorkspaceProfiles", "RLVaLoginLastLocation", "InstantMessageLogPath", "FSPrimfeedOAuthToken"})
        assert(!accountPreferenceAllowed(control,CopyAll));
    assert(ProfileSettingsLimit == 4u*1024u*1024u && ProfileSettingsEntries <= 8192);
    std::cout << "Profile settings portability, native backup eligibility and isolated-data exclusions passed.\n";
}
