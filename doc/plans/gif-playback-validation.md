# Long GIF/WebP playback checkpoint

First implementation checkpoint of `folderstorm-gif-board-klipy.md`; starts above
#84. Native Windows UI/link testing remains pending. No installer or release.

- GIF and WebP now retain a first-frame poster plus bounded encoded source,
  rather than all decoded frames. The public incremental giflib API handles GIF
  raster input; Folderstorm composites disposal, transparency and interlacing.
  WebP uses its sequential animation API. Source limits are 15 MiB / 16 megapixels,
  10,000 frame records and ten minutes per loop. Posters remain possible when
  full animation metadata exceeds the limit or has an invalid tail.
- Two reusable frame workers admit visible playback fairly. Animation demand
  expires when draw subscriptions stop; decoder/ready-frame memory releases
  after one idle second. Paused cards retain their uploaded picture. Existing
  incoming-chat autoplay choices and three-card per-frame policy remain intact;
  the later board uses its own visible-tile policy.
- Process-wide reservations cap encoded animation sources, decoded poster/frame
  allocations and decoder scratch separately at 64 MiB, plus 32 MiB for padded
  chat-preview GPU textures. Native upload review thumbnails remain separate
  from this preview texture pool. Futures survive cancellation until collected,
  and shutdown cancels/joins workers before HTTP/GL cleanup.
- GIFLIB pin: `a8e3114a81f0987a61d06a41c99fd7cc2d58232c`, version 6.1.3
  plus SF #199/CVE-2026-26740 and #203 fixes. Decoder-only C sources and MIT
  notice are bundled, with no configure-time downloads or runtime DLLs.

Focused validation: all 13 media/upload CTest entries passed, including generated
119/120/121/300/1,000/3,000-frame animations, bounded frame retention, cancellation,
loop metadata, long first-frame extraction, malformed input and the GPU pool.
A four-frame partial/transparency/disposal fixture compares pixels with Pillow
12.3 reference output. The native service harness exercises its actual two-worker
admission, fair scheduling and cancelled results at stubbed viewer/decoder
boundaries. Actual decoder tests pass ASan/UBSan; LeakSanitizer is disabled because
the cloud sandbox cannot inspect its threads. Affected native media syntax checks
pass against available Linux viewer headers. No broad viewer build was run.

Windows acceptance:

1. Receive/play a GIF beyond 120 frames, including one uploaded unchanged through
   native Nearby/IM. Test static fallback and malformed/oversized files.
2. Pause/resume, scroll off/on, close/reopen, open the larger preview, switch
   login or add a sender/group restriction during decoding. No late image may
   return after invalidation, and original link/clipboard behavior must survive.
3. Test partial transparent frames, restore-to-previous/background, finite loops,
   GIF and animated WebP timing. Watch CPU/GPU memory with repeated URLs, several
   open previews and long scrolling sessions. Reduce motion and restart.
