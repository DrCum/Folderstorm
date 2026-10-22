# KLIPY checkpoint

Third draft checkpoint above #86; includes #85's streaming GIF playback and
#86's native GIF Board / My GIFs. No host Chat integration, installer, release,
full viewer build or provider account changes are included.

## Setup

Use a **public desktop application key**, never a KLIPY partner/dashboard
credential. A key shipped in a desktop viewer is recoverable. No key is
committed. Configure the application for **ads off**: the native grid cannot
render ad HTML; an unexpected ad response produces an explicit error and the
response is not displayed as a filtered/reordered GIF grid.

For local testing, set `FOLDERSTORM_KLIPY_APP_KEY` in the environment that starts
the viewer. Alternatively enter an override in Preferences → Chat → Media →
GIF Board and Apply. This override stays outside normal settings backup and
is masked in Preferences. Leave it empty for the environment/build default.
Restart after changing an environment variable.

Release builds can pass `-DFOLDERSTORM_KLIPY_APP_KEY=<desktop-app-key>` to CMake.
The resulting definition is generated in the build directory. A build with no
key still supports My GIFs and explains that KLIPY search is not configured.
Do not paste credentials in issue reports or command output.

The [KLIPY developer setup](https://klipy.com/developers) describes testing
access (currently 100 requests/hour) and production approval. Before public
rollout, confirm this implementation's transient native display buffers,
stored favourite slugs and send-accepted events with KLIPY, as described in
the [approved plan](folderstorm-gif-board-klipy.md). No provider was contacted
on the user's behalf.

## Behavior

- My GIFs and KLIPY remain separate tabs, with separate search text. Provider
  browsing includes Trending, Search, Recent, categories and Favourites.
  Search/Trending preserve returned order, unavailable items retain their slots,
  and media URLs/queries remain unchanged. Selecting or previewing never sends.
  Tile right-click exposes source actions. Arrow navigation moves actual focus
  with selection so Enter/Space cannot act on the formerly selected GIF.
- A KLIPY favourite stores only its provider, slug and label. Opening Favourites
  resolves eight IDs at a time. My GIFs does not accept KLIPY media URLs as local
  uploads/bookmarks. Provider favourite/remove/report actions live in the board's
  Options menu; incoming chat URLs cannot be converted into guessed provider IDs.
- No disk media cache, rehosting, Zipline upload or server proxy is used. Media
  transfers go from the viewer to provider-returned HTTPS URLs through the bounded
  preview service. Display buffers release with their subscribers, including
  virtualized cells and independently opened previews. Provider URLs/results are
  cleared on closing the board, changing source or changing account/login/grid.
- A random local customer ID is bound to the account/grid but only the opaque ID
  is transmitted. Copied profiles regenerate it for a different owner. Requests
  send search queries and the necessary provider IDs, never avatar UUIDs, recipient
  identities or chat bodies. No API URL/key/query is logged by this integration.
- Two API work slots include cancelled-but-unfinished requests. Searches debounce
  and coalesce; stale results cannot replace the latest source. Responses are
  bounded, API redirects are refused, and 429 responses enforce Retry-After
  backoff. Failed More requests retry the same page. Result rows and interaction
  queues have fixed caps. Configured HTTP proxies disable external previews and
  KLIPY, consistent with the existing image service.
- Share events follow a scoped native composer send that contains the inserted
  GIF URL, after native restrictions/filtering. A preview, favourite or insertion
  does not count. Different editors/recipients, stale targets, removed/filtered
  URLs and programmatic sends cannot consume a pending selection. Events are
  bounded, deduplicated and best effort, without automatic retries after ambiguous
  POST failures. Native send acceptance is not a delivery receipt. Queued IMs
  that finish initialization later may omit an interaction rather than guess its
  former draft/source. Reports are explicit and described as queued, not delivered.
- Apply commits the provider key/enable choices; Cancel leaves the active client
  configuration alone. Changed configuration cancels old work; re-enabling an open
  provider tab restarts its current search. My GIFs remains usable independently.

## Validation

Sixteen focused CTest entries pass in Release with assertions enabled. Coverage
includes real provider-shaped JSON, exact URL preservation, request encoding,
ad/invalid/oversized responses, 429, late/stale responses, account/login/grid
ownership, held cancelled work slots, pagination failure/retry, bounded results,
and native composer send tracking. The controller/acceptance tests compile the
production bodies with only viewer/network boundaries replaced. Existing codec,
animation, board, upload, media service/presentation and HTTP checks pass too.

Affected native files pass Linux syntax checks; changed settings/XUI XML parses
and `git diff --check` passes. With the user-supplied app key, live Trending,
Search, Categories, Recent and Items calls returned HTTP 200 and passed the actual
URL builder/parser. Trending/Search each supplied 24 usable items; Categories
supplied 36 entries. Returned thumbnail/share GIFs (42/63 frames respectively)
passed the actual codec and incremental player. No share/report POST was sent.
Live HTTP used the required cloud proxy; direct native viewer networking could
not be exercised end-to-end in this environment. The key stays outside source,
fixtures and reports. Fixtures also follow the official
[GIF API](https://docs.klipy.com/gifs-api) and
[demo model](https://github.com/KLIPY-com/klipy-ios-demo-app).
Windows UI/GL, provider approval and native linking remain unverified. No broad
build, packaging, GitHub build or unrelated tests ran.

Review follow-up: all three reported failures and the child-button keyboard
issue reproduced before the fixes. The production service now transfers its
decoder into the worker and releases it before restarting a loop. A real
2048×2048 GIF regression completes three loop restarts within the existing
64 MiB scratch limit and releases its reservations after cancellation. Opaque
partial-frame background/disposal fixtures match independently generated Pillow
pixels; transparent disposal remains covered and invalid background indices
never dereference the palette. Failed board tiles use the common retry path,
including with incoming previews disabled. Card shortcuts require direct focus,
so child buttons cannot trigger a second card action. All 16 focused checks and
both changed native translation units' Linux syntax checks pass after these fixes.

Windows acceptance with a test key:

1. Open GIFs from normal Nearby, toolbar Chat, IM, group and conference. Search
   KLIPY, use Trending/categories/Recent, More and Favourites; test unavailable
   items, rate limits and network failures without losing the destination draft.
   Save more than eight favourites and verify subsequent pages and retries.
2. Select/preview/save/insert and cancel chat sends. Only actually sending the
   inserted URL should queue Share; try two editors, a changed recipient, removed
   URL, restrictions, logout and reopening the board. Verify Recents after sends.
3. Favourite/unfavourite, restart and resolve again; inspect local metadata for
   provider IDs without media URLs/bytes. Check offline/unavailable favourites
   and reports. Confirm order and intact tracking queries against live responses.
4. Check 79%/80% autoplay, manual pause/play, hover-only mode, reduced motion,
   resizing/scrolling, many long GIFs, minimizing and independent previews. Check
   the compact picker and larger resizable pop-out in each supported skin/DPI.
   Test arrow navigation followed by Enter/Space, including unavailable tiles
   and newly scrolled rows, and each source's right-click menu.
   Check a large GIF across several loops, opaque partial-frame backgrounds,
   and Space/Enter on child controls without a second card action.
5. Change key/enable options with Apply/Cancel while searches run. Confirm late
   responses are discarded, closing/reopening preserves useful My GIFs state,
   and My GIFs works when KLIPY is disabled, unconfigured or unavailable.
   Fail a board download, disable incoming-chat previews, then Retry: it should
   start a fresh request while loaded/pending tiles continue without restarting.
