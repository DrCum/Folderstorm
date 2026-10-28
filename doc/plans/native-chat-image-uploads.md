# Native chat image uploads: clipboard and drag/drop

Status: implementation checkpoint 1, 2026-10-22. Native uploader/Choose image is implemented; paste/drop follows in a second PR. No VPS changes made.
Build above PR #80 while it remains unmerged, or current main after it merges.
Preserve published history and unrelated work.

## Result for the user

Paste a screenshot or drop an image into native Chat, Nearby Chat, or an IM,
group or conference window. Folderstorm uploads it to `images.sodie.online`
and puts its direct link in that conversation's draft. Sending remains explicit.
The receiving viewer can display that link using #80's previews.

Add one small image/attachment button beside the composer, with a tooltip and
an accessible name. It offers Choose image and Paste image; it also makes the
feature discoverable without knowing a shortcut. Keep explanations short:
"Paste or drop an image to upload a public link. You choose when to send it."
Use a compact attachment/progress row rather than a separate wizard.

Scope is native viewer Chat, including the toolbar Nearby composer. Host Chat,
MCP tools, inventory texture uploads, video, GIF search, image editing and VPS
deployment are separate work. This uploads ordinary web files, not paid grid
inventory assets. Existing grid-upload commands keep their behavior.

## Zipline service and setup

Use Zipline's existing anonymous folder-upload support as the default. An
account/token is unnecessary when a dedicated folder permits uploads. The
upstream v4.7.0 middleware admits an unauthenticated upload with
`x-zipline-folder`; the upload handler checks `allowUploads` and charges the
folder owner's storage quota. A folder may have `public=false` while accepting
uploads, keeping other people's files out of a browsable public listing.
Individual image links are still public to anyone who has them.

Server handoff received: folder `jkydgzspc7t9snhxy7c32yvo`, anonymous uploads, 10 MiB per file, a 30 GiB server budget and best-effort retention up to 180 days. Oldest-first cleanup may remove images earlier. Quota error `E5002` suggests a manual retry after about one minute. Client uploads remain limited to images even though the server also permits some other file types. The following deployment checks remain relevant:

- Confirm the deployed Zipline version supports this route and supply a
  dedicated folder ID with uploads enabled and public listing disabled.
- Confirm allowed image types, maximum request/file size, owner quota and
  retention. Public sample files having no expiration does not establish the
  policy for new uploads.
- Apply request-rate, upload-size and storage limits at the service/proxy;
  client limits alone cannot protect a public upload endpoint. A full quota or
  disabled folder should produce a clear refusal without repeated retries.
- Verify an unauthenticated multipart test returns a publicly readable image
  link. This test waits for implementation/testing authorization and uses a
  deliberately chosen fixture, not clipboard contents or a user's photo.

The folder ID is a public upload destination, not an administrator secret.
Never put a shared Zipline account/admin token in the viewer or its settings.
The client does not need to list files or administer the server. A new relay
is not required for the verified upstream anonymous-folder flow; if the deployed
service cannot expose it, settle that server contract before adding a substitute.

Advanced settings may support another Zipline host and the user's own account
token. Keep a token in the viewer's protected store, bound to the exact HTTPS
origin/profile, with Forget token and an optional session-only choice. Do not
copy tokens with profile settings, include them in diagnostics, or reuse grid
login credentials. This optional mode is not a prerequisite for the default UX.

## Interaction and Preferences

1. The first paste/drop opens a small preview with the image, sending character,
   destination and upload host. Say "Anyone with the link can view this image."
   Upload and Cancel are explicit; no image bytes leave the viewer first.
2. Offer "Upload future pasted/dropped images without asking" for that upload
   destination. Default Off. Later operations may upload immediately when this
   preference is on, but still never send chat automatically. Reset consent when
   the upload host/destination changes.
3. During work, show thumbnail/name, progress and Cancel. Keep typing available.
   While attachments are being prepared/uploaded, block all native Send/Enter
   variants for that draft with a short progress explanation. Cancelling/removing
   the pending attachment restores normal text sending.
4. On confirmed success, insert the direct URL into the original draft using
   normal editor undo support. Preserve captions, selection and later typing.
   Do not place upload placeholders into text that could be sent accidentally.
5. If the editor/draft changed in a way that makes its insertion anchor uncertain,
   leave the completed attachment with Insert link and Copy link actions. Never
   guess a new recipient, overwrite text, or insert into the next sent message.
6. Removing an attachment removes it from the draft. It does not promise to
   delete an already uploaded image. Cancellation after submission may leave a
   server file; say so only where that outcome matters.

Put the settings under Preferences → Chat → Media, with compact Previews and
Uploads top tabs if needed to avoid a scrolling page. Include upload enablement,
host/destination, first-use confirmation, and connection status. Put personal
token controls behind an Advanced expander. Apply/OK commits changes; Cancel
discards draft changes and cannot initiate a request or save a token. The image
button can open setup when uploads are not configured. Link-only chat and #80's
preview choices remain usable independently.

Keep failed/completed link receipts available for this login until dismissed,
within a bounded queue. Do not create a persistent image/history database.

## Clipboard behavior

- Normal Ctrl+V and the normal Paste menu preserve nonempty Unicode text paste.
  When there is no text but a supported image is present, stage that image.
  Paste image explicitly selects the image when the clipboard offers both.
  Add Ctrl+Shift+V as Paste image only for registered chat composers, after
  checking it does not displace an existing chat shortcut.
- On Windows, prefer the registered PNG clipboard format, then bounded
  `CF_DIBV5`/`CF_DIB`. Validate header length, masks, dimensions, orientation,
  stride and pixel count before copying; handle opaque BGRX versus real alpha
  correctly. Copy owned bytes and close the clipboard promptly; encode/decode
  in a worker. A bitmap paste becomes PNG, preserving screenshot text and alpha.
- Support copied local image files through `CF_HDROP` using the same file
  validation as dropping. Do not interpret pasted path text as permission to
  open a file. Do not retrieve an image from a pasted URL, HTML or remote site
  and re-upload it behind the user's back; URLs remain ordinary text.
- Inspect/read clipboard content only following the user's paste action.
  Right-click and keyboard Paste must share eligibility and dispatch, including
  image-only clipboard availability. Other editors retain ordinary text paste.
- Bitmap clipboard contents are a still image. Preserve GIF/WebP animation when
  the clipboard contains an actual supported image file; do not invent animation
  from a rendered browser frame.

## File drag/drop

- Accept local PNG, JPEG, GIF and WebP files over an explicit native Chat pane
  or composer. Highlight the destination and show the Copy cursor; never Move.
  Do not upload during drag-enter/hover, over the 3D world, or after Escape.
- Bind the drop to the chat under the pointer, not the last focused chat. A tab
  switch, hidden/closed pane, changed login or invalid destination cancels that
  binding before any submission.
- Extend the window drag/drop API with a typed local-file payload rather than
  passing paths through the existing URL/SLURL callback. The Windows OLE target
  currently accepts only `CF_TEXT`; it needs `CF_HDROP` handling and balanced
  `STGMEDIUM`/handle cleanup. Existing URL, SLURL, inventory and prim-media drops
  continue on their existing paths.
- Accept regular image files only, without directory recursion. Do not fetch
  browser URLs or virtual drag attachments. Validate actual content, not just
  filename suffixes. Snapshot file data for a job; a changed/deleted/unreadable
  file cannot cause a different later file to be uploaded silently.
- Support a small multi-file drop. Show validation failures per file before
  upload, keep the chosen order, and retain successful links if another upload
  fails. Do not restart an entire batch automatically.

## Upload pipeline and ownership

Use a new upload controller alongside #80's preview code. Reuse URL validation,
image probing and source-lifecycle concepts; keep authenticated multipart POST
separate from the anonymous preview GET client.

- Initial limits: four attachments, 32 MiB combined staged bytes, 10 MiB per
  file or the lower server limit, 16 megapixels per image canvas, one active
  upload per viewer, bounded encoding/probing and a 256 KiB response limit.
  Reject excessive dimensions before allocation. Do not flatten animations or
  silently recompress/resize original file uploads to fit. Explain a size/type
  failure so the user can choose another file.
- Clipboard bitmaps become newly encoded PNGs without source metadata. File
  uploads preserve original bytes, animation and orientation. Use a generic
  multipart filename plus randomized server names, omitting local paths and
  original filenames. The first-use details should make clear that original
  files can retain embedded metadata. Metadata removal/editing is a separate
  explicit operation, not a claimed side effect of renaming.
- Capture account, grid, login session, conversation/destination, composer weak
  handle and draft generation at staging. Track an insertion anchor through
  edits or fall back to explicit Insert link. Nearby channel changes are also
  destination changes. Logout, conversation removal, restrictions or upload
  consent/configuration revocation invalidate jobs and release local image data.
- Recheck native send availability, RLVa, support-contact restrictions, group
  membership/moderator limits and current login before submission, link insertion
  and eventual normal sending. Do not route uploads around the existing native
  send path. No fallback to Nearby or another character on any failure.
- Switching a managed character never reassigns a pending job. Completion may
  update only its original still-valid draft; it cannot steal focus, keyboard
  ownership or bring its viewer to the foreground. No new host IPC is required.
- POST one file at a time to `/api/upload`, multipart field `file`, with
  `x-zipline-folder` in anonymous mode and no Authorization header/cookies.
  Request random filenames and JSON; do not request lossy compression or
  extensionless links. Honor server retention and expose any returned expiry.
- Parse `files[].id`, `name`, `type`, `url` and `pending` with bounded JSON/schema
  validation. For the verified Folderstorm instance, return a validated
  `/raw/<encoded-name>` URL for native previews. Never concatenate an unchecked
  response name, blindly accept another origin, or insert an admin/API URL.
- Keep uploading and server-side processing distinct. A `pending` result retains
  its receipt and waits for bounded public availability checks; it is not a
  reason to submit the file again. A failure to establish readiness leaves a
  recoverable receipt rather than a false success.
- Valid HTTPS certificates, explicit destination validation, private-address
  rejection and no automatic POST redirects/credential forwarding. Respect a
  configured proxy through a supported path; if this cannot preserve the policy,
  refuse with a clear status rather than bypass it. Compatibility with older
  viewer libcurl headers remains required.
- Cancellation stops queued work and attempts to abort an active transfer.
  A lost response can mean the server already stored the image: retain an
  uncertain outcome and require an explicit retry, which may create another
  file. Never auto-replay a POST after reconnect, logout, timeout or startup.
  Keep active jobs/receipts until callbacks settle, even at queue capacity.
- Do not log tokens, clipboard contents, local paths, image bytes, chat text or
  private URL queries. Do not send avatar identity to Zipline as extra metadata.

## Code integration

- `LLChatEntry` gets opt-in image-paste availability/dispatch hooks; generic
  `LLTextEditor` retains its text behavior. Wire the actual `FSFloaterIM`,
  `FSFloaterNearbyChat` and `FSNearbyChatControl`/Nearby hub surfaces. All send
  entry points consult the same pending-attachment state.
- `LLWindow`/Windows implementation provides a bounded image/file-list clipboard
  interface. `LLDragDropWin32` and `LLWindowCallbacks` provide typed file drops;
  `LLViewerWindow` dispatches them to the visible Chat target before world logic.
- Introduce small upload policy/job/transport helpers and a shared attachment-row
  component. Reuse Preferences machinery and the protected-store API. No new
  grid upload permissions, MCP tool or session protocol version is necessary.
- Windows paste/drop is the first native acceptance target. Keep platform
  interfaces capability-based with safe defaults so macOS/Linux continue to
  compile. Choose image works through their native file picker; image clipboard
  and file-drop adapters require their own native acceptance before advertising
  those gestures as supported there.

## Delivery and checks

Two useful stacked PR checkpoints, without merges or release publishing:

1. **Uploader and native attachment UI.** Anonymous-folder configuration, first
   use, Choose image, jobs, progress/cancel, source-bound insertion and failure
   receipts. This can be tested end to end before platform event integration.
2. **Paste and file drop.** Windows PNG/DIB/copied files, OLE file drops, target
   highlighting, context-menu/keyboard behavior and managed/custom-viewport
   acceptance. Reuse the same pipeline rather than adding a second uploader.

Focused portable tests cover image/file eligibility, resource admission,
clipboard-header arithmetic, response/URL validation, endpoint changes,
cancel/late completion, uncertain POST outcomes, source/draft ownership and
multi-file order/partial success. Test pending-upload Send guards and all
native restriction changes while queued. Do not require a public server for
automated tests or upload real user data in CI.

Windows acceptance covers screenshot tools, transparency, browser copy image,
mixed text/image formats, Unicode file paths, Explorer copy/paste, multiple
files, cancelled drags, custom viewport coordinates/DPI, hosted/separate viewers,
chat-tab changes, caption typing, text selection/undo, Enter variants, logout,
server quota/auth/rate failures, network interruption, and a confirmed uploaded
GIF playing via #80. Verify Preferences Apply/Cancel and no requests before the
first confirmation. Use deliberately chosen nonsensitive fixtures for the
separate live Zipline test and record their returned URLs/cleanup outcome.

No broad viewer build, packaging, GitHub build or unrelated tests are part of
this planning work. Implementation uses focused affected-path checks; native
Windows behavior remains pending until tested.

## Sources checked

- [Zipline upload endpoint](https://zipline.diced.sh/docs/api/api/upload/post)
- [Upload headers and response](https://zipline.diced.sh/docs/guides/upload-options)
- [Normal token authority](https://zipline.diced.sh/docs/api/authentication)
- [Anonymous admission, pinned v4.7.0](https://github.com/diced/zipline/blob/v4.7.0/src/server/middleware/user.ts)
- [Folder checks, quotas and upload result, pinned v4.7.0](https://github.com/diced/zipline/blob/v4.7.0/src/server/routes/api/upload/index.ts)
- [Upload-only folder visibility, pinned v4.7.0](https://github.com/diced/zipline/blob/v4.7.0/src/server/routes/api/server/folder.ts)

These establish upstream behavior; the supplied public read-only examples do
not verify an upload-enabled destination on `images.sodie.online`.
