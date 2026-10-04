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

#include "linden_common.h"
#include "llcontrol.h"
#include "llfile.h"
#include "llsdutil.h"
#include "../fsassistantsettings.h"
#include <stdexcept>

namespace
{
void check(bool value) { if (!value) throw std::runtime_error("Actual assistant control regression"); }
struct Equal { bool operator()(const LLSD& a, const LLSD& b) const { return llsd_equals(a, b); } };
using Editor = fs_assistant::SettingsEditor<LLControlVariable, Equal>;
struct FileCleanup
{
    std::string filename;
    ~FileCleanup() { LLFile::remove(filename); }
};
LLSD policy(const char* read)
{
    LLSD value = LLSD::emptyMap();
    value["read"] = read;
    value["future"]["typed"] = 17;
    return value;
}
void actualControls()
{
    LLUUID id; id.generate();
    LLControlGroup settings("assistant-settings-test-" + id.asString());
    auto* access = settings.declareBOOL("Access", false, "test", LLControlVariable::PERSIST_ALWAYS);
    auto* permissions = settings.declareLLSD("Permissions", policy("saved"), "test", LLControlVariable::PERSIST_ALWAYS);
    FileCleanup file {std::string(LLFile::tmpdir()) + "assistant-settings-" + id.asString() + ".xml"};
    // An unpublished edit in saved scope must also survive autosave without
    // leaking its preview into the next viewer process.
    {
        Editor pending(*access, *permissions, Equal());
        pending.preview(true, policy("preview"), true);
        check(!pending.session() && pending.dirty());
        check(!pending.captureBaseline(false));
        settings.saveToFile(file.filename, false);
        LLControlGroup restarted("assistant-pending-test-" + id.asString());
        restarted.declareBOOL("Access", false, "test", LLControlVariable::PERSIST_ALWAYS);
        restarted.declareLLSD("Permissions", LLSD::emptyMap(), "test", LLControlVariable::PERSIST_ALWAYS);
        check(restarted.loadFromFile(file.filename) == 2);
        check(!restarted.getBOOL("Access"));
        check(llsd_equals(restarted.getLLSD("Permissions"), policy("saved")));
        pending.cancel(false);
        check(!access->getValue().asBoolean());
        check(llsd_equals(permissions->getValue(), policy("saved")));
    }
    for (bool savedOn : {false, true})
    {
        access->setValue(savedOn, true);
        permissions->setValue(policy("saved"), true);
        Editor editor(*access, *permissions, Equal());
        editor.setSession(true);
        editor.preview(true, policy("session"), true);
        editor.accept(true);
        check(access->getSaveValue().asBoolean() == savedOn);
        check(permissions->getSaveValue()["read"].asString() == "saved");
        settings.saveToFile(file.filename, false);
        LLControlGroup restarted("assistant-restart-test-" + id.asString());
        restarted.declareBOOL("Access", false, "test", LLControlVariable::PERSIST_ALWAYS);
        restarted.declareLLSD("Permissions", LLSD::emptyMap(), "test", LLControlVariable::PERSIST_ALWAYS);
        check(restarted.loadFromFile(file.filename) == 2);
        check(restarted.getBOOL("Access") == savedOn);
        check(llsd_equals(restarted.getLLSD("Permissions"), policy("saved")));
        editor.setSession(false);
        editor.preview(false, policy("pending"), false);
        check(!editor.captureBaseline(false));
        editor.cancel(true);
        check(editor.session() && access->getValue().asBoolean());
        check(permissions->getValue()["read"].asString() == "session");
        editor.setSession(false);
        editor.accept(true);
        check(access->getSaveValue().asBoolean() && !access->hasUnsavedValue());
        check(permissions->getSaveValue()["read"].asString() == "session" && !permissions->hasUnsavedValue());
    }
    // Command-line-like override never becomes saved on generic baseline capture.
    access->setValue(false, true);
    access->setValue(true, false);
    Editor cli(*access, *permissions, Equal());
    cli.snapshot(cli.hasRuntimeOverrides());
    cli.accept(true);
    check(cli.session() && !access->getSaveValue().asBoolean());

    // Real LLControl can retain an equal-valued override and can also elide its
    // reconstruction. Neither case may manufacture a temporary access change.
    access->setValue(false, true);
    access->setValue(true, false);
    access->setValue(false, false);
    check(access->hasUnsavedValue());
    Editor equal(*access, *permissions, Equal()); equal.snapshot(true);
    access->setValue(false, true); // A layer-only write sends no property signal.
    equal.cancel(false);
    check(!access->getValue().asBoolean() && !access->getSaveValue().asBoolean() && equal.session());
    check(!access->hasUnsavedValue());

    permissions->setValue(policy("saved"), true);
    Editor external(*access, *permissions, Equal());
    external.preview(true, policy("pending"), true);
    permissions->setValue(policy("external"), true);
    check(external.changedElsewhere());
    external.discardForExternalChange([](const LLSD& value) { return value.asBoolean(); });
    check(!access->getValue().asBoolean());
    check(permissions->getValue()["read"].asString() == "external");
    external.cancel(false);
    check(permissions->getSaveValue()["read"].asString() == "external");
}
}

#ifdef FS_ASSISTANT_SETTINGS_STANDALONE
int main() { actualControls(); }
#else
#include "../test/lltut.h"
namespace tut
{
struct assistant_settings_data {};
using assistant_settings_test = test_group<assistant_settings_data>;
using assistant_settings_object = assistant_settings_test::object;
assistant_settings_test assistant_settings_group("assistant_settings");
template<> template<> void assistant_settings_object::test<1>() { actualControls(); }
}
#endif
