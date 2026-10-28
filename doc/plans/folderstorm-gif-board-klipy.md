# Folderstorm GIF playback, KLIPY and My GIFs

Status: approved implementation in progress. Prepared October 22, 2026.
Playback is checkpoint #85; the board/library checkpoint follows above it.

Scope: native viewer Nearby, direct IM, group and conference composers, including
the normal Conversations window and toolbar Nearby. Host Chat remains deferred.
Preserve the fixes through #84; start implementation above its latest head while
unmerged, or current main after the stack merges. Do not rewrite published history.

## Product behavior

Add a GIF button beside native chat's attachment controls. It opens a compact
picker with a Pop out button for a larger, resizable GIF Board viewer floater.
Both use the same reusable panel, search state and library. Popping out transfers
the panel rather than fetching a second copy of the results. The board can stay
open while chatting; restore its size, position, last tab and thumbnail size.
This means a separate viewer floater, like the gesture board, rather than a new
standalone Windows process.

Top-level tabs:

- **KLIPY:** Search, Trending, Favourites and Recent within this provider's tab.
- **My GIFs:** saved external links and the user's own uploaded GIFs, with local
  text search, favourites and user collections.

Keep controls short: search, thumbnail size, pop out/pin, selection and Insert.
Use tooltips for source, title, duration, playback and failure details. Show a
small destination line, for example “Sodie → Maddy” or “Sodie → Nearby · channel
0”, so the persistent board's insertion target remains obvious.

Click or arrow keys select a tile. **Insert** or double-click inserts its link
into the bound draft; it does not send chat. Space toggles playback on the
selected tile. Right-click offers preview, favourite/save, copy/open link and
source-appropriate reporting/removal actions. Escape closes the compact picker;
it does not clear a draft or reset a pinned board's library.

## 1. Replace the 120-frame rejection with bounded playback

### Confirmed cause

`fschatmediapolicy.h` sets `MaxFrames = 120`. In `fschatmediacodec.cpp`, the GIF
scanner returns failure above that number before deciding whether only a poster
is needed. WebP rejects the same way. Consequently, even an upload's
`decode(..., firstFrame=true)` can fail on an otherwise valid long animation.
The existing animated GIF path also asks stb to allocate all source-sized frames
before scaling them. Raising the constant alone would magnify that allocation.

### Decoder design

Split static/poster decoding from an **AnimationSource + AnimationPlayer** API.
The source owns a bounded immutable encoded snapshot and checked metadata. A
player advances sequentially on a worker, holding its compositor state, the
current frame and a small ready-frame queue. The UI receives scaled frames and
updates an existing texture; it never decodes during draw or retains one GPU
texture per frame.

Use a pinned, decoder-only giflib dependency through its incremental record and
scanline APIs, with a tested Folderstorm compositor. Keep stb for PNG/JPEG and
reuse WebP's existing sequential animation decoder. Avoid tying the new player
to private stb internals or its whole-animation allocation function. Select and
audit the exact maintained giflib release during the first implementation
checkpoint; add its licence/pin to the existing dependency manifest. No runtime
download or external converter is required.

Preserve transparency, local palettes, partial-frame rectangles, interlacing,
disposal methods, finite/infinite loop counts and valid frame timing. In
particular, restore-to-previous needs an independently owned prior canvas; no
pointer may refer into a reallocated frame array. GIF and WebP expose the same
player interface even though their decoder/compositor internals differ.

Remove 120 as a content-validity rule. Retain generous abuse bounds, initially
10,000 frame records and ten minutes per loop, alongside the existing 15 MiB
download and 16-megapixel source limits. These are safety ceilings, not a request
to materialize that many frames. Checked duration arithmetic, metadata parsing
and cancellation apply before and during expensive work. If full playback is
too costly, a validated first frame still loads, with Open original available.
Poster generation stops after a valid first image and does not require a full
animation count or scan to the file's end. Invalid first-frame data still fails.

Decode the entire normal animation over time, rather than truncating it after
120 frames. Downscale presentation frames to the requested tile/preview size.
If decoding cannot keep up, reduce presentation updates while processing required
compositor dependencies; never jump directly to a delta frame. Hold the last
valid frame during a short underrun and fall back to a poster on sustained budget
failure. Do not change the uploaded original or the link being shared.

### Resource ownership

Give media services shared reservations for encoded bytes, decoded frame queues,
decoder scratch space and GPU allocations. Start with the current 64 MiB retained
pixel budget plus separately bounded 64 MiB scratch, 64 MiB encoded and 32 MiB
GPU pools per viewer process; tune only against measurements. Count padded texture
extents and every compositor/queue allocation, not just nominal thumbnail pixels.
Oversized source canvases may receive poster-only fallback even below the
metadata limits. Do not increase pools for each board or card.

Use a small fair worker pool and the existing four-transfer process limit.
Uploads and explicit previews take priority without starving visible tiles.
Identical eligible sources can share read-only source data and playback frames;
independent pause state remains a view choice. KLIPY's retention rules below
override general caching. Unsubscribe/cancel on source changes and release
offscreen textures through idle maintenance, preserving #80's cleanup fix.

## 2. Autoplay defaults and visibility

**GIF Board default: autoplay when at least 80% of a tile is visible.** The default
is not hover-only. Offer **Visible tiles**, **Hover or selection only**, and
**Manual** under Chat → Media → GIF Board. An advanced percentage setting may
adjust the default threshold; keep the visible default 80%.

Measure visible area divided by tile area after intersecting all ancestor clips
and the viewer window, not merely whether the rectangles overlap. Exclude
collapsed/hidden tabs, hidden floaters and minimized windows. This is geometric
visibility inside the viewer; do not claim to detect arbitrary OS-window
occlusion. Start after a short stable-visible delay to avoid scroll-boundary
thrashing; pause below the threshold and on explicit per-tile Pause. Do not
override a user's pause when the tile becomes visible again.

Autoplay uses the returned small animation rendition where available. All
eligible visible tiles participate in fair scheduling rather than keeping the
existing hard three-animated-card limit. Under pressure, lower presentation FPS
or use posters for lower-priority tiles; selected/hovered and explicit large
previews win. Virtualize the grid, reuse GPU textures and stop work offscreen.
Reduced-motion settings select still playback until the user explicitly plays
or chooses an override.

Board playback preferences are independent of incoming chat media settings.
Opening a GIF Board deliberately loads its visible results; it does not enable
automatic external downloads or animation in incoming chat history. Existing
saved incoming-chat choices keep their current meaning.

## 3. KLIPY integration

Use the native GIF API directly from the viewer for Search, Trending, Categories,
Recent, resolving saved slugs through Items and recording actual Share events.
Implement an isolated provider adapter, with typed replies, bounded JSON and
request lifetimes. Search on Enter or a short debounce; maintain one current
query generation, paginate incrementally and discard old completions. Respect
429/Retry-After, backoff and unavailable-service states without clearing drafts.

Use only format URLs returned by the API, choosing a small GIF rendition for
tiles and an appropriate returned share rendition for insertion. Preserve the
URL, query parameters and identifiers byte-for-byte. Validate scheme, address,
redirects, content type, dimensions and size before display; do not reconstruct
URLs or invent CDN paths. If an item cannot render, keep an error/poster tile in
its original slot rather than silently filtering/reordering the result set.

Keep Search/Trending in provider order. My GIFs is a distinct source tab; no
combined local/KLIPY search grid. Use **Search KLIPY** in the provider search field
and a small visible provider attribution. Content filters and ads-off selection
are configured in the Partner Panel; do not strip advertisements client-side
if the key was configured differently. A response contract mismatch must be
handled explicitly instead of pretending an ad is a GIF.

No KLIPY media uploads to Zipline, persistent media downloads, server proxy or
partner-operated cache. Keep only the transient buffers needed to display an
active result, and release them with its subscribers. Persistent favourites store
provider + slug and optional user labels, not image bytes or stale delivery URLs;
resolve and validate them again when opened. Confirm this client-buffer/favourite
design with KLIPY before public rollout, given its documented custom-cache rules.

Generate an opaque random customer identifier for each account/grid's local
integration; do not send avatar UUIDs, usernames, chat text, recipient identities
or account tokens. Explain external search/media requests briefly on first use.
Provider search terms and required interaction data are transmitted intentionally;
do not copy them into viewer logs, crash attachments or unrelated telemetry.
Resetting this identifier also resets any provider-linked recents state.

Queue a Share interaction only after the ordinary native send path accepts a
message containing that selection. Selection, preview, saving and draft insertion
are not shares. A local send-accepted callback is not a server delivery receipt.
Bound/deduplicate interaction retries according to the API contract; do not
repeat a non-idempotent event blindly after an ambiguous timeout. Confirm whether
client-side view events are required for GIFs before wiring them; do not invent
ad/impression callbacks.

KLIPY key handling: supply an approved public desktop application key in release
configuration, never a privileged partner/dashboard credential. Treat a shipped
desktop key as recoverable, not secret. Keep optional developer override out of
source control and normal screenshots. If approval/key is absent, My GIFs still
works and the KLIPY tab explains that search is unavailable.

## 4. My GIFs

Add links via **Add link**, **Save to My GIFs** on an animated chat preview, or a
successful GIF upload. Add a **Save to My GIFs** choice to the existing upload
review; default it on for a recognized animated image. Do not add failed,
cancelled, still-processing or unconfirmed uploads. Local GIF files use the
existing explicit public-upload flow before they become shareable bookmarks.

Store stable bookmark ID, public source/share URL, optional title/tags,
collection, favourite flag and timestamps. Show searchable collections and
favourites, with rename, move, remove and copy/open actions. Preserve the
original share URL; use the existing sharing-site resolver to obtain a preview.
Recognized KLIPY links belong in KLIPY Favourites and never become rehosting
candidates. A deleted/expired private link remains an unavailable bookmark the
user can replace or remove; no repeated automatic upload or retries.

Store this small versioned metadata library per account/grid in stable user
settings, outside installation directories, with atomic saves, a last-good
backup, validation and bounded import/export. Use virtualized/paged results so
there is no gesture-board-style 64-item usability limit. Start with a generous
10,000-bookmark / 4 MiB metadata ceiling, with a clear capacity message rather
than silent eviction. Export/import moves bookmarks and user labels, not media
files or credentials. Multiple processes writing the same account library must
merge under a lock rather than overwrite each other's edits.

Saving a link does not guarantee permanent hosting. Zipline files remain subject
to the instance's expiry/quota policy; removing a bookmark does not delete the
public upload. Put this detail in the upload/library tooltips. No anonymous
server enumeration or per-user ownership is inferred from the shared folder ID.
Users see only links they explicitly saved or successfully uploaded locally.

## 5. Composer and lifetime safety

Reuse source binding from native uploads: editor handle, account, grid, login,
conversation, Nearby channel and binding generation. Capture a target when
opening the picker. A popped-out board keeps that target until explicitly
retargeted by opening it from another composer; changing viewer focus alone must
not redirect insertion. Global menu opening starts with no destination and offers
Copy link until the user chooses a live native composer.

On Insert, recheck native restrictions, current conference participants, text
capacity, editor lifetime and source ownership. Use native insertion/undo at the
current caret, preserving captions/selections by normal editor rules. Never fall
back from a closed/disconnected private conversation to Nearby. If the target is
gone, keep browsing/copying available and require explicit retargeting. Changing
account/grid/login clears provider requests and transient results; late callbacks
cannot restore an earlier library, insert text or record a share for another
session. Closing a board releases its subscribers without cancelling another
still-valid view's work.

## 6. Delivery order: three reviewable PRs

1. **Long-animation playback foundation.** New incremental API, pinned GIF
   decoder, poster/upload fix, WebP parity, budgets, cancellation and disposal/
   timing regressions. Integrate the player into existing native image previews.
2. **GIF Board and My GIFs.** Reusable compact/pop-out panel, native composer
   insertion, local library/collections, save-upload integration, visibility
   autoplay options and persistence. Complete this without needing a KLIPY key.
3. **KLIPY provider.** Search/trending/recents/favourites, rendition selection,
   attribution, metadata refresh and accepted-send interactions. Add fixtures and
   configuration instructions. Production enablement awaits the provider setup
   below, rather than blocking the independent playback/library work.

Use focused tests and affected-source checks; no broad viewer builds, packaging,
GitHub builds, VPS changes or live uploads are needed for implementation.
Windows runtime acceptance follows the ordinary user build/test flow.

## 7. Acceptance checks

- GIF/WebP fixtures at 119, 120, 121, 300, 1,000 and several thousand frames;
  poster extraction/upload preserves long originals. Test disposal 0–3,
  transparency, partial/interlaced frames, local palettes, loops, timing overflow,
  malformed late frames, metadata/byte/dimension ceilings and cancellation.
- Long playback and looping stay within measured CPU/GPU/encoded/scratch pools.
  Open repeated URLs in chat, picker and pop-out, scroll several pages and close
  views; no per-frame/per-card retained texture accumulation or orphan jobs.
- At 79% visible a tile pauses; at 80% it becomes eligible. Test horizontal and
  vertical clipping, DPI, resize, hidden tabs, minimized viewer, manual Pause,
  reduced motion and all playback modes. Under load, scheduling remains fair.
- Search A then B with A returning last; pagination, throttling, invalid replies,
  missing formats, withdrawn favourites, exact URL preservation and provider
  order. No query/cache sharing across account/grid/login generations.
- Select/preview/insert/save/cancel emits no Share; accepted send emits one.
  Edited-away links, rejected sends, unrelated manual pastes, retry ambiguity and
  logout cannot produce a false or duplicate provider interaction.
- My GIFs survives restart/upgrade, corrupt/older metadata, concurrent writes,
  duplicate bookmarks and large libraries. Expired links stay manageable;
  library removal and public-file deletion remain separate operations.
- Normal Conversations and toolbar Nearby, IM, group and conference; compact
  picker/pop-out, Unicode queries, Enter/Space/Escape, text undo, themes/skins,
  larger text and scaling. Explicitly test a closed private target never becoming
  public Nearby and a board opened from a second character requiring retargeting.

## Provider setup and open dependencies

No user choice is required to complete the plan. Before shipping KLIPY search:

1. Create a desktop application in the [Partner Panel](https://partner.klipy.com/)
   and obtain a test key, then request production access. KLIPY documents a
   100-request/hour test limit. Confirm production terms/cost and public desktop
   key distribution for Folderstorm rather than assuming “unlimited” means a
   particular commercial arrangement.
2. Confirm transient native playback buffers, stored favourite slugs and share
   tracking with KLIPY; obtain approval for any requested departure from its
   standard direct-client delivery rules. No provider contact is sent by this
   planning task.
3. Configure content filters and disable optional ads in the Partner Panel.
   Record the approved key/branding/API contract for release configuration.

## Sources checked

- [KLIPY integration requirements and API overview](https://docs.klipy.com/):
  direct client requests, returned URLs, result ordering and separate source
  grids; custom caching requires prior approval.
- [KLIPY API terms](https://klipy.com/support/api-terms): provider content storage
  and source-combination limits. Treat the current integration requirements as
  the stricter design baseline; do not rely on a general thumbnail exception to
  justify a new provider cache.
- [KLIPY GIF API](https://docs.klipy.com/gifs-api): Search/Trending, Items by slug,
  Recent and Share. Endpoint details need a captured current schema/fixture at
  implementation time; parts of the documentation render inconsistently.
- [KLIPY developer setup](https://klipy.com/developers): testing/production access,
  attribution, Partner Panel controls and optional advertisements.
- [GIFLIB decoder reference](https://android.googlesource.com/platform/external/giflib/+/142126bb961e118aeba73478f07c1f198f1308bf/doc/gif_lib.xml):
  sequential record/scanline decoding. The implementation must pin and audit a
  current upstream release, not the older documentation snapshot.

Repository evidence: `fschatmediacodec.cpp`,
`fschatmediapolicy.h`, `fschatmedia.cpp` and `fschatupload.cpp` in the #84 tree.
Implementation and validation progress are recorded in the checkpoint documents.
