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
    // Same-machine UI/graphics preferences are portable, but permission to
    // persist in the main viewer is insufficient to copy account/local data.
    assert(seedPreference("SkinCurrent","String",true,true,true));
    assert(seedPreference("SkinCurrentTheme","String",true,true,true));
    assert(seedPreference("RenderQualityPerformance","U32",true,true,true));
    assert(seedPreference("RenderShadowDetail","S32",true,true,true));
    assert(seedPreference("UIScaleFactor","F32",true,true,true));
    assert(seedPreference("ShowFPS","Boolean",true,true,true));
    assert(seedPreference("ChatFontSize","String",true,true,true));
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
    const std::set<std::string> files(seedUiFiles().begin(),seedUiFiles().end());
    assert(files.count("colors.xml") && files.count("key_bindings.xml") && files.count("quick_preferences.xml"));
    for (const auto* forbidden : {"bin_conf.dat","settings_per_account.xml","grids.user.xml","workspaces.xml",
        "gesture_boards.xml","inventory.cache","chat.txt","settings_crash_behavior.xml"}) assert(!files.count(forbidden));
    assert(ProfileSettingsLimit == 4u*1024u*1024u && ProfileSettingsEntries <= 8192);
    std::cout << "Profile settings portability, native backup eligibility and isolated-data exclusions passed.\n";
}
