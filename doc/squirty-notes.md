# Squirty Notes

Windows desktop squirts for incoming resident IMs and friends coming online.
Both notification modes start Off.

1. Run Squirty Notes 1.2 or later. In its Settings, enable **API + MCP**,
   click Apply, then Copy token. Keep Squirty Notes running in its tray.
2. In Folderstorm, open **Preferences > Notifications > World > Squirty Notes**.
   Paste the token and use the same port as Squirty Notes (default 47831).
3. Use **Check connection**, or **Test squirt** to request an actual overlay.
   Tests use the entered connection fields without saving them.
4. Choose Off, All, or Custom independently for incoming IMs and friend logins.
   With Custom, type a resident name and click Add, then confirm the person
   in the resident picker. Friend logins require a current friend.
5. Click **Apply** in this window to save. Closing without Apply discards edits.
   These settings have their own Apply and are independent of the preferences
   window's Cancel button.

**Squirt again after silence** defaults to 120 seconds and accepts 0 through
86400 seconds. Each accepted live incoming IM resets that resident's timer,
even while IM squirts are Off or Do Not Disturb is active. A squirt occurs
on their first IM, and on an IM after at least the configured silence period.
There is no delayed squirt when the timer expires. Your outgoing replies
do not reset the timer. Zero squirts for every eligible incoming IM.

All IMs means direct resident IMs, including nonfriends allowed by your chat
preferences. Group chat, object messages, typing, system messages, and saved
offline messages do not squirt. Existing mute, friends-only, and RLVa receive
restrictions apply. Automatic squirts respect Do Not Disturb and work while
Folderstorm is focused. Presence squirts require an offline-to-online change;
login state, unknown initial presence, and duplicate updates stay quiet.

Modes, custom UUID lists and the inactivity interval are account/grid settings.
The port is local viewer settings; the token uses the viewer's protected
credential store, separate from ordinary settings XML. Names are labels;
UUIDs determine who matches. Activity timestamps reset at login and are not
written to disk.

The viewer sends short name-only notices to `http://127.0.0.1:<port>/v1/squirts`
as JSON with bearer authentication. Requests bypass viewer and environment
proxies, disable credential trace logging, redirects and automatic HTTP retries,
and time out after two seconds. Squirty Notes supplies the saved recipe and
monitor selection. HTTP 202 means accepted into its queue, not proof that an
overlay was displayed. The app can pause reminders or expire queued notices.

The viewer has a bounded, short-lived delivery queue. A failure drops pending
notices and backs off automatic requests for 30 seconds. It never replays old
messages when Squirty Notes resumes. Manual connection tests remain available.

Run the standalone `squirty_policy` regression target for mode selection,
independent resident timers, continuous conversation, timeout boundaries,
suppression, presence transitions, reset and token validation. Native acceptance
should also check a real overlay, token rotation, API-off/paused states,
Custom selection, and continued responsive chat while Squirty Notes is stopped.
