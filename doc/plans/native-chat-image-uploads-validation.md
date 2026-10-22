# Native chat uploads: validation and Windows acceptance

PR #81 adds Choose image and an attachment popup beside actual native IM, Nearby
and toolbar composers. The dependent follow-up adds Windows clipboard image/file
paste and local file drops. Public-upload review is the default. Host Chat and
grid inventory uploads are unchanged. Both inherit #80’s offscreen texture fix.

The anonymous destination is preconfigured from the supplied server handoff:
`https://images.sodie.online/api/upload`, folder `jkydgzspc7t9snhxy7c32yvo`.
Limits: 10 MiB/image, 16 megapixels, four pending attachments per draft, 32 MiB staged
encoded data per viewer, one active preparation/upload, 24 retained job receipts.
Windows bitmap clipboard input has one separate bounded slot (up to 64 MiB plus
headers); conversion temporarily needs a second RGBA canvas before PNG encoding.
PNG/JPEG/GIF/WebP parsing retains the decoder’s existing canvas/frame limits.
Upload thumbnails use only one frame at up to 320 pixels. The shared decoder
serializes preview/upload work internally. Original file bytes are preserved.

Controls are under Preferences → Chat → Media → Uploads. Apply/OK commits
configuration; Cancel cannot activate draft choices. Changing destination resets
confirmation bypass. A configured HTTP proxy leaves uploads unavailable with
an explicit status, matching the initial preview loader's policy.

Failed uploads keep their bytes until Cancel, explicit Retry, or bounded receipt
admission eviction. Retry first returns to review, including a duplicate warning;
it never automatically replays POST. A cancelled in-flight job keeps its future
and any later confirmed receipt until completion. Login, source, destination,
restriction and consent changes prevent stale insertion. Modified drafts, moved cursors or active selections require explicit Insert
links. One confirmed insertion advances other attachments’ original anchors;
completed links flush in staging order per draft, including when an earlier
upload awaits server processing. Send guards stop Enter/button paths while preparing,
reviewing, queued, uploading or awaiting server processing.

## Paste and drop behavior

- Normal Paste prefers native text. An image-only clipboard stages an image;
  context-menu Paste image, the popup’s Paste image button, and Ctrl+Shift+V
  explicitly select an image from mixed clipboard formats.
- Windows prefers registered PNG, then DIBV5/DIB; bitmap data becomes PNG.
  Explorer copied files preserve original file bytes. File drops are typed
  separately from existing URL/SLURL drops and use the frontmost native Chat
  destination. Enter/hover only highlights the target; Drop stages it.
- Escape/closing an unconfirmed popup cancels its staged review. Approved
  background work can continue without stealing focus. Completed receipts can
  be copied or inserted manually. Closed popups release their thumbnail texture.
- Windows is the initial platform for paste/drop. Other platform implementations
  keep safe unsupported defaults; Choose image remains available through their
  existing file picker. A native Windows compile/runtime test is still needed.

## Checks

- Ten focused tests pass: the five media tests plus upload policy/response,
  HTTP early rejection, bitmap parsing, production Job/Controller harness and
  production drop routing/hit-testing harness.
- The controller harness compiles production bodies unchanged with viewer and
  transport boundaries stubbed. Twenty-two scenarios cover cancellation/late receipt,
  login change, normal insertion, edited draft, revoked send authority, changed
  binding generation, replacement destination, current insertion authority,
  multi-image order, cursor movement and selected text. The drop harness adds
  eleven destination/lifecycle scenarios including opaque overlays and no Nearby
  fallback after a session/login/grid/configuration change.
  Conference admission and ongoing validation use the production permission
  check with the current speaker list, including late joins, departed initial
  participants, self exclusion, missing speakers and late upload receipts.
  Production admission/preparation checks cover three 9 MiB encoded inputs,
  worker-held bytes, file/DIB output reservations and cancellation during work.
  Delayed server processing preserves attachment order without blocking another
  conversation or bypassing draft-edit guards. In-flight reservations remain
  counted against the 32 MiB encoded limit until the worker result is collected.
- Bitmap layout/overflow/alpha/orientation tests also pass ASan/UBSan, with
  LeakSanitizer disabled because this sandbox cannot inspect its threads.
- Affected native source syntax checks pass against available Linux viewer
  headers, including uploader, IM, Nearby, toolbar, registration, Preferences,
  text-editor/chat-entry integration, window callback dispatch and UI hit testing.
  Windows OLE/clipboard code cannot be compiled or run with this Linux setup.
  Existing `llappviewer.cpp` CEF SDK absence still prevents its standalone check.
- Updated settings, strings, Preferences and popup XML parse; diff checks pass.
- No viewer/link/packaging/GitHub build or native Windows UI/GPU run performed.

## Live service check

Two deliberately nonsensitive 3×2 production-test fixtures were uploaded using
the documented multipart fields and anonymous header, then downloaded publicly
and compared byte for byte. This exercised the server protocol through the cloud
proxy, not a Windows viewer executable. No account token was used.

- PNG: `https://images.sodie.online/raw/gN2Nmt.png` (78 bytes, identical public download).
- GIF: `https://images.sodie.online/raw/u5zZSu.gif` (150 bytes, identical public download).

Anonymous uploaders cannot delete these two tiny verification files. They remain
subject to the supplied expiry/oldest-first policy; no cleanup is claimed.

## Windows acceptance

1. Build matching binaries above #80. In native IM, Nearby and toolbar Chat,
   open the attachment button and Choose image. Verify sender/destination,
   thumbnail, public-link explanation and explicit Upload before any POST.
2. Test PNG/JPEG/GIF/WebP, Unicode paths, corrupt/non-image/oversized files,
   multiple images and cancellation. GIF/WebP file downloads retain animation.
3. Type/edit captions during upload. Confirm no overwrite or focus steal;
   unchanged drafts receive a link, changed drafts offer Insert links. Check
   undo, selection, Enter modifiers, Send buttons and length limits.
4. Close an IM, change login/channel, restrict an IM/group, switch managed
   characters, and change/disable upload Preferences during preparation/POST.
   Late results must never rebind a draft or become public Nearby chat.
5. Interrupt connectivity after submission and cancel in flight. Check bounded
   receipts, Copy links, explicit Retry warning, quota/rate-limit failures,
   server-side pending results and shutdown without an unbounded wait.
   Stage three 9 MiB clipboard images and verify preparation progresses. Hold
   the first upload in server processing while a second completes; verify links
   appear in staging order. Have a restricted participant join a conference
   during preparation/POST and verify cancellation without automatic insertion.
6. Test Preferences Apply/Cancel, first-use bypass, destination-change reset,
   restart persistence, alternate skins, UI scaling and narrow composers.
7. Windows paste/drop: test Snipping Tool PNG, DIBV5 transparency, older opaque
   DIB screenshots, browser Copy image, mixed text/image formats, ordinary text
   paste, menu Paste image, Ctrl+Shift+V and Explorer copy/paste. Test Unicode
   paths, four-file drops, rejected larger batches, drag Escape, overlays and
   custom viewport/DPI coordinates. Hover never starts an upload; changing a
   tab/login/grid/recipient mid-drag cannot send to a replacement conversation.
8. Test custom proxies: the feature explains unavailable transport and does not
   silently bypass a configured proxy. Original text chat remains functional.
