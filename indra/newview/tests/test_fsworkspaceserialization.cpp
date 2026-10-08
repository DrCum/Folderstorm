/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 The Phoenix Firestorm Project, Inc.
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
/** @file test_fsworkspaceserialization.cpp @brief Strict workspace LLSD contract tests.
 * Links against llcommon; intentionally exercises real LLSD types and copy semantics.
 */
#include "../fsworkspacelayout.h"
#include "llsd.h"
#include "lluuid.h"
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
    workspace.windows[Role::MiniMap] = FSWorkspaceLayout::Window{};
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
    workspace.extra_inventory.push_back(workspace.windows[Role::InventoryPrimary]);
    std::string error;
    expect(!fromLLSD(data, workspace, error) && !error.empty(), message);
    expect(workspace.frame_width == 123.f && workspace.windows.size() == 3 &&
           workspace.extra_inventory.size() == 1 && workspace.extra_inventory.front().visible,
           "Failed parse leaves output untouched");
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
    LLSD legacy = valid; legacy.erase("components");
    expect(fromLLSD(legacy, parsed, error) && parsed.components == All && !parsed.remember_toolbars,
           "Legacy records restore all groups without replacing toolbar content");
    LLSD masked = valid; masked["components"] = Inventory;
    rejected(masked, "Excluded map/chat records cannot smuggle group changes");
    masked["windows"].erase("mini_map"); masked["windows"].erase("conversations_geometry");
    expect(fromLLSD(masked, parsed, error) && parsed.components == Inventory, "Inventory-only record accepted");
    masked["components"] = "2"; rejected(masked, "Component mask rejects coercion");
    masked["components"] = 16; rejected(masked, "Unknown component bits rejected");
    masked["components"] = 0; rejected(masked, "Empty restoration without toolbar content rejected");
    Workspace toolbar_only; toolbar_only.components = 0; toolbar_only.remember_toolbars = true;
    toolbar_only.toolbars = {{1, 0, {"inventory", "workspace_switch"}}, {2, 1, {"map"}}, {3, 2, {}}};
    LLSD toolbar_data = toLLSD(toolbar_only);
    expect(fromLLSD(toolbar_data, parsed, error) && parsed.remember_toolbars && parsed.toolbars.size() == 3 &&
           parsed.toolbars[0].commands[1] == "workspace_switch", "Ordered toolbar-only set round trips");
    LLSD bad_toolbar = toolbar_data; bad_toolbar["toolbars"][1]["location"] = 1;
    rejected(bad_toolbar, "Duplicate toolbar locations rejected");
    bad_toolbar = toolbar_data; bad_toolbar["toolbars"][2]["display_mode"] = 3;
    rejected(bad_toolbar, "Unsupported toolbar display mode rejected");
    bad_toolbar = toolbar_data; bad_toolbar["toolbars"][2]["commands"].append("inventory");
    rejected(bad_toolbar, "Duplicate commands across toolbars rejected");
    bad_toolbar = toolbar_data; bad_toolbar["toolbars"][2]["commands"].append("arbitrary.action()");
    rejected(bad_toolbar, "Command names cannot contain operations or paths");
    bad_toolbar = toolbar_data;
    for (int n = 0; n <= MAX_TOOLBAR_COMMANDS; ++n) bad_toolbar["toolbars"][2]["commands"].append("tool_" + std::to_string(n));
    rejected(bad_toolbar, "Toolbar command count is bounded");
    LLSD data = valid;
    data["windows"]["mini_map"].erase("minimized");
    expect(fromLLSD(data, parsed, error) && !parsed.windows[Role::MiniMap].has_geometry,
           "Hidden never-created window accepts visible:false alone");

    expect(valid["extra_inventory"].isArray() && valid["extra_inventory"].size() == 0 &&
           parsed.extra_inventory.empty(), "Empty extra Inventory array round trips");
    parsed.extra_inventory.push_back(fixture().windows[Role::InventoryPrimary]);
    data = valid; data.erase("extra_inventory");
    expect(fromLLSD(data, parsed, error) && parsed.extra_inventory.empty(),
           "Legacy workspace without extra Inventory array restores no extras");
    Workspace with_extras = fixture();
    with_extras.extra_inventory.push_back(capture({100.f, 200.f, 500.f, 800.f},
                                                {0.f, 0.f, 2400.f, 1000.f}, true, false));
    with_extras.extra_inventory.push_back(capture({600.f, 200.f, 900.f, 700.f},
                                                {0.f, 0.f, 2400.f, 1000.f}, false, true));
    const LLSD extras_valid = toLLSD(with_extras);
    expect(fromLLSD(extras_valid, parsed, error) && parsed.extra_inventory.size() == 2 &&
           parsed.extra_inventory[0].visible && !parsed.extra_inventory[0].minimized &&
           parsed.extra_inventory[0].has_geometry && parsed.extra_inventory[0].width_ui == 400.f &&
           !parsed.extra_inventory[1].visible && parsed.extra_inventory[1].minimized &&
           parsed.extra_inventory[1].height_ui == 500.f && parsed.windows.size() == 3,
           "Extra Inventory visibility, minimization, order and geometry round trip");
    data = valid; data["extra_inventory"] = LLSD::emptyMap();
    rejected(data, "Extra Inventory array type required");
    data = valid; data["extra_inventory"].append(false);
    rejected(data, "Extra Inventory entries require objects");
    data = valid; data["extra_inventory"].append(LLSD::emptyMap());
    rejected(data, "Extra Inventory entry requires visibility");
    data = valid; data["extra_inventory"][0]["visible"] = false;
    expect(fromLLSD(data, parsed, error) && parsed.extra_inventory.size() == 1 &&
           !parsed.extra_inventory[0].has_geometry, "Hidden extra Inventory may omit geometry");
    data["extra_inventory"][0]["minimized"] = true;
    rejected(data, "Minimized extra Inventory requires geometry even when hidden");
    data = extras_valid; data["extra_inventory"][0]["visible"] = 1;
    rejected(data, "Extra Inventory visibility type is strict");
    data = extras_valid; data["extra_inventory"][0]["minimized"] = 1;
    rejected(data, "Extra Inventory minimized type is strict");
    for (const char* field : {"center_x", "center_y", "width_ui", "height_ui"})
    {
        data = extras_valid; data["extra_inventory"][0].erase(field);
        rejected(data, "Extra Inventory partial geometry rejected");
    }
    for (double bad : {0., -1., 16385., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        data = extras_valid; data["extra_inventory"][0]["width_ui"] = bad;
        rejected(data, "Extra Inventory dimensions are finite, positive and bounded");
    }
    data = extras_valid; data["extra_inventory"][0]["center_y"] = std::numeric_limits<double>::quiet_NaN();
    rejected(data, "Extra Inventory centers must be finite");
    data = valid;
    for (int i = 0; i < MAX_EXTRA_INVENTORY_WINDOWS; ++i)
        data["extra_inventory"].append(extras_valid["extra_inventory"][0]);
    expect(fromLLSD(data, parsed, error) && parsed.extra_inventory.size() == MAX_EXTRA_INVENTORY_WINDOWS,
           "Extra Inventory count accepts bound");
    data["extra_inventory"].append(extras_valid["extra_inventory"][0]);
    rejected(data, "Extra Inventory count rejects above bound");
    data = extras_valid;
    data["extra_inventory"][0]["role"] = "unregistered_editor";
    data["extra_inventory"][0]["registry"] = "world_map";
    expect(fromLLSD(data, parsed, error) && parsed.extra_inventory.size() == 2 &&
           parsed.windows.size() == 3 && parsed.ignored_details == 2,
           "Unknown extra Inventory fields are ignored and cannot select registered roles");

    data = valid; data.erase("remember_inventory_folders");
    expect(fromLLSD(data, parsed, error) && !parsed.remember_inventory_folders &&
           !parsed.windows[Role::InventoryPrimary].inventory_folder.present,
           "Legacy workspace keeps geometry-only behavior");
    Workspace folders = with_extras;
    folders.remember_inventory_folders = true;
    const std::string folder_id = "01234567-89ab-cdef-0123-456789abcdef";
    folders.windows[Role::InventoryPrimary].inventory_folder = {true, true, 2, folder_id};
    folders.extra_inventory[0].inventory_folder = {true, false, 0, folder_id};
    const LLSD folders_valid = toLLSD(folders);
    expect(fromLLSD(folders_valid, parsed, error) && parsed.remember_inventory_folders &&
           parsed.windows[Role::InventoryPrimary].inventory_folder.single_folder &&
           parsed.windows[Role::InventoryPrimary].inventory_folder.view_mode == 2 &&
           parsed.extra_inventory[0].inventory_folder.folder_id == folder_id,
           "Primary and ordinary extra folder context round trips");
    data = folders_valid; data["windows"]["inventory_primary"]["inventory_folder"].erase("folder_id");
    rejected(data, "Single-folder context requires a root");
    data = folders_valid; data["extra_inventory"][0]["inventory_folder"].erase("folder_id");
    expect(fromLLSD(data, parsed, error), "Normal presentation permits no selected folder");
    data = folders_valid; data["remember_inventory_folders"] = false;
    rejected(data, "Folder context requires explicit opt-in");
    data = folders_valid; data["remember_inventory_folders"] = 1;
    rejected(data, "Folder opt-in type is strict");
    data = folders_valid; data["windows"]["mini_map"]["inventory_folder"] = folders_valid["windows"]["inventory_primary"]["inventory_folder"];
    rejected(data, "Non-Inventory roles cannot carry folders");
    data = folders_valid; data["extra_inventory"][0]["inventory_folder"]["folder_id"] = folder_id;
    rejected(data, "Folder UUID strings are not coerced");
    data = folders_valid; data["extra_inventory"][0]["inventory_folder"]["folder_id"] = LLUUID::null;
    rejected(data, "Null folder UUID rejected");
    data = folders_valid; data["extra_inventory"][0]["inventory_folder"]["view_mode"] = 3;
    rejected(data, "Folder presentation modes are bounded");
    data = folders_valid; data["extra_inventory"][0]["inventory_folder"]["single_folder"] = 1;
    rejected(data, "Single-folder flag type is strict");

    data = folders_valid;
    data["extra_inventory"][0]["inventory_folder"]["expanded_folders"] = LLSD::emptyArray();
    expect(fromLLSD(data, parsed, error) && parsed.extra_inventory[0].inventory_folder.has_expanded_folders &&
        parsed.extra_inventory[0].inventory_folder.expanded_folders.empty(), "Explicit empty expansion differs from legacy omitted expansion");
    data["extra_inventory"][0]["inventory_folder"]["expanded_folders"].append(LLUUID(folder_id));
    expect(fromLLSD(data, parsed, error) && parsed.extra_inventory[0].inventory_folder.expanded_folders == std::vector<std::string>{folder_id} &&
        toLLSD(parsed)["extra_inventory"][0]["inventory_folder"]["expanded_folders"][0].isUUID(), "Expanded folder references round trip without contents");
    const LLSD expanded_valid = data;
    data["extra_inventory"][0]["inventory_folder"]["expanded_folders"].append(LLUUID(folder_id));
    rejected(data, "Duplicate expanded folders rejected");
    data = expanded_valid; data["extra_inventory"][0]["inventory_folder"]["expanded_folders"][0] = folder_id;
    rejected(data, "Expanded UUID strings are not coerced");
    data = expanded_valid; data["extra_inventory"][0]["inventory_folder"]["expanded_folders"][0] = LLUUID::null;
    rejected(data, "Null expanded folder rejected");
    data = expanded_valid; data["extra_inventory"][0]["inventory_folder"]["expanded_folders"] = true;
    rejected(data, "Expanded folder list type is strict");
    data = expanded_valid; data["windows"]["inventory_primary"]["inventory_folder"]["expanded_folders"] = LLSD::emptyArray();
    rejected(data, "Single-folder presentations cannot carry tree expansion");
    data = expanded_valid; data["extra_inventory"][0]["inventory_folder"]["expanded_folders"] = LLSD::emptyArray();
    for (int i = 0; i < MAX_EXPANDED_INVENTORY_FOLDERS; ++i)
    {
        LLUUID id(folder_id); id.mData[14] = static_cast<U8>(i / 256); id.mData[15] = static_cast<U8>(i % 256);
        data["extra_inventory"][0]["inventory_folder"]["expanded_folders"].append(id);
    }
    expect(fromLLSD(data, parsed, error), "Expanded folders accept their count bound");
    data["extra_inventory"][0]["inventory_folder"]["expanded_folders"].append(LLUUID(folder_id));
    rejected(data, "Expanded folders reject above bound");

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
    data = valid; data["schema"] = SCHEMA_VERSION + 1; rejected(data, "Future workspace schema rejected");
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

    data = valid; data["schema"] = 1;
    expect(fromLLSD(data, parsed, error), "Version-one workspace remains compatible");
    Workspace context = fixture(); context.components |= Graphics | Camera | HUDs;
    context.context.graphics.mode = FSWorkspaceContext::Mode::Current;
    context.context.graphics.values["RenderFarClip"] = {128.};
    context.context.camera.mode = FSWorkspaceContext::Mode::Preset; context.context.camera.preset = "Driving";
    context.context.huds.push_back({"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", "Driving HUD", 31});
    const auto context_data = toLLSD(context);
    expect(fromLLSD(context_data, parsed, error) && parsed.context.huds.size() == 1 && parsed.context.camera.preset == "Driving", "Context groups round trip");
    data = context_data; data["schema"] = 1; rejected(data, "Old schema cannot silently accept new groups");
    data = context_data; data["graphics"]["values"]["FullScreen"] = true; rejected(data, "Display-mode injection rejected");
    data = context_data; data["graphics"]["values"]["RenderFarClip"] = "128"; rejected(data, "Graphics numeric strings rejected");
    data = context_data; data["graphics"]["values"]["RenderFarClip"] = std::numeric_limits<double>::infinity(); rejected(data, "Nonfinite context rejected");
    data = context_data; data["huds"][0]["point"] = 1; rejected(data, "Non-HUD points rejected");
    data = context_data; data["huds"].append(data["huds"][0]); rejected(data, "Duplicate HUD references rejected");
    data = context_data; data["huds"][0]["item"] = "not-a-uuid"; rejected(data, "Malformed HUD reference rejected");
    data = context_data; data.erase("camera"); rejected(data, "Selected group requires its definition");

    LLSD profiles = LLSD::emptyMap();
    expect(canSaveProfile(profiles, "New", error), "Empty map permits first save");
    for (int i = 0; i < MAX_PROFILES; ++i) profiles["Profile " + std::to_string(i)] = valid;
    expect(!canSaveProfile(profiles, "New", error) && canSaveProfile(profiles, "Profile 0", error),
           "32-profile limit allows replacement only");
    profiles["Profile 0"]["future_extension"] = true;
    expect(!canSaveProfile(profiles, "Profile 0", error), "Unknown extensions remain protected on replacement");
    profiles["Profile 0"]["schema"] = 2;
    expect(!canSaveProfile(profiles, "Profile 0", error), "Future-schema definition cannot be overwritten accidentally");
    expect(!canSaveProfile(LLSD("bad collection"), "New", error), "Malformed collection is not discarded");
    expect(!canSaveProfile(LLSD::emptyMap(), "builtin:driving", error), "Builtin names cannot be saved");
    if (!failures) std::cout << "Workspace LLSD tests passed\n";
    return failures ? 1 : 0;
}
