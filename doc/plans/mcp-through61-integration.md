# Combined shared-pose and MCP integration

This follow-up starts above #61 and retains the existing feature histories. It
provides one source tree for the next native build; it does not merge the open
PRs on GitHub or establish native build/runtime acceptance.

## Pinned inputs

| Input | Verified head | Retained behavior |
| --- | --- | --- |
| Main | `85e42ffe615974b1adce9f2584ee18c914e5da2b` | Current native viewer changes |
| #48 | `015511a1dfc49a6652b76345f2b2992b66c888c8` | Electric Jungle unread/mention contrast |
| #49 | `212ec9c87f9d709056a7f303f436ff6e2609ed1a` | Immediate local pose previews and complete queued gestures |
| #50 | `a5ef5eb7aa5e4a4437e714179745b148b40ddf1e` | Accessible host controls and shared Accessibility drafts |
| #61 | `9209788e295bcf62d790172b793194ad7e7a48d0` | MCP expansion, source ownership, native/manual safety and test corrections |

Build the published integration head directly. These pinned feature heads are
already incorporated; reapplying their patches is unnecessary. Newer changes
to those branches need a separate integration review.

## Shared posing

Each queued native gesture retains its original source, session, grant and
lease. Only the front gesture reserves, renews or submits an edit. Local
previews remain immediate; owner-canonical replay must preserve valid newer
previews. Source changes cannot rebind old actions or restore old previews.

Native transport and MCP operations have separate ownership. Delayed native
submission needs its original current binding just as MCP packets need their
captured assistant authority. Rejecting a drag discards its queued work and
requires release before retry; cleanup must not cancel unrelated assistant or
native work. A reply for an old lease cannot finish a newer gesture or roll
back observed joint revisions. Legacy unversioned lock replies still wait for
a matching canonical observation under the original bounded request lifetime.

Native drag and Undo/Redo take over received motion ownership. Later MCP
cancellation, source loss or sharing cleanup must preserve that manual work.
Native consent, joint reservations, privacy checks and Preferences acceptance
remain authoritative.

## Host Preferences and test registration

Accessibility, Advanced, Local Assistant and MCP controller pages retain
distinct page indices and appropriate sidebar categories. Accessibility mirrors
edit their original fields' draft; controller permissions commit only through
validated Apply/OK. Cancel discards both drafts and resets visual previews.

Standalone tests retain the union of controls/accessibility, assistant feature,
controller model/dispatch, content and native Windows HWND/MSAA registrations.
The four content targets retain their viewer include directories.

## Native acceptance still required

- Build matching viewer, sidecar and host from the integration head.
- Drag while reservation replies are delayed; release and rapidly re-grab.
  Confirm immediate previews and complete ordered gestures without bursts or
  older replies overwriting the newest preview.
- Change source/grant/session with a front and tail queued; deny a reservation
  or let it time out while bridge packets wait. No stale replay or submission
  may occur; release must be required before retry.
- Delay, duplicate and reorder locked/ACK/snapshot traffic, including an older
  native peer. Preserve monotonic canonical state and per-gesture ownership.
- Interleave native edits/Undo/Redo and MCP operations, then revoke, disable
  sharing, change region or end the session. Preserve unrelated manual motion
  and independently owned work.
- Visit Accessibility, Local Assistant and MCP controller pages; test shared
  field mirrors, preview, Apply/OK and Cancel. Check controller permissions
  remain independent of viewer permissions.
- On Windows, run native HWND/MSAA checks and manually check focus, keyboard
  navigation, contrast, DPI and resize repaint. Portable/source checks do not
  replace those tests.
