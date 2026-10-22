# Native chat uploads: validation and Windows acceptance

Checkpoint 1 adds Choose image and an attachment popup beside actual native IM,
Nearby and toolbar composers. Public-upload review is the default. Paste/drop
is the next dependent checkpoint. Host Chat and grid inventory uploads are unchanged.

The anonymous destination is preconfigured from the supplied server handoff:
`https://images.sodie.online/api/upload`, folder `jkydgzspc7t9snhxy7c32yvo`.
Limits: 10 MiB/image, 16 megapixels, four pending attachments, 32 MiB staged
encoded data, one active preparation/upload per viewer, 24 retained job receipts.
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
restriction and consent changes prevent stale insertion. Modified drafts require
explicit Insert links. Send guards stop Enter/button paths while preparing,
reviewing, queued, uploading or awaiting server processing.

## Checks

- Eight focused tests pass: the five media tests plus upload policy/response,
  HTTP early rejection and production Job/Controller lifecycle harness.
- The controller harness compiles production bodies unchanged with viewer and
  transport boundaries stubbed. Nine scenarios cover cancellation/late receipt,
  login change, normal insertion, edited draft, revoked send authority, changed
  binding generation, replacement destination and current insertion authority.
- Affected native source syntax checks pass against available Linux viewer
  headers, including uploader, IM, Nearby, toolbar, registration and Preferences.
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
6. Test Preferences Apply/Cancel, first-use bypass, destination-change reset,
   restart persistence, alternate skins, UI scaling and narrow composers.
7. Test custom proxies: the feature explains unavailable transport and does not
   silently bypass a configured proxy. Original text chat remains functional.
