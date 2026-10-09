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

#include "../fsassistantpermissions.h"
#include "../fsassistantsettings.h"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool passed) { if (!passed) throw std::runtime_error("Assistant settings regression"); }
using Map = std::map<std::string, std::string>;
// Emulates LLControl's value stack for portable controller tests. The real
// LLControl tests below also verify its equal-write and serialization behavior.
struct Control
{
    using Value = std::string;
    std::vector<Value> values;
    std::vector<std::pair<std::string, bool>>* writes;
    std::string key;
    const Value& getValue() const { return values.back(); }
    const Value& getSaveValue() const { return values.size() > 1 ? values[1] : values[0]; }
    bool hasUnsavedValue() const { return values.size() > 2; }
    void setValue(const Value& value, bool saved)
    {
        writes->emplace_back(key, saved);
        if (saved)
        {
            values.resize(1);
            if (values[0] != value) values.push_back(value);
        }
        else if (values.back() != value)
        {
            if (values.size() > 2) values.resize(2);
            if (values.size() < 2) values.push_back(values[0]);
            values.push_back(value);
        }
    }
};
struct Equal { bool operator()(const std::string& a, const std::string& b) const { return a == b; } };
int main()
{
    using namespace fs_assistant;
    Map policy {{"future-class", "future-value"}};
    auto lookup = [&](const char* key) { auto it = policy.find(key); return it == policy.end() ? "" : it->second; };
    auto write = [&](const char* key, const char* value) { policy[key] = value; };
    check(permissionPreset(lookup) == PermissionPreset::Custom);
    applyPermissionPreset(PermissionPreset::ReadOnly, write);
    check(policy.size() == permissionClasses.size() + 1 && policy["future-class"] == "future-value");
    check(permissionPreset(lookup) == PermissionPreset::ReadOnly);
    for (const auto& def : permissionClasses) check(policy[def.id] == (def.ask ? "deny" : "allow"));
    const auto before = policy;
    applyPermissionPreset(PermissionPreset::Custom, write);
    check(policy == before);
    applyPermissionPreset(PermissionPreset::AskBeforeChanges, write);
    check(permissionPreset(lookup) == PermissionPreset::AskBeforeChanges);
    policy["read"] = "ask"; // Invalid Read value follows its Allow fallback.
    check(permissionPreset(lookup) == PermissionPreset::AskBeforeChanges);
    policy["camera"] = "invalid";
    check(permissionPreset(lookup) == PermissionPreset::Custom);
    check(!permissionLevelAllowed(permissionClasses[0], "ask"));

    std::vector<std::pair<std::string, bool>> writes;
    for (const auto& savedAccess : {std::string("off"), std::string("on")})
    {
        Control access {{savedAccess}, &writes, "access"}, perms {{"saved"}, &writes, "permissions"};
        SettingsEditor<Control, Equal> editor(access, perms, Equal());
        editor.setSession(true);
        editor.preview("on", "read-only", true);
        check(access.getSaveValue() == savedAccess && perms.getSaveValue() == "saved");
        editor.accept(true);
        check(editor.session() && !editor.dirty());
        editor.setSession(false);
        editor.preview("off", "ask", false);
        check(!editor.captureBaseline(false));
        writes.clear();
        editor.cancel(true);
        check(access.getValue() == "on" && perms.getValue() == "read-only" && editor.session());
        check(access.getSaveValue() == savedAccess && perms.getSaveValue() == "saved");
        check(writes.front().first == "permissions"); // Policy restored before enable.
        editor.setSession(false);
        editor.accept(true);
        check(access.getSaveValue() == "on" && perms.getSaveValue() == "read-only");
        check(!access.hasUnsavedValue() && !perms.hasUnsavedValue());
    }
    Control access {{"off", "off", "off"}, &writes, "access"}, perms {{"saved"}, &writes, "permissions"};
    SettingsEditor<Control, Equal> editor(access, perms, Equal());
    editor.snapshot(true);
    editor.preview("on", "preview", true);
    editor.cancel(false);
    check(access.getValue() == "off" && access.getSaveValue() == "off" && editor.session());
    // Equal-valued runtime layers may be elided; scope survives without unsafe toggles.
    editor.preview("on", "preview", true);
    perms.setValue("external", true);
    check(editor.changedElsewhere());
    editor.discardForExternalChange([](const std::string& value) { return value == "on"; });
    check(access.getValue() == "off" && perms.getValue() == "external");
    check(!editor.dirty() && !editor.changedElsewhere());
    editor.cancel(false);
    check(perms.getValue() == "external");
    editor.setSession(true); // Scope-only edits do not write either control.
    writes.clear();
    editor.accept(false);
    check(writes.empty() && editor.session());
}
