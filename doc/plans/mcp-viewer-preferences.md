# MCP curated viewer preferences

E8, depends on E0. Extend explicit profile-bound settings transactions; never expose generic LLViewerControl.set/toggle/groups/vars or arbitrary LLSD settings.

## Curated schema and tools

Start with `preferences_schema`, `preferences_get`, `preferences_preview`, `preferences_accept`, `preferences_cancel`, and `preferences_status`. Use short stable public IDs mapped to specific native controls/side-effect handlers. Each descriptor declares type, range/enum, global-private-profile versus per-account storage, availability, preview support, restart requirement and native restrictions.

Audit initial candidates: movement keyboard choices/WASD and quick-jump; graphics quality/draw-distance limits; audio volume/mute; interface scale and selection-beam/LookAt display preferences; established camera preferences where native restrictions permit. Add each only after its actual native callback and saved layer are understood. State limits/unsupported items instead of accepting writes that have no effect.

Exclude paths, credentials, proxy/network/security policies, arbitrary files/URLs, Local Assistant enable/permission settings, restriction-disabling controls, microphone/voice ownership, managed background mode ownership and unreviewed restart-only choices. RLVa restrictions cannot be disabled or overridden through a settings-write permission. Host lifecycle/mode tools belong to the controller plan instead.

New opt-in `preferences_read` and Ask-capable `preferences_write` are separate from inventory Read/Edit. Never authorize the assistant to grant itself permissions. New controls do not get exposed automatically when the viewer adds a debug setting.

## Transaction semantics

Reuse the saved/effective/unsaved-layer and external-writer approach in fsassistantsettings.h and FSWorkspaceController, not generic setters. Bind a bounded transaction and expected revision to actual profile/account/grid/login/worker generation and schema version. Allow at most one MCP preview owner for overlapping settings; a second writer receives Busy/Conflict rather than silently taking ownership.

Preview changes only eligible runtime values, preserving saved layers. It returns a transaction ID/revision and exact before/after values. Acceptance persists only the accepted, unchanged draft values. Cancel restores only still-owned preview changes; it must not overwrite a later native/manual edit or erase an unrelated runtime override. Expiry/revocation cancels future writes and requests ownership-safe preview cleanup, with explicit conflict/partial-restoration results.

Native Preferences is an independently owned transaction. Initially reject MCP writes while it has a conflicting open transaction; reads remain accurate. Test coexistence with workspace changes and host imported settings. Do not treat LLFloaterPreference.apply or saveSettings as acceptance: both run during initialization/opening in this branch. Native explicit OK commits; Cancel discards. If a future UI adds Apply, wire that explicit user action, not a generic helper call. MCP accept is its own clearly approved acceptance and does not press native buttons or close Preferences.

Use actual side-effect handlers (renderer/UI/audio refresh) without creating floaters or moving focus. Require active ownership for a setting whose side effects need rendering/native UI; return RequiresActive rather than automatically switching. Ordinary runtime-safe preferences may work in an inactive ready worker after native verification.

## Delivery and checks

Root implements this state machine sequentially to avoid races with existing Preferences/workspace ownership. A Sol 6.1 High planning/audit agent may catalog candidate controls; final code is reviewed by Sol 6.1 Extra High. Coordinate host UI #50 with its owner; no independent writes to their menus or main.cpp.

Test schema/type/range/availability, wrong account/profile, native restriction refusal, unknown/forbidden settings, preset defaults, repeated preview, explicit accept, cancel, timeout/revocation, saved/runtime/unsaved restoration, external changes and account/worker replacement. Tests must include native transaction entry points rather than assuming generic apply means approval.

Windows: OK/Cancel and reopen, renderer/audio/UI-scale effects, two/five profiles, host import/workspace interactions and manual changes during preview. Confirm saved values persist only after acceptance and no assistant permission/access settings can be changed by this API.
