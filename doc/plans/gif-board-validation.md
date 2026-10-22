# GIF Board / My GIFs checkpoint

Second checkpoint above #85. Native UI, Windows locking and runtime acceptance
remain pending; no installer, VPS change or release is included.

- Native Nearby/IM/toolbar composers have a GIF button beside attachments.
  The compact picker becomes a larger resizable viewer floater using Pop out.
  One board keeps search/library state when switching size. World → GIF Board
  opens browsing/copying without silently choosing a chat target.
- Selection, Insert/double-click/Enter, preview, saved links, favourites,
  collections, title/tags, import/export and removal use native UI operations.
  Insert checks the captured editor/account/grid/login/channel/generation and
  current native restrictions; it never sends. A closed private target cannot
  become Nearby. Opening from another composer explicitly retargets the board.
- My GIFs stores version-1 bounded XML in account settings, outside installation
  files. Writes reread/merge under an OS-released lock, preserve a validated
  backup and atomically replace through same-directory temps. Bad/future formats
  remain untouched; a valid backup can be shown read-only. Import/export contains
  bookmarks and labels, not media or credentials. Re-saving a URL preserves its
  custom labels and favourite state.
- Confirmed GIF uploads save a bookmark by default; review can disable saving.
  Failed/cancelled/processing uploads do not add entries. Saving a chat GIF uses
  its image context menu. Bookmarks do not extend Zipline retention or imply
  anonymous server-folder ownership.
- Grid cells are virtualized. Only visible/near-visible rows have controls;
  offscreen image views release subscriptions/textures. Board sources decode to
  at most 320px and use the shared four-preview-transfer service and #85's bounded
  frame workers. Upload transport retains its independent single-job limit.
- Default autoplay is at least 80% geometrically visible, with a short dwell.
  Options include hover/selection and Manual. Explicit Play works with Manual
  or reduced motion; explicit Pause beats autoplay. Hidden/minimized boards do
  not animate. Board options commit through Preferences Apply/OK; incoming-chat
  media consent/autoplay stays independent.
- KLIPY remains unavailable until the next checkpoint supplies/configures its
  provider. Sources are never combined into one search grid.

Checks: 14 focused CTest entries pass, including bookmark, URL/provider, capacity,
XML bounds, clipping/80%, source identity and production composer/service/card
bodies at stubbed boundaries. Linux syntax passes for board/editor, library,
uploader, media and registration. XML parses and diff checks pass. No broad
viewer build, native Windows UI or GL run.

Windows acceptance:

1. With toolbar hidden, open GIFs from normal Nearby, IM, group and conference.
   Test compact/pop-out/keep-open, resize, skins/themes, DPI and keyboard.
   Attachment/GIF buttons must fit without repeatedly shrinking the composer.
2. Save/add/edit/favourite links and collections, import/export, restart/upgrade.
   Test expired URLs, duplicate saves, concurrent same-account viewers and
   corrupt/older/future XML preservation. Recovery from a corrupt main file is
   read-only until its saved backup is restored outside the viewer.
3. Upload GIFs with save enabled/disabled, automatic uploads, cancellation,
   server processing, logout and changed recipients. Unconfirmed/stale links
   must not enter another account's library.
4. Check 79%/80%, both clip axes, manual pause/play, hover/selection, reduced
   motion, minimization and scrolling many GIFs. Incoming autoplay stays unchanged.
5. Insert into drafts with captions, selection, caret changes and undo. Close
   the private destination or change login/channel/restrictions while browsing:
   Insert refuses stale targets and browsing/Copy link remains available.
