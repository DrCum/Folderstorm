# Inline chat images and animated previews

Status: native chat and sharing-site implementation on `feat/native-chat-media`, based on main `8a217b6b72`. Windows acceptance is pending. Updated 2026-10-22.

Scope update: the user deferred host Chat. This branch implements the native viewer and sharing-site adapters together; the host sections below remain future design only. Zipline deployment and upload integration remain separate work. See `chat-media-previews-validation.md` for actual behavior, checks and limits.

## Goal and scope

Show compact image previews beneath image links in native Nearby, direct IM, group and conference chat, and in shared, detached and monitor Chat in the session host. Clicking a preview opens a larger in-app image viewer. Keep the original link available for copying and for recipients using other viewers.

Include direct images, animated GIFs and supported image-sharing pages. Zipline installation is being handled separately. Clipboard/file upload and GIF search/picker are separate follow-ups; viewing an existing image link must work without an upload account or credentials.

## Evidence from the current implementation

- `indra/newview/llchathistory.cpp` inserts inline widgets through `LLTextBase::appendWidget()`. Add image cards here without replacing the viewer's existing message formatting or declaring incoming text trusted.
- The host creates plain Win32 EDIT controls for shared and monitor history in `tools/session-host/main.cpp`. `tools/session-host/chatui.h::Transcript` preserves text selections, reading position and event-based read watermarks. True inline cards require a media-capable replacement, rather than adding image painting to the existing EDIT control.
- `indra/llimage` currently registers PNG, JPEG and other viewer formats, but no GIF or WebP decoder. Animated support needs an explicit decoder dependency and bounded playback implementation.
- Host chat lines already have worker, account, grid, login-generation, conversation and event identities. Use them to reject stale preview completions; do not add image bytes to session IPC or the bounded chat-event queue.

## User experience and defaults

- Keep message text and original URLs intact. Add an aspect-preserving thumbnail beneath each supported image link, at most three cards per message; further links remain ordinary links.
- Default maximum card size: 320 by 240 logical pixels, constrained by the current panel width. Reserve card geometry before loading, then update in place to prevent scroll jumps.
- A compact GIF badge and Play/Pause affordance identify animation. Default to a still thumbnail; opening the larger preview plays it. An optional visible-card autoplay setting enables inline playback. Stop playback when hidden, minimized or scrolled away and honor reduced-motion preferences.
- The enlarged preview supports fit-to-window, zoom, pan, Copy link, Open original and Escape to close. It must not take over viewer movement controls or change the active character.
- Failed, expired, private or unsupported links retain their usable URL and show a small Retry/Open original affordance. Never turn a network failure into a modal interruption or block chat delivery.
- Add a Media subtab under Chats & notifications, and the equivalent native chat preference: Off, Click to load (default), Automatic for selected sites, or Automatic for all supported image links. Include thumbnail size, animation and Clear preview cache controls. Explain external requests and IP disclosure when automatic loading is enabled; keep deeper detail in tooltips.
- Preview completion, animation and opening the larger viewer never independently mark a message read or trigger another notification. Keep existing text visibility/read settings authoritative.

## Provider support

| Provider or URL | Resolution strategy |
| --- | --- |
| Direct PNG, JPEG, GIF and WebP URLs | Validate the response type and decoded format. Support query strings and extensionless media on explicitly selected sites; an extension alone is not proof that a response is an image. |
| Folderstorm's Zipline instance | Prefer returned direct file URLs. Add its final public hostname to the provider configuration. Support public view/share links through bounded metadata extraction when available. |
| Gyazo | Use the documented oEmbed endpoint for public share links, then validate the returned thumbnail/media URL. Never execute returned embed HTML. |
| Jumpshare / jmp.sh | Use the documented direct-media link mechanism where applicable; accept only image/GIF responses. Fall back to public preview metadata if needed and permitted. |
| Snipboard | Resolve the public share page's actual image metadata; do not guess an extension or rewrite an arbitrary share ID to a presumed CDN filename. Add captured real-page fixtures before enabling the adapter. |
| Imgur, ImgBB and Image Chest | Direct media URLs work through the shared loader. Public single-image share pages get small, separately tested metadata adapters where available and permitted. Albums initially show one representative thumbnail and open the original page. |

Provider adapters should use documented APIs first and bounded Open Graph metadata second. Verify actual public response shapes and applicable embedding rules before promising share-page support. Do not depend on paid scraping services, bypass authentication, or make arbitrary websites work by executing JavaScript. Unknown share pages remain links.

Sources checked for this plan:

- Gyazo API/oEmbed: <https://help.gyazo.com/Gyazo%20APIs-5de75e1e040e1d0017df436e>
- Jumpshare direct links, including images and GIFs: <https://support.jumpshare.com/article/219-how-to-share-videos-audios-via-link>
- Zipline upload and file URL options: <https://zipline.diced.sh/docs/guides/upload-options>
- Zipline release notes describe view-route and Open Graph behavior: <https://zipline.diced.sh/releases>

Snipboard and the additional share-page adapters still require representative response fixtures and policy verification. Their direct image links do not depend on those adapters.

## Shared loading and decoding

- Extract URL discovery, provider classification, request policy, media validation, resource budgets and request-state transitions into a common C++ component used by host and viewer. Keep platform networking, drawing and UI ownership adapters separate.
- Use a media-specific asynchronous HTTP client on each platform. The host's existing release-fetch helper is not a general media client: previews require stricter redirect/destination checks and cancellation. Do not reuse updater-specific allowlists or weaken update protections.
- Allow HTTPS only, with certificate validation. Reject credentials in URLs, local/private/link-local destinations and non-network schemes. Validate every redirect and resolved connection destination, including IPv6; prevent DNS changes from bypassing destination checks.
- Fetch metadata and image bytes only, without browser cookies, referrer forwarding, native session credentials, scripts, iframes, plugins or remote HTML rendering. Metadata URLs and image URLs go through the same network policy. Preserve signed query strings but never log them.
- Initial hard limits: 512 KiB decompressed metadata, 15 MiB downloaded image data, 16 megapixels decoded canvas, three redirects, 15-second total request deadline and four concurrent fetches per process. Limit pending requests, retry rate and negative-cache lifetime as well.
- Bound animation frame count, frame dimensions, decoded working memory and frame timing. Decode incrementally with a maintained, license-compatible GIF/WebP library shared by both executables; avoid platform codec availability assumptions. Oversized animation may fall back to a validated first frame; reject it if even that exceeds limits.
- Share in-flight work and a bounded memory cache within the same account/grid/login namespace. Use reference-counted subscribers so closing one card does not cancel another surface's valid preview. Start with a 64 MiB process-wide decoded cache, with visible entries prioritized. No persistent media cache by default.
- Fetch visible cards first. Cancel work and release surfaces on conversation hiding, restriction, source change or logout. Drop completion unless its source, message identity, URL and UI generation still match. Reloading old history never fetches offscreen links automatically.
- Do not log chat text, image URLs, private query tokens or image contents. No preview requests through MCP and no automatic upload of received images to the Zipline server.

## Native viewer presentation

- Create an inline image-card widget and a dedicated image-preview floater. Add cards only after existing mute, restriction and message-filter handling.
- Preserve compact/plain and expanded chat styles, selectable original text, timestamps, links, themes, alternate skins, Unicode and zoomed UI text.
- Anchor layout to message identity and offset. Image completion or resizing must preserve reading position and selection; follow new messages only when the user was already following the tail.
- Keep transient toasts text-only in this first version, avoiding duplicated animations and downloads.

## Host presentation

- Replace history with one reusable, virtualized transcript control containing selectable message text and preview rows. Keep the composer and its source guards separate.
- Preserve Ctrl+A, copying, URL context menus, keyboard selection, accessibility text, latest-message navigation, read watermarks and position across docking, resizing and history updates. Do not ship the new control until these behaviors are covered; retain a text-only display option.
- Reuse the same control for shared, detached, fixed-monitor and cycling-monitor Chat. Keep theme, DPI and character accents consistent.
- A paused cycling monitor that loses its character stays unavailable until explicit navigation. Media completion must never rebind the composer or restore its focus. Playing the cycling monitor retains the existing Pause to chat behavior.
- No protocol change is expected: parse URLs from already authorized chat text and fetch previews locally. Reassess only if implementation reveals a required lifecycle signal; never transmit image payloads through chat IPC.

## Delivery checkpoints

1. **Shared media core and native previews.** Implement policy/settings, bounded fetch/decode, PNG/JPEG/GIF/WebP cards and enlarged preview in native chat. Include direct Zipline URLs and fixture-based core tests.
2. **Host transcript and media previews.** Integrate the reusable media-capable transcript across all host Chat surfaces, with selection, scroll, accessibility and account-ownership regression coverage.
3. **Sharing-site adapters and integration polish.** Enable verified Gyazo, Jumpshare, Snipboard and additional single-image share adapters; finish site settings, failure handling, reduced-motion behavior and narrow monitor layouts.

Keep these as separate reviewable PRs when implementation is authorized. Start from the latest compatible integrated branch at that time, preserve published history, and use a deliberate dependency stack if earlier checkpoints remain unmerged. No release publishing or VPS changes are part of this plan.

## Focused validation

- URL parsing: punctuation, Unicode, multiple links, signed queries, exact provider-host matching and misleading suffixes.
- Network policy: redirects, HTTPS downgrade, private IPv4/IPv6, DNS rebinding, oversized compressed responses, timeouts, cancellation and revoked automatic-load consent.
- Media: incorrect MIME types, malformed/truncated files, transparency, EXIF orientation, oversized dimensions, animation frame disposal/timing and memory limits.
- Provider fixtures: public image/GIF, removed or private content, album, no thumbnail, blocked response and expiring links. Ordinary network changes should affect one adapter rather than break chat.
- Ownership/lifecycle: same URL across surfaces, subscriber removal, account switch/logout, restrictions becoming active mid-fetch, paused monitor disconnect and cycling during completion.
- Transcript behavior: late image loads while reading older messages, selected text, copy/Ctrl+A, unread watermarks, pending sends, draft destination and original-link preservation.
- Resource checks: only visible images animate; no offscreen fetch storm; bounded cache, decoder and network concurrency with five characters and multiple monitors.
- Windows acceptance: native and host chat, hosted/separate viewers, docking/pop-out, all themes, larger text, DPI scaling, keyboard and screen-reader navigation. Existing preview-stream rendering budgets remain unchanged.

Use focused portable unit/fixture tests and affected-path checks. Do not run broad viewer builds, packaging, GitHub builds or unrelated suites as part of planning. Native runtime acceptance remains pending until the implementation is built and tested.

## Zipline handoff

The setup agent only needs to provide the final public HTTPS hostname, a sample direct image URL, a sample share/view URL and a sample animated GIF URL. Use non-sensitive fixtures. Confirm whether public files and metadata are accessible without login and what expiry limits are configured. No administrator token or upload secret is needed to implement previews.
