# Native chat media previews: validation and Windows acceptance

Implementation branch: `feat/native-chat-media`, based on main `8a217b6b72`.
Host Chat, GIF search and clipboard/file uploads are deferred. No session protocol
change, release, installer or VPS configuration is included.

## Behavior

- Preferences → Chat → Media controls native Nearby, direct IM, group and
  conference history. Original message text and links stay selectable/copyable.
- Default: Click to load. A compact Load image row expands when clicked, reserving
  space before the download finishes. Up to three supported links per message
  have cards; other links remain ordinary links.
- PNG/JPEG, GIF and WebP are decoded in a worker. GIF/WebP animation, transparency,
  JPEG EXIF orientation and non-power-of-two source dimensions are supported.
  Preview pixels are capped at 1024 per dimension; Open original accesses the
  source resolution. The larger viewer supports scroll-to-zoom, drag-to-pan,
  Fit image, Play/Pause, Copy link, Open original and normal floater close keys.
- Automatic loading is separately opt-in for selected sites or all supported
  image links. These choices take effect through Preferences Apply/OK, so
  changing a draft and pressing Cancel cannot initiate automatic downloads.
- Inline animation is off by default. At most three visible inline cards animate
  per frame, plus up to four explicitly opened image floaters. Windows reduced
  motion disables implicit inline autoplay; explicit Play remains available.
- Loading is separate from native unread counts, notifications and composition.
  Native restrictions and mute state are rechecked while queued and before
  showing results. Group IMs mirrored to Nearby retain their conversation ID;
  ambiguous old Nearby IM logs remain text-only.

## Providers and fallbacks

- Direct HTTPS image URLs, including Zipline output, need no account or API key.
- Gyazo public share links use its oEmbed response, consuming only image URL or
  thumbnail URL fields, never embed HTML.
- Jumpshare/jmp.sh tries its documented `+` direct-media URL, preserving queries,
  and falls back to public page metadata.
- Snipboard resolves the real metadata image, including share pages ending in
  `.jpg`; it does not guess CDN filenames.
- Imgur, ImgBB/ibb.co and Image Chest public pages use Open Graph/Twitter image
  metadata. Albums initially use the representative image, not a gallery.
- `images.sodie.online` is included in Selected sites. Its `/view/name.png`
  pages use the verified `/raw/name.png` route because the public view page is
  a JavaScript shell without image metadata. Original share links remain intact.
  Other public hosts can be added for metadata-based share pages; direct image
  links need no extra configuration. No upload secret or admin token is used.
- Deleted/private pages, login walls, script-only pages, unsupported formats and
  provider blocks retain the original URL and offer a retry when applicable.
  Provider fixtures test the actual resolver, but live-provider compatibility
  still needs Windows acceptance with representative public links.

## Network, ownership and resource limits

- HTTPS and valid certificates only; no cookies, native credentials, referrer,
  scripts or remote page rendering. Every redirect is parsed and its actual
  connected IPv4/IPv6 address checked, preventing private-address/DNS rebinding
  access. HTTPS downgrade, URL credentials and local schemes are rejected.
- This first version leaves previews as text when the viewer has a proxy enabled for
  HTTP traffic. It does not silently bypass the user's configured proxy. Open
  the original link through the normal viewer/browser behavior in that case.
- Four concurrent fetch jobs, three redirects, 15 seconds per fetch operation,
  512 KiB metadata, 15 MiB encoded image, 16 megapixels source canvas, 120 frames,
  48 retained requests and 64 MiB retained decoded pixels per viewer process.
- Decoder work is serialized. stb's allocations have a separate 64 MiB ceiling,
  including PNG inflate buffers. WebP allocations are bounded and its canvases
  are limited before decoding. These working buffers and encoded transfers are
  additional to the retained-pixel cache budget. Oversized animations can use a
  first frame or fail safely rather than allocate unbounded frame history.
- Idle maintenance releases every offscreen card texture, including repeated
  cards sharing a single decoded image; no offscreen draw call is required.
  Returning cards upload again without collapsing their reserved history rows.
  Weak subscribers for destroyed/rebound cards are pruned.
- Offscreen cache entries are evicted when needed, releasing their GL textures
  through weak view handles. Pending jobs remain retained until their futures
  settle; Clear/Apply never waits synchronously for the network.
- Source ownership includes account, grid, login, sender and conversation.
  Logout, restrictions, explicit clear and consent changes cancel pending work
  and invalidate displayed textures. Shutdown drains cancelled worker jobs
  before global HTTP/GL cleanup. Image URLs/query tokens are not logged.

## Checks performed

- Four strict C++17 portable suites: policy/URL/metadata, decoder, HTTP rejection
  and cancellation, sharing-site resolver. They build through the standalone
  regression CMake project with assertions retained in Release.
- A Python-driven C++ harness compiles the actual native Source/Entry/Service
  bodies against stubbed viewer API boundaries. It covers late results after
  login change, group restriction, sender-specific Nearby restriction, clear
  while work is pending, four-job admission, offscreen capacity eviction and
  refusal to evict visible pinned entries. It also checks mute and proxy gates.
  A second case compiles the actual attach/visibility/idle-maintenance bodies,
  seeds 100 textures for one 4 MiB decoded image, and checks offscreen release,
  visible retention, frame reset and dead-subscriber pruning.
- The four C++ suites passed AddressSanitizer and UndefinedBehaviorSanitizer.
  LeakSanitizer is disabled for this run because this sandbox prevents its
  thread inspection; no leak-check claim is made.
- Linux syntax checks against available viewer/prebuilt headers passed for
  `fschatmedia.cpp`, `fschathistory.cpp`, `llfloaterpreference.cpp`,
  `llviewerfloaterreg.cpp` and `llimview.cpp`. The HTTP adapter also passes with
  the available libcurl 7.54.1 prebuilt headers and current system headers;
  both keep the same HTTPS-only transport policy.
- `llappviewer.cpp` syntax checking stops at missing pre-existing
  `cef/dullahan_version.h`; only its include and early-cleanup hook changed.
- Updated settings, strings, preferences and floater XML parse successfully.
  Diff/whitespace checks are clean. Dependency notices are included in the three
  platform license files already staged by the installer manifest.
- The supplied public `images.sodie.online` PNG, view page and GIF returned
  HTTP 200 with the expected image/HTML content types. The PNG and GIF were
  decoded with the production decoder, and the view-to-raw adapter has fixture
  regressions for PNG, animated GIF, signed queries and deleted files.

No viewer build, link, packaging, GitHub build, unrelated suite, native Windows
UI or GPU run was performed. This is a draft native-testing checkpoint.

Focused reproduction commands (use your installed CMake/Python/compiler):

```sh
cmake -S indra/newview/tests/folderstorm -B /tmp/fs-chat-tests \
  -DCMAKE_BUILD_TYPE=Release \
  -DFS_RELEASE_BOOST_JSON_INCLUDE_DIR=/path/to/boost/includes
cmake --build /tmp/fs-chat-tests --target \
  chat_media_policy chat_media_codec chat_media_http chat_media_resolver
ctest --test-dir /tmp/fs-chat-tests -R '^chat_media_' --output-on-failure
```

The resolver test needs Boost.JSON headers, as does the existing release-feed
test. The HTTP test needs libcurl headers/library. The production build uses
the viewer's existing prebuilts. The service harness is registered on Unix
when Python is available, or run it directly with
`python3 indra/newview/tests/test_fschatmedia_service.py`.

## Windows acceptance checklist

1. Build matching viewer/host normally; verify host Chat remains text-only and
   session protocol/input/audio behavior is unchanged.
2. With default settings, receive PNG/JPEG/GIF/WebP links in Nearby, direct IM,
   group and conference chat. Confirm no remote fetch before clicking Load.
3. Test standard and compact history styles, Unicode, copying a selected range,
   Ctrl+A, clicking original URLs and scrolling older messages. Loading pixels
   must not move reading position, selection or composer focus.
4. Enlarge an image: fit, wheel zoom, drag, animation Play/Pause, Copy link, Open
   original and Escape. Test transparent/non-square images and phone JPEGs with
   EXIF rotation. Open/close/reopen several previews, up to four at once.
5. Test actual public Gyazo, Snipboard (including `.jpg` share-page URLs),
   Jumpshare, Imgur, ImgBB and Image Chest examples. Deleted/private pages must
   remain usable as original links, with no repeated automatic retry loop.
6. Test the supplied public samples (no token required):
   - `https://images.sodie.online/raw/sodie-sample.png`
   - `https://images.sodie.online/view/sodie-sample.png`
   - `https://images.sodie.online/raw/sodie-sample.gif`
7. Change automatic-loading mode in a Preferences draft and Cancel: it must not
   begin loading. Apply selected-sites mode and confirm unrelated sites remain
   click-to-load. Test Off, Clear cache and persistence after restart.
8. Enable autoplay; Pause must override it for that card. Scroll/hide/minimize
   chat, switch characters, and enable Windows reduced motion. Hidden previews
   must not keep updating textures or initiate offscreen downloads.
9. Restrict/mute a sender or group during a slow load; disconnect/logout while
   loading; remove an old history line; close its window. Late results must not
   restore hidden media, cross logins or change IM/Nearby destinations.
10. Resize narrow/wide, use larger text and multiple DPI scales/themes/skins.
    Footer controls and original links must remain accessible. Load more than
    48 distinct images over time to verify old offscreen entries yield capacity.
11. With a configured proxy, verify this version leaves links as text rather
    than making a direct preview connection. Quit during pending loads and
    check normal shutdown, including managed viewers.
