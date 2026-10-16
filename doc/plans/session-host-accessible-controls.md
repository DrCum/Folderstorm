# Session host accessible controls and audio states

Implementation of the session-host UI/UX review's suggestion 6: resize repaint handling, accessible toggle states, clearer audio scope, independent labels, Accessibility settings and acknowledged voice ownership. The implementation is on `feat/host-accessible-controls`, stacked on PR #47 at `30f05ed18f19b2a49363346ce3306c43e8286de8` to preserve the concurrent host, monitor and Local Assistant work. It reuses PR #39's pinned Lucide renderer. IPC/protocol 16, worker audio behavior, account routing and lifecycle code are unchanged.

[Implementation validation and native acceptance](session-host-accessible-controls-validation.md) records completed checks and the Windows runtime checks still required. The phases below retain the agreed design and acceptance requirements; they are delivered together in this PR.

[Revision 2 interactive mockup](../mockups/session-host-controls-v2.html) · [Toolbar preview](../mockups/session-host-controls-v2.png) · [Accessibility preview](../mockups/session-host-accessibility-v2.png)

The mockup includes optional audio quick actions for comparison. Existing quick-action selections and defaults remain respected. Its schematic viewport is not a screenshot of the native viewer. Change permanent-button labels and quick-action labels independently, use the Accessibility and original settings tabs, change the active character separately from the Chat sender, and inspect the voice-state scenarios. Dock buttons illustrate detached headers without moving browser windows. Do not add an active-character badge or other character overlay to the world viewport; the existing character dropdown provides that identity.

## Recommended names and behavior

Use **BG Audio** instead of **Mute**. Do not append **Muted**, **On** or **Off** by default, including when labels are visible. The control affects inactive viewers' world, UI and media audio. The active viewer keeps its normal audio settings; host notification alerts have their own settings. An enabled BG Audio policy allows audio through the host policy and does not override the viewer's own mute choices.

Use **Voice follows active** instead of **Voice**, also without a visible text-state suffix by default. The words identify the policy directly. Changing this option does not select the Chat sender or claim that a microphone is transmitting. Lucide icon changes, native checked state and a selected-state cue remain available in every label mode. Accessibility adds an optional **Show text states on audio buttons** checkbox, default Off, to display On/Off/Muted when audio quick-action labels are shown. Full accessible state and action tooltips are always supplied independently of this visual preference.

Keep **Always on top** as the accessible name for each window's pin control. Replace the current diamond glyphs with Lucide `pin` and `pin-off`.

| Control | State and icon | Checked meaning | Scope |
| --- | --- | --- | --- |
| BG Audio | Muted policy: `volume-x`; audio allowed: `volume-2`; text suffix optional | Checked means audio allowed | Inactive managed viewers' world, UI and media |
| Voice follows active | Off: `mic-off`; On: `mic` | Checked means follow enabled | Managed voice policy |
| Always on top | Off: `pin-off`; On: `pin` | Checked means pinned | Only the invoking detached window |
| Shared Chat and monitor Chat | `message-square`; add a selected border and checkmark while visible | Checked means that surface's Chat is visible | Host Chat or that monitor, independently |
| Attention | `inbox`, count and selected border/checkmark while open | Checked means inbox visible | Opening does not acknowledge items |
| Diagnostics, where exposed as an action | Stable icon with selected state | Checked means details visible | Host presentation |

Do not use crossed-out Chat/bell icons for hidden panels: they suggest message blocking or muted notifications. Dock/pop-out and expand/collapse remain commands, rather than audio-style checkboxes. Their icons and accessible names describe the next action: `external-link` / `arrow-down-left`, and `chevron-down` / `chevron-up`. Disclosure state must reflect the actual visible expansion; automatic compaction and the saved density preference are separate inputs.

BG Audio deliberately reverses the presentation of the existing `muteBackground` value: `audioChecked = !muteBackground`. Retain the existing key, defaults, saved data, command IDs 204/205, permissions and native audio behavior. Apply this inversion consistently in Preferences, quick actions, overflow menus, tooltips and accessibility. Do not let an automatic native checkbox flip and the command handler each invert the value.

## Independent labels and Accessibility

Add **Toolbar button labels** for permanent buttons such as Login, Chat, Monitors and Preferences. Keep **Quick action labels** as a separate preference for optional actions such as BG Audio, Voice follows active and Attention. Both offer Icon and text, Text, and Icons with tooltips. Changing either setting must not change the other, and resizing must not overwrite either saved selection. Utility buttons already expressed as icons, such as docking and disclosure, retain their full accessible action names.

Keep both label choices in **Layout & theme → Appearance**, alongside Button spacing. Add an **Accessibility** settings category/tab that repeats both label choices and Button spacing, plus Switch effect from **Switching & monitors → Switching** and Enable character shortcuts from **Controls → Keyboard shortcuts**. Keep every original control in place. Add **Show text states on audio buttons**, default Off, and the contrast controls below in Accessibility.

Repeated controls are alternate views of the same draft fields and persisted values, never separate accessibility copies. Changes in either location update the other immediately; Apply/OK accept one shared baseline and Cancel restores the last accepted values across every location. Refreshing a mirrored control must not generate a second edit or invert a toggle again. Accessibility focus, meaningful names and native state semantics remain enabled regardless of the optional text-state setting. Preserve the existing Quick action labels value during migration. Initialize the independent permanent-button setting to Icon and text, retaining visible Login/Chat labels; docking and disclosure utilities stay icon buttons.

## Windows and host contrast controls

Add these controls to **Accessibility**:

- **Windows contrast settings…** opens the Windows Contrast themes/High Contrast settings page through `ms-settings:easeofaccess-highcontrast`. It is a navigation action, not a deferred preference. If Windows cannot open the page, show a short failure message and the manual Settings route. The user changes the system-wide setting in Windows; the host does not call `SPI_SETHIGHCONTRAST` or otherwise alter desktop-wide contrast.
- **Host contrast**, with **Follow Windows** (default) and **Always high contrast**. Follow Windows retains the existing automatic behavior. Always high contrast affects the session host and its detached panels/monitors only; it does not change Windows or the viewer's skin/world rendering.

Windows High Contrast always takes precedence, including its chosen system colors. The effective host mode is `windowsHighContrast || hostAlwaysHighContrast`; neither dropdown choice disables an enabled Windows accessibility setting. When the host override is on while Windows High Contrast is off, use a dedicated high-contrast host palette with clear text, focus, selection and icon colors. Keep actual Windows contrast detection separate from the host override when choosing colors and native versus custom control painting.

Contrast takes precedence over the host's Midnight/Daylight/Follow Windows theme without overwriting that saved theme. Once neither Windows nor the host override requires contrast, resume the selected theme. The host-only choice participates in immediate Preferences preview, Apply/OK, Cancel and saved-choice persistence; existing settings migrate to Follow Windows. Changes made in Windows are external system state and are not reverted by Preferences Cancel. React to Windows settings/theme-change notifications and refresh every host surface and icon cache without requiring a restart or hover. The revision 2 mockup predates the host override; its contrast checkbox remains a simulation of the Windows setting.

## Voice ownership indicator

Place a read-only **Voice → character** indicator in the host status area and mirror it in detached character controls, so it remains available when those controls are on another monitor. It is independent of optional voice quick actions. Character cards and selector entries may add a small ownership mark with a full accessible description; the persistent named indicator remains the primary explanation. Preserve error/recovery messages when allocating status space. Long labels may shorten visually, while accessible text and tooltips retain the account and grid. Minimal mode must retain a route to the status through Show controls/recovery.

| Display | Required state |
| --- | --- |
| Voice: Off | Follow policy is off |
| Voice → Sodie | Follow policy is on and a live, managed, current-session active worker has acknowledged `VoiceOwner` |
| Voice: Switching… | Ownership handoff or a policy change is awaiting acknowledgement |
| Voice: Unavailable | Follow policy is on but there is no valid current acknowledged owner |

`VoiceOwner` currently means `permitVoice && ready() && inputGranted && mode == Active && !promoting` in `fssessionworker.cpp`. It is a permission/ownership fact, not a voice-service connection, microphone mute or speaking signal. Use **Voice assigned to Sodie** as the accessible description. Do not label it Connected, Mic live, or Speaking. A richer connection/microphone indicator would require separate viewer telemetry and protocol work outside this first batch.

Derive the indicator from acknowledged worker state, account/session identity and the handoff/policy lifecycle. Never assign it optimistically from the selected management slot, Chat sender, or a stale `active` index. Clear an old name on disconnect, detach, account replacement or uncertain ownership. Multiple/contradictory owners produce Unavailable, rather than picking one. Announce meaningful ownership transitions once, without repeating the status on each 100 ms poll. The native viewer remains authoritative for voice connection and microphone controls.

## Phase 1 Fix resizing before replacing icons

**Observed report:** icons remain corrupted after resizing until rollover. **Source evidence:** the hover handler in `theme.h` invalidates the full button; the main/detached resize handlers primarily move controls, and monitor layout invalidates its parent without an explicit all-child redraw. Partial invalidation or copied stale pixels is the leading hypothesis, not a confirmed native diagnosis.

1. Reproduce on the matching Windows host with the pointer parked away from controls. Resize through the label-collapse/overflow thresholds, both directions, including detached controls and Chat, Attention and monitors. Record before/after frames and identify which HWND retains stale pixels.
2. Batch changed child positions/sizes; avoid hide/show cycling for controls whose visibility does not change. Use `SWP_NOCOPYBITS` where preserving old client pixels is invalid. Keep painting deferred during the batch and perform one targeted parent/child invalidation at the end (`RedrawWindow` with `RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN`, or equivalent explicit child invalidation).
3. Ensure every affected custom control repaints its complete current client bounds, including background, border, icon, text and focus. Use current dimensions and SaveDC/RestoreDC around custom drawing. Invalidate on visual-state, icon, label, DPI and theme changes even when the logical toggle value is unchanged. An unchanged selected value must not suppress a size-related repaint.
4. Keep hover as a visual input, never as a prerequisite for repair. Do not add a perpetual repaint timer, unconditional full-window redraws on every poll, or viewer-renderer changes. Check that the fix avoids flicker and preserves focus, text selection and scroll positions.

Touchpoints: `main.cpp`'s `layoutCharacters`, `layoutChat`, `layoutAttention`, `layoutMonitorChat`, `layout`, and `WM_SIZE`/`WM_DPICHANGED` routes; `theme.h`'s `hoverProc`, `button` and control painting. Keep this change independently reviewable before adding Lucide rendering.

## Phase 2 Unify icons and accessible toggle state

Introduce one presentation model for a control's stable accessible name, current checked/expanded value, enabled state, visible label/value, icon and action tooltip. Feed it from host state; mirrored settings must share the same underlying fields. Add the independent permanent-label, optional audio-state-text and host-contrast preferences described above. Refresh main controls, quick actions, overflow/menu checkmarks and detached/monitor controls through the same mapping. Existing temporary `FolderstormToggle` properties cannot be the only representation of checked state.

Vendor the selected Lucide SVGs and their license notices at a pinned revision. The mockup uses revision `7b0196e249ca09ca27c584c497aca9a8cab8a7f5`; the native host retains PR #39's revision `440a1e1dfb1e712505308da46bead3c28b1f1398`, adding mic-off, volume-2 and pin-off from that same revision. Generate monochrome alpha masks ahead of runtime, tint with the active system/theme foreground, and draw through the existing DPI-aware helper. Retain source SVGs and the regeneration command; normal viewer builds and launches need no browser, Node, CDN, icon font or runtime download. Invalidate the raster/tint cache on the corresponding changes and verify less common DPI/text-scale combinations. Preserve crisp stroke weight and minimum hit areas rather than shrinking glyphs to make room.

Use native checked controls styled as push buttons for true toggles, with custom painting that retains native check semantics. Keep full accessible text separate from the drawn glyph/short caption; icon-only mode must not rename a button to a Unicode symbol or bare count. Prefer native MSAA/UIA exposure and state-change events, verifying the exact control class with Inspect/Narrator. Add a small accessibility adapter only for properties native controls do not expose. Do not assume `BS_OWNERDRAW`, a custom HWND property, or hover text communicates Toggle state.

Expose full names and associated labels for character/conversation selectors, composers and numeric fields. Preserve per-window Pin scope and docking's suspension of topmost behavior. Selected-state contrast, focus rectangles and a checkmark/text cue must remain readable in Midnight, Daylight, Follow Windows and Windows High Contrast; color alone is insufficient.

Touchpoints: `fssessionexperience.h` command metadata; `main.cpp`'s `quickLabel`, `layoutCharacters`, `setToggle`, `pinWindow`, `setMonitorChat`, audio handlers and Preferences field definitions; `theme.h`; shared control construction and `session-host.rc`/CMake resource integration. Include the Quick actions preview and overflow widths, not just the visible toolbar. Preserve users' action order, removals, pins and saved settings.

## Phase 3 Voice status and keyboard integration

Add a small derived voice-status helper with the ownership rules above and update it at meaningful lifecycle/policy transitions. The existing protocol 16 fields supply the assignment indicator; do not promise connection/speaking status without additional telemetry.

Audit logical Tab/Shift+Tab order across main, detached, Attention, Preferences and monitor windows. Space toggles the focused toggle; Enter activates the focused action or sends only from its focused composer. Keep existing Shift+Enter and IME-commit behavior. A collapsing/hiding/docking panel restores focus to a sensible visible control, preserving composer draft, selection and transcript position. Menu keys, Shift+F10 and Escape should work at the invoking control and return focus predictably. Position keyboard-invoked popup menus at that control, rather than an unrelated mouse location. Recovery remains reachable throughout a handoff.

Accessible names stay stable as captions collapse. Tooltips describe the next action and scope; accessible values describe current state. State announcements occur once per actual change. Expanded/Condensed automation must distinguish visible disclosure from saved preference without contradicting the density arrow. Preserve the existing viewer input, focus, held-key and microphone handoff protections.

## Acceptance and delivery

The PR groups these phases in the existing renderer, portable presentation helper, accessibility adapter and Preferences integration. Build and run the matching Windows host/viewer for native acceptance; portable helper tests and browser mockups cannot establish Win32 painting or Narrator behavior.

| Area | Required evidence |
| --- | --- |
| Repaint | Continuous grow/shrink with the pointer parked elsewhere; no ghost/doubled/clipped icons after paint completes. Repeat docked/detached and monitor/Attention surfaces, label collapse/overflow, minimize/restore and DPI/theme changes; no hover needed. |
| Toggle consistency | Mouse, Space, Preferences Apply/Cancel, overflow/menu actions and restart all agree on icon, checked state and value. Specifically cover the BG Audio inversion and repeated Apply. Text-state suffixes default Off without removing accessible toggle values. |
| Shared settings | Permanent-button labels and quick-action labels change independently. Accessibility mirrors Appearance, Switching and Keyboard shortcuts through one draft; edits in either location, repeated Apply and Cancel keep every view synchronized. Resize preserves selections; migration retains old quick-action labels. |
| Audio scope | BG Audio changes only inactive viewer audio policy, respecting native mutes; host alerts and active native audio remain governed by their existing settings. Voice-follow toggles do not change the Chat sender. |
| Voice ownership | A active/B sender, switch A to C, delayed acknowledgement, follow off/on, logout/disconnect/detach, stale/replaced session, no active worker and contradictory flags. Never show an unacknowledged owner or claim live transmission. |
| Keyboard and assistive technology | Inspect/Narrator expose full names, toggle values and expanded state; state changes announce once. Logical focus route, context menus, Escape, docking/hiding and retained Chat drafts/selection; Enter/Shift+Enter/IME regressions checked. |
| Presentation | 100/150/200% DPI, increased Windows text scale, long account names, minimum supported widths, both densities, all label modes, Midnight/Daylight/Follow Windows/High Contrast. Labels and focus indicators fit; error messages and voice ownership remain reachable. |
| Contrast controls | Open the Windows settings link and handle launch failure. Exercise both host choices with Windows High Contrast on/off, including custom system colors, live system changes, detached panels and monitors. Verify immediate preview, Apply/restart persistence, Cancel, icon repaint and restoration of the saved theme. The host override changes no desktop setting or viewer skin, and Cancel does not undo a change made in Windows. |
| Compatibility | Existing settings load and retain meaning; no lost quick actions or new forced defaults. No protocol change for assignment-only voice status. Existing profile isolation, saved-choice transactions and host recovery remain intact. |

Meaningful automated additions should exercise the shared audio-state inversion and stale/contradictory voice-ownership cases, plus existing toolbar layout limits after icon/text measurement changes. Use native UI automation for checked/accessibility properties and resize screenshots for the reported paint regression. Do not substitute tests that only repeat icon-name constants for native evidence.

## Mockup verification

The HTML is self-contained and uses embedded Lucide SVG symbols with local license notices. It demonstrates the proposed copy and state changes; actual Windows pixel rendering, accessibility APIs and resize behavior remain native acceptance work. The illustrated owner state is explicitly simulated, and no control connects to viewer accounts or sends messages.

Revision 2 browser checks passed for independent permanent-button/quick-action label modes, absent character viewport badge, audio text states hidden by default, shared Accessibility/original-tab values, Apply/Cancel baselines, Space activation, audio icon/check-state agreement, independent character and Chat-sender selection, voice scenarios, separate drafts and scoped Pin/docking. Toolbar bounds were checked at 600/800/1240 px without changing either label choice. Midnight, Daylight, Follow Windows and a High Contrast sample were exercised. The toolbar, Accessibility and mixed-label previews were rendered; the mockup produced no JavaScript errors or network requests. These browser results validate the proposal artifacts; native implementation evidence is recorded separately in the validation document.
