# UI lifetime batch

Base: main `e462dd1d1f`. Independent of #163 and other upstream batches.

Plan: retain weak IM floater handles, disconnect login focus callbacks before
child destruction, give concurrent directory-picker listeners distinct names,
and give tab insertion enums their intended signed S32 range. Preserve native
selection, chat media/upload integration and session-host behavior.

Sources: SL `fd2040cf169e416f855d914c704179a2ddcf43e7` and
`89a5c6d0038bd18d7d6b713da92d5032fe8ff0a8`; Firestorm
`88ef9b00a56037b4f9072dc0a872dbf4a936169f` and
`aa72e4d9494b285428d4233a8d7cdb31fafde825`.
Ancestry/stable-patch-ID checks against main and #32/#66 found no duplicates.
Adapt the IM patch to Folderstorm's existing ad-hoc-session update path rather
than replacing that path with the newer upstream implementation.

Validation: four focused checks pass (three compiled native method/snippet
adapters and one directory-listener source contract). The callback check uses
real Boost signals; set `FS_UPSTREAM_BOOST_INCLUDE` to a Boost include root if
not installed system-wide. Syntax checks pass for the IM container and login
panel with real viewer headers. The directory-picker Linux syntax check is
blocked by existing unavailable GTK picker APIs in this harness configuration;
the changed Windows listener registration is source-reviewed, not compiled.
The weak-handle adapter models native handle expiration; it is not a native UI
or Windows dialog test.

Native acceptance: concurrent folder pickers; focus changes during login-panel
destruction/logout/relogin; close/reopen and ad-hoc ID replacement of IM tabs
while messages arrive; tab insertion/rearrangement and chat upload/media controls.
Full viewer/host builds and native runtime acceptance remain unrun.
