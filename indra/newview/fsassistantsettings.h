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

#ifndef FS_ASSISTANT_SETTINGS_H
#define FS_ASSISTANT_SETTINGS_H
#include <type_traits>
#include <utility>

namespace fs_assistant
{
// Used with the real LLControlVariable and with dependency-free test controls.
// The caller guards synchronous settings signals during every write operation.
template<class Control, class Equal> class SettingsEditor
{
public:
    using Value = std::decay_t<decltype(std::declval<Control>().getValue())>;
    struct State { Value saved, runtime; bool unsaved; };
    SettingsEditor(Control& access, Control& permissions, Equal equal)
        : mAccess(access), mPermissions(permissions), mEqual(equal) { snapshot(false); }
    static State read(Control& control)
    {
        return {control.getSaveValue(), control.getValue(), control.hasUnsavedValue()};
    }
    bool hasRuntimeOverrides() const { return read(mAccess).unsaved || read(mPermissions).unsaved; }
    void snapshot(bool session)
    {
        mAccessBaseline = read(mAccess);
        mPermissionsBaseline = read(mPermissions);
        mSessionBaseline = mSession = session;
        remember();
        mDirty = false;
    }
    bool captureBaseline(bool session)
    {
        if (mDirty) return false; // A generic Preferences snapshot is not acceptance.
        snapshot(session);
        return true;
    }
    bool session() const { return mSession; }
    bool dirty() const { return mDirty; }
    void setSession(bool session) { mSession = session; mDirty = true; }
    void preview(const Value& access, const Value& permissions, bool enabled)
    {
        if (!enabled) mAccess.setValue(access, false);
        mPermissions.setValue(permissions, false);
        if (enabled) mAccess.setValue(access, false);
        remember();
        mDirty = true;
    }
    void accept(bool enabled)
    {
        if (!mSession)
        {
            const auto access = mAccess.getValue(), permissions = mPermissions.getValue();
            if (!enabled) mAccess.setValue(access, true);
            mPermissions.setValue(permissions, true);
            if (enabled) mAccess.setValue(access, true);
        }
        snapshot(mSession);
    }
    void cancel(bool baselineEnabled)
    {
        if (!baselineEnabled) restore(mAccess, mAccessBaseline);
        restore(mPermissions, mPermissionsBaseline);
        if (baselineEnabled) restore(mAccess, mAccessBaseline);
        mSession = mSessionBaseline;
        mDirty = false;
        remember();
    }
    template<class IsEnabled> void discardForExternalChange(IsEnabled enabled)
    {
        const auto access = read(mAccess), permissions = read(mPermissions);
        // Retain externally changed controls, discard our own unpublished changes
        // to the other control. Never restore over an external writer's value.
        const auto accessGoal = same(access, mAccessExpected) ? mAccessBaseline : access;
        const auto permissionsGoal = same(permissions, mPermissionsExpected) ? mPermissionsBaseline : permissions;
        const bool accessEnabled = enabled(accessGoal.runtime);
        if (!accessEnabled) restore(mAccess, accessGoal);
        restore(mPermissions, permissionsGoal);
        if (accessEnabled) restore(mAccess, accessGoal);
        snapshot(hasRuntimeOverrides());
    }
    const Value& baselineAccess() const { return mAccessBaseline.runtime; }
    bool changedElsewhere() const
    {
        return !same(read(mAccess), mAccessExpected) || !same(read(mPermissions), mPermissionsExpected);
    }
private:
    bool same(const State& a, const State& b) const
    {
        return mEqual(a.saved, b.saved) && mEqual(a.runtime, b.runtime) && a.unsaved == b.unsaved;
    }
    void restore(Control& control, const State& state)
    {
        if (same(read(control), state)) return;
        if (!state.unsaved) { control.setValue(state.runtime, true); return; }
        // Avoid removing a still-valid override, and never manufacture a transient
        // opposite value just to recreate an equal-valued redundant layer.
        if (!mEqual(control.getSaveValue(), state.saved) || !control.hasUnsavedValue())
            control.setValue(state.saved, true);
        control.setValue(state.runtime, false);
    }
    void remember() { mAccessExpected = read(mAccess); mPermissionsExpected = read(mPermissions); }
    Control& mAccess;
    Control& mPermissions;
    Equal mEqual;
    State mAccessBaseline, mPermissionsBaseline, mAccessExpected, mPermissionsExpected;
    bool mSession = false, mSessionBaseline = false, mDirty = false;
};
}
#endif
