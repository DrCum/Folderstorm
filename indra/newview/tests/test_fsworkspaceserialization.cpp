/** @file test_fsworkspaceserialization.cpp @brief Strict workspace LLSD contract tests.
 * Links against llcommon; intentionally exercises real LLSD types and copy semantics.
 */
#include "../fsworkspacelayout.h"
#include "llsd.h"
#include <iostream>
#include <limits>

using namespace FSWorkspaceLayout;
namespace
{
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
Workspace fixture()
{
    Workspace workspace;
    workspace.frame_width = 2400.f;
    workspace.frame_height = 1000.f;
    workspace.chrome.viewport_enabled = true;
    workspace.chrome.inset_right = 32.f;
    workspace.windows[Role::InventoryPrimary] = capture({1700.f, 100.f, 2100.f, 800.f},
                                                      {0.f, 0.f, 2400.f, 1000.f}, true, false);
    workspace.windows[Role::MiniMap] = Window{};
    workspace.windows[Role::ConversationsGeometry] = capture({100.f, 30.f, 400.f, 330.f},
                                                           {0.f, 0.f, 2400.f, 1000.f}, false, false);
    workspace.has_inbox = true;
    workspace.inbox_expanded = false;
    workspace.inbox_height = 200.f;
    return workspace;
}
void rejected(const LLSD& data, const char* message)
{
    Workspace workspace = fixture();
    workspace.frame_width = 123.f;
    std::string error;
    expect(!fromLLSD(data, workspace, error) && !error.empty(), message);
    expect(workspace.frame_width == 123.f && workspace.windows.size() == 3, "Failed parse leaves output untouched");
}
}
int main()
{
    const LLSD valid = toLLSD(fixture());
    Workspace parsed;
    std::string error;
    expect(fromLLSD(valid, parsed, error) && error.empty(), "Complete schema1 accepted");
    expect(parsed.frame_width == 2400.f && parsed.chrome.inset_right == 32.f && parsed.windows.size() == 3 &&
           parsed.has_inbox && parsed.inbox_height == 200.f, "Nested chrome/window/panel round trip");
    expect(!valid["windows"]["conversations_geometry"].has("visible") &&
           !valid["windows"]["conversations_geometry"].has("minimized"), "Host geometry omits lifecycle fields");
    LLSD data = valid;
    data["windows"]["mini_map"].erase("minimized");
    expect(fromLLSD(data, parsed, error) && !parsed.windows[Role::MiniMap].has_geometry,
           "Hidden never-created window accepts visible:false alone");

    for (const char* field : {"schema", "type", "chrome", "frame", "windows", "panels", "world_view_in_mouselook"})
    {
        data = valid; data.erase(field); rejected(data, "Missing envelope field rejected");
    }
    for (const char* field : {"schema", "viewport_enabled", "inset_left", "inset_right", "inset_top", "inset_bottom",
                              "left_placement", "right_placement", "left_offset", "right_offset",
                              "bottom_dock", "nav_favorites", "menu_status"})
    {
        data = valid; data["chrome"].erase(field); rejected(data, "Missing nested chrome field rejected");
    }
    for (const char* field : {"region", "start", "end", "margin_left", "margin_right"})
    {
        data = valid; data["chrome"]["bottom_dock"].erase(field); rejected(data, "Missing span field rejected");
    }
    data = valid; data["schema"] = 2; rejected(data, "Future workspace schema rejected");
    data = valid; data["chrome"]["schema"] = 2; rejected(data, "Future nested chrome schema rejected");
    data = valid; data["schema"] = "1"; rejected(data, "Schema string not coerced");
    data = valid; data["world_view_in_mouselook"] = 1; rejected(data, "Boolean integer not coerced");
    data = valid; data["frame"]["width_ui"] = "2400"; rejected(data, "Number string not coerced");
    data = valid; data["windows"]["inventory_primary"]["visible"] = "true"; rejected(data, "Visibility string not coerced");
    data = valid; data["windows"]["conversations_geometry"]["visible"] = false; rejected(data, "Host visibility forbidden even when false");
    data = valid; data["windows"]["conversations_geometry"]["minimized"] = false; rejected(data, "Host minimize forbidden");
    data = valid; data["windows"]["mini_map"]["center_x"] = .5; rejected(data, "Partial hidden geometry rejected");
    data = valid; data["windows"]["mini_map"]["visible"] = true; rejected(data, "Visible role requires geometry");
    for (double bad : {0., -1., 16385., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        data = valid; data["windows"]["inventory_primary"]["width_ui"] = bad;
        rejected(data, "Nonpositive, oversized or nonfinite dimensions rejected");
        data = valid; data["frame"]["height_ui"] = bad; rejected(data, "Frame dimensions bounded");
        data = valid; data["panels"]["inventory_primary_inbox"]["height_ui"] = bad; rejected(data, "Panel dimensions bounded");
    }
    data = valid; data["windows"]["inventory_primary"]["center_x"] = -.25;
    expect(fromLLSD(data, parsed, error), "Finite out-of-frame centers accepted for later fitting");
    data = valid; data["windows"]["inventory_primary"]["center_y"] = std::numeric_limits<double>::quiet_NaN();
    rejected(data, "Nonfinite normalized centers rejected");
    data = valid; data["chrome"]["inset_left"] = std::numeric_limits<double>::infinity(); rejected(data, "Nonfinite chrome rejected");
    data = valid; data["chrome"]["left_placement"] = 99; rejected(data, "Chrome enum bounds strict");
    data = valid; data["chrome"]["bottom_dock"]["start"] = 101.; rejected(data, "Chrome percent bounds strict");

    data = valid;
    data["extension"] = true;
    data["frame"]["display_id"] = "ignored";
    data["windows"]["unregistered_editor"] = LLSD::emptyMap();
    data["windows"]["mini_map"]["zoom"] = 12;
    data["chrome"]["bottom_dock"]["extension"] = true;
    data["panels"]["inventory_primary_inbox"]["filter"] = "ignored";
    data["panels"]["unsupported_split"] = true;
    expect(fromLLSD(data, parsed, error) && parsed.ignored_details == 7 && parsed.windows.size() == 3,
           "Unknown extensions counted and never enter allowlist");
    data = valid;
    for (int i = 0; i < 4; ++i) data["windows"]["unknown" + std::to_string(i)] = LLSD::emptyMap();
    rejected(data, "Role record count bounded including unknown roles");

    LLSD profiles = LLSD::emptyMap();
    expect(canSaveProfile(profiles, "New", error), "Empty map permits first save");
    for (int i = 0; i < MAX_PROFILES; ++i) profiles["Profile " + std::to_string(i)] = valid;
    expect(!canSaveProfile(profiles, "New", error) && canSaveProfile(profiles, "Profile 0", error),
           "32-profile limit allows replacement only");
    profiles["Profile 0"]["schema"] = 2;
    expect(!canSaveProfile(profiles, "Profile 0", error), "Future-schema definition cannot be overwritten accidentally");
    expect(!canSaveProfile(LLSD("bad collection"), "New", error), "Malformed collection is not discarded");
    expect(!canSaveProfile(LLSD::emptyMap(), "builtin:driving", error), "Builtin names cannot be saved");
    if (!failures) std::cout << "Workspace LLSD tests passed\n";
    return failures ? 1 : 0;
}
