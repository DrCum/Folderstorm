# Character quick actions

In Host Preferences → Controls → Quick actions, assign **Character 1–5** to
any toolbar position. Choices include the saved profile name, such as
“Character 2 · Sodie”; buttons show the profile name. Apply/OK accepts and
autosaves; Cancel discards changes.

- A ready managed character switches through the existing handoff path.
  Clicking the active character uses the existing foreground behavior.
- An unused/stopped profile opens its normal login, retaining the existing
  first-profile setup, settings-copy and private-profile checks.
- A running login/disconnected/detached/recovering viewer is brought forward
  when its verified native window is available. Repeated clicks never start a
  second viewer. While no surface exists yet, the host explains that it is
  opening. Pending restarts, handoffs, setup, closing and detachment retain the
  existing guards.
- Targets are saved profile slots, independent of the current management
  selection, active character or reordered character cards. Existing quick
  actions and saved positions keep their IDs and behavior. No settings format
  or IPC change is needed; configured positions are stored in host preferences.
- Full buttons use the existing Lucide icon and name. Compact/icons-only
  presentation uses the slot number, so the five shortcuts remain distinct.
  Buttons use the character accent and mark the active character. Tooltips and
  accessibility names retain the full identity/action. Overflow lists retain
  the names and dispatch to the same profile action, even if the unrelated
  management-selected character changes while the menu is open.

Validation: seven focused Release CTest checks pass with assertions enabled:
production character-action controller bodies at Win32 boundaries, experience
serialization/reordering, controls, icon coverage, preferences, lifecycle and
toolbar sizing/overflow. The controller checks cover empty and ready profiles,
repeated clicks on login, unavailable windows, pending restarts, disconnected,
detached/recovering viewers and closing/busy guards. `git diff --check` passes.
No native Windows build/runtime, broad viewer build, packaging, GitHub build or
unrelated tests were run.

Windows acceptance:

1. Add all five profile actions alongside an existing action. Apply, reopen
   Preferences and restart the host; verify order/targets. Cancel an edit and
   confirm the saved choices stay intact.
2. Click an unused slot, repeat while login starts and after its window opens,
   then log in and switch between ready characters. No duplicate process should
   appear. Verify hosted/separate viewers and clicking the active character.
3. Reorder cards, rename a profile and change management selection; shortcuts
   must retain their profile targets and names. Check colors/active indication,
   hover help, accessibility, large text, DPI and themes.
4. Test full/text/icons-only labels, narrow overflow and detached controls in
   Expanded/Condensed modes. Compact slots should read 1–5 and overflow should
   retain friendly names. Retry after a switch completes to confirm re-enabling.
5. Exercise disconnect/recovery, pending restart/cancellation and logout. A live
   viewer must be shown rather than duplicated; a stopped profile opens login.
