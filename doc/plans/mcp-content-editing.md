# MCP agent-inventory content editing

E1 hardens existing reads. E6 adds notecard writes; E7 adds script source save/compile. Depends on E0; E7 reuses E6's guarded upload/receipt service. Task inventory and in-world script execution are excluded.

## Harden the existing reads first

inventory_read_notecard and inventory_read_script already exist. Align them with native editor permissions rather than treating inventory metadata Read as unrestricted asset access. Audit native COPY requirements for notecards and COPY+MODIFY for script source, including the native Library exception; do not invent broader godlike access. Use item-bound getInvItemAsset context as native editors do, not a raw arbitrary asset UUID fetch.

Validate actual account/grid/login, own inventory or explicitly native-readable Library scope, item/link/type/asset revision and native restrictions before fetching and again before publishing. If identity, item authority or permissions change during fetch, refuse private payload delivery. Return item/revision, full versus truncated status, native read/write eligibility and a content hash for subsequent comparison. Retain existing inventory Read choices for these existing tools; new write authority does not silently expand old permissions.

## Versioned edits and native UI conflicts

Planned tools: `inventory_content_preview`, `inventory_update_notecard`, `inventory_save_compile_script` and source-bound operation status. Accept UTF-8 text and exact item IDs, not local filenames or arbitrary URLs. Bound request/content/diff/diagnostic bytes to conservative native-supported limits and advertise them.

Require expected asset/revision plus content hash from a complete read. Resolve links consistently, verify native MODIFY policy and reject Library writes. A dirty native editor is a conflict; do not save over it or drive its UI callbacks. Freeze bounded diff, target/content, compiler mode and required classes before Ask; revalidate immediately before upload stages and inventory commit. Update existing agent-inventory assets only; new-item flows separately require Create plus the content-write permission. Do not change permissions, attach items or open editors as a side effect.

New `content_edit` is Ask-capable and distinct from Rename/details. Compile uses explicit selected native supported target (initially raw LSL with the native supported compiler); no implicit global preprocessor preferences, includes or filesystem access. Any future preprocessing must be a separate audited service with bounded inputs, not editor UI automation.

## Notecards

Extract a non-UI service from LLPreviewNotecard.saveIfNeeded using UpdateNotecardAgentInventory and guarded inventory upload queues. Preserve embedded inventory and the native serialization format; never reconstruct a rich notecard from only flattened text. Refuse edits from truncated/incompletely parsed snapshots. Reject edits that would remove/modify embedded items unless that feature is later separately reviewed; the first implementation changes text while preserving embedded items losslessly.

Return resulting asset/revision and server-confirmed versus submitted/unknown status. Native observers reconcile clean editors without overwriting unsaved work. Keep a bounded, session-memory before-image for explicitly requested recovery, not an unbounded private-content log; recovery is a new revision-checked mutation, not automatic rollback.

## Scripts and compiler outcomes

Use UpdateScriptAgent, LLScriptAssetUpload and native upload machinery; never UpdateScriptTask or task inventory. Saving/compiling an inventory script does not rez it, insert it into objects or execute it. Source-save and compilation are distinct: native compilation failure can still save the new source.

Return `source_saved`, `compiled`, bounded structured errors (line/column where native output provides them), item/revision/asset, operation ID and submitted/uncertain state. Do not report CompileFailed as NothingChanged. If a source is saved but diagnostic delivery fails, preserve known saved identity and uncertainty. No automatic resend/recompile following an unknown outcome.

## Async authority and validation

Extract an assistant-aware upload path before reusing the native queue: AssetInventoryUploadCoproc retries failed stage-two uploads by requesting a new uploader. Disable that automatic retry for assistant-owned work; retain ordinary native behavior. Identify each irreversible submission explicitly. Native save capabilities do not provide expected-revision CAS, so local checks/dirty-editor ownership are preflight protections, not atomic protection against another server-side editor. Report this limit and post-submit conflict/uncertainty honestly, without automatic rollback.

Carry the durable operation through fetch, upload queue, capability handshake, upload URL, asset transfer, inventory update, compile/result callback and destructor/deferred work. Check at actual submission boundaries and after every yield. Revocation before the next stage stops unsent work; already submitted source may have changed and must be reported honestly. Never apply an old login's callback to a newer inventory.

A Sol 6.1 Extra High implementation agent may own new content service/Go wrapper/test files in a separate worktree. Root owns shared native inventory extraction, bridge/permissions and final integration. Complete notecards before scripts; do not parallelize two writers on the upload state machine.

Tests: native permission eligibility, denied data after async revocation, stale asset/hash, changed Ask content, links/Library, dirty-editor conflicts, rich/truncated/malformed notecards, upload cancellation at each stage, duplicate/late callbacks, compile failure with source_saved=true, diagnostics limits and unknown outcomes without retry. Add actual dispatch/service tests, not only pure model tests.

Windows: each capability path and unsupported grid, native editor coherence, embedded inventory preservation, compiler success/error, disconnect at each upload boundary and two/five private profiles. Finished code gets independent Sol 6.1 Extra High review; Max is justified for a complex multi-stage upload review. No native acceptance claim from portable checks alone.

## Implemented checkpoint

E6/E7 implement complete reads, bounded diffs, rich-v2 notecard text replacement and raw Mono inventory-script save/compile. V1 notecards remain readable and explicitly report lossless editing unavailable. Snapshots are limited to eight, receipts to 32, text to 65,536 UTF-8 bytes, serialized assets to 262,144 bytes and compiler diagnostics to 8,192 bytes/64 entries. Source-save and compiler results are reported separately; source contents never appear in generic operation receipts.

The assistant upload path disables retries/redirects and guards queue, capability, source and commit boundaries. Native editor conflicts include hidden external watchers, preprocessing/include fetches and pending saves through asset callbacks and deferred inventory transport. Confirmed notecard cache bytes are evicted before reuse; dormant clean editors reload only on a later explicit native open. Native manual behavior remains available.

Independent Sol 6.1 Max review is clean through `c9ded51686`. Four affected portable C++ checks, four native-boundary source checks, registered Go Content dispatch checks and approval XML bindings pass. Native Windows compilation, real server capabilities and graphical/editor acceptance remain pending. Local revision checks cannot provide server-side compare-and-swap or eliminate edits from another client; unknown outcomes are never replayed or rolled back automatically.
