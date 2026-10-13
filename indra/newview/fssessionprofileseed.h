/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_PROFILE_SEED_H
#define FS_SESSION_PROFILE_SEED_H
#include "fssessionprotocol.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>

namespace fs_session
{
constexpr std::size_t ProfileSettingsLimit = 4u * 1024u * 1024u;
constexpr std::size_t ProfileSettingsEntries = 8192;
enum ProfileCopyGroup : std::uint32_t
{
    CopyGraphics = 1u, CopyInterface = 2u, CopyColors = 4u,
    CopyBindings = 8u, CopyQuickPreferences = 16u, CopyToolbars = 32u,
    CopyMovement = 64u, CopyCamera = 128u, CopyAvatar = 256u, CopyRLVa = 512u, CopyAccountUI = 1024u
};
constexpr std::uint32_t CopyLegacy = CopyGraphics | CopyInterface | CopyColors | CopyBindings | CopyQuickPreferences;
constexpr std::uint32_t CopyGlobalPreferences = CopyGraphics | CopyInterface | CopyMovement | CopyCamera | CopyAvatar | CopyRLVa;
constexpr std::uint32_t CopyAll = CopyLegacy | CopyToolbars | CopyMovement | CopyCamera | CopyAvatar | CopyRLVa | CopyAccountUI;
constexpr const char* ProfilePromptMarker = "FSC1\n";
inline bool profilePromptWasOffered(const std::string& bytes)
{
    // Earlier hosts wrote four bytes of the five-byte marker. Preserve their
    // completed setup instead of offering to overwrite the profile again.
    return bytes == ProfilePromptMarker || bytes == "FSC1";
}
inline bool knownProfilePreset(const std::string& name)
{
    for(const auto* preset : {"settings_firestorm.xml", "settings_modern.xml", "settings_phoenix.xml", "settings_hybrid.xml", "settings_v3.xml", "settings_text.xml"})
        if(name==preset)return true;
    return false;
}
inline bool validProfileXmlText(const std::string& bytes)
{
    return !bytes.empty() && bytes.size() <= ProfileSettingsLimit && validUtf8(bytes,true) &&
        bytes.find("<!DOCTYPE") == std::string::npos && bytes.find("<!ENTITY") == std::string::npos;
}
inline bool validProfileCopyGroups(std::uint32_t groups) { return groups && !(groups & ~CopyAll); }
inline bool parseProfileCopyGroups(const std::wstring& text, std::uint32_t& output)
{
    if (text.empty() || text.size() > 4) return false;
    std::uint32_t groups = 0;
    for (const wchar_t digit : text)
    {
        if (digit < L'0' || digit > L'9') return false;
        groups = groups*10u + static_cast<std::uint32_t>(digit-L'0');
    }
    if (!validProfileCopyGroups(groups)) return false;
    output = groups; return true;
}
// Exact native controls reviewed for behavior copying. This also keeps names
// like PrivateLookAtTarget distinct from unrelated account/security controls.
inline std::uint32_t behaviorPreferenceGroup(const std::string& name)
{
    for(const auto* control : {"ArrowKeysAlwaysMove", "LetterKeysAffectsMovementNotFocusChatBar", "FSLetterKeysFocusNearbyChatBar", "AllowTapTapHoldRun", "AutomaticFly", "FSCrouchToggle", "FSIgnoreFinishAnimation", "FSDisableTurningAroundWhenWalkingBackwards", "FSTurnAvatarToSelectedObject", "FSAvatarTurnSpeed", "FSExperimentalRegionCrossingMovementFix", "FSFlyAfterTeleport"})
        if(name==control)return CopyMovement;
    for(const auto* control : {"CameraAngle", "CameraOffsetScale", "ZoomTime", "CameraPositionSmoothing", "DisableCameraConstraints", "FSDisableMouseWheelCameraZoom", "EditCameraMovement", "AppearanceCameraMovement", "ClickOnAvatarKeepsCamera", "FSResetCameraOnTP", "FSResetCameraOnMovement", "ResetViewTurnsAvatar", "FSIgnoreSimulatorCameraConstraints", "MouseWarpMode", "EnableMouselook", "FirstPersonAvatarVisible", "FSShowConvoAndRadarInML", "FSEnableRightclickMenuInMouselook", "FSScrollWheelExitsMouselook", "FSShowMouselookInstructions", "ShowCrosshairs", "MouseSensitivity", "InvertMouse"})
        if(name==control)return CopyCamera;
    for(const auto* control : {"ShowSelectionBeam", "FSBeamShape", "FSBeamShapeScale", "FSBeamColorFile", "FSMaxBeamsPerSecond", "FSParticleChat", "PitchFromMousePosition", "YawFromMousePosition", "FSStaticEyes", "FSPlayDefaultBentoAnimation", "FSResetSkeletonOnStandUp", "DebugLookAtShowNames", "ExodusLookAtLines", "PrivateLookAtTarget", "PrivateLocalLookAtTarget", "PrivatePointAtTarget", "FSLookAtTargetLimitDistance", "FSLookAtTargetMaxDistance", "EnableLookAtTarget", "LimitLookAtTarget", "LimitLookAtTargetDistance"})
        if(name==control)return CopyAvatar;
    for(const auto* control : {"RestrainedLove", "RestrainedLoveCanOOC", "RestrainedLoveForbidGiveToRLV", "RestrainedLoveNoSetEnv", "RestrainedLoveReplaceWhenFolderBeginsWith", "RestrainedLoveShowEllipsis", "RestrainedLoveStackWhenFolderBeginsWith", "RLVaCompatibilityModeList", "RLVaEnableCompositeFolders", "RLVaEnableIMQuery", "RLVaEnableLegacyNaming", "RLVaEnableSharedWear", "RLVaEnableTemporaryAttachments", "RLVaExperimentalCommands", "RLVaExperienceMaturityThreshold", "RLVaHideLockedLayers", "RLVaHideLockedAttachments", "RLVaSharedInvAutoRename", "RLVaShowRedirectChatTyping", "RLVaSplitRedirectChat", "RLVaTopLevelMenu", "RLVaWearReplaceUnlocked"})
        if(name==control)return CopyRLVa;
    return 0;
}
inline bool accountPreferenceAllowed(const std::string& name,std::uint32_t groups)
{
    if(!validProfileCopyGroups(groups))return false;
    if((groups&CopyAvatar) && (name=="DebugLookAt" || name=="DebugLookAtHideOwn" || name=="FSStaticEyes"))return true;
    if((groups&CopyMovement) && name=="FSCrouchToggle")return true;
    if(!(groups&CopyAccountUI))return false;
    for(const auto* control : {"ConversationsExpandMessagePaneFirst", "ConversationsListPaneCollapsed", "ConversationsListPaneWidth", "ConversationsMessagePaneCollapsed", "ConversationsMessagePaneWidth", "ConversationsParticipantListCollapsed", "NavigationBarRatio", "InventoryInboxHeight", "InventoryInboxToggleState", "NearbyChatIsNotTornOff", "NearbyChatIsNotCollapsed", "layout_size_chat_bar_stack", "FSLastNotificationsTab", "FSShowOnlyActiveGestures"})
        if(name==control)return true;
    return false;
}
inline std::uint32_t preferenceCopyGroup(const std::string& name)
{
    if(const auto group=behaviorPreferenceGroup(name))return group;
    if (name.rfind("Render",0) == 0 || name == "TextureMemory" || name == "TextureQuality" || name == "VideoCardMem") return CopyGraphics;
    return CopyInterface;
}
inline bool copyUiFile(std::uint32_t groups, const std::string& name)
{
    if (!validProfileCopyGroups(groups)) return false;
    return (name == "colors.xml" && (groups & CopyColors)) ||
        (name == "key_bindings.xml" && (groups & CopyBindings)) ||
        (name == "quick_preferences.xml" && (groups & CopyQuickPreferences));
}
// Native backup eligibility is necessary but not sufficient: this operation
// copies appearance/graphics, not all global settings or acceptance policies.
inline bool seedPreference(const std::string& name, const std::string& type,
    bool persisted, bool backupable, bool sourceBackupable, std::uint32_t groups = CopyLegacy)
{
    if (!validProfileCopyGroups(groups) || !(groups & preferenceCopyGroup(name)) ||
        !persisted || !backupable || !sourceBackupable || name.empty() || name.size() > 128) return false;
    const std::array<const char*,11> types{"Boolean","S32","U32","F32","String","Vector3","Vector3D","Color3","Color4","Color4U","Rect"};
    if (std::find(types.begin(),types.end(),type) == types.end()) return false;
    if(behaviorPreferenceGroup(name))return true;
    std::string lower = name;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const char* excluded[] = {"login","password","credential","token","secret","cookie","cache","path","directory","folder",
        "settingsfile","account","uuid","assistant","mcp","eventapi","bridge","voice","chatlog","imlog","logfile",
        "firstname","lastname","firstlocal","lastlocal","firstscreen","lastscreen","workspace","gestureboard","snapshot",
        "location","history","grid","remember","trusted","certificate","auth","apikey","webhook","proxy","socks",
        "permission","autoaccept","debit","security","fileaccess","caution","confirm","privacy","restriction","protection"};
    for (const auto* part : excluded) if (lower.find(part) != std::string::npos) return false;
    const char* families[] = {"Render","Window","UI","Font","Skin","FSWorldView","FSChrome","FSFont",
        "FSMenuBackground","FSStatusBar","FSToolbar","ToolBar","Toolbar","FullScreen"};
    for (const auto* prefix : families) if (name.rfind(prefix,0) == 0) return true;
    const char* controls[] = {"ChatFontSize","ChatConsoleFontSize","ShowFPS","Language","InstallLanguage",
        "TextureMemory","TextureQuality","VideoCardMem","EnableHiDPI","ResetUIScaleOnFirstRun",
        "FSEnableVolumeControls","FSShowInterfaceInMouselook"};
    for (const auto* control : controls) if (name == control) return true;
    return false;
}
inline const std::array<const char*,3>& seedUiFiles()
{
    // Ignored dialogs can change future prompts/automatic responses. Keep the
    // destination's warnings and all account/security choices intact.
    static const std::array<const char*,3> files{"colors.xml","key_bindings.xml","quick_preferences.xml"};
    return files;
}
}
#endif
