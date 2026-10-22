# Chat image decoders

- `stb_image.h`: upstream nothings/stb commit
  `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d` (MIT/public-domain dual license,
  included at the end of the header). Only PNG, JPEG and GIF are enabled.
- `libwebp`: decoder/demux sources and required headers from Google's
  libwebp v1.6.0. Upstream code is unchanged (the license file’s final blank line is normalized); `src/webp/config.h` is the
  Folderstorm scalar, single-threaded configuration. BSD license and patent
  grant are included in `COPYING` and `PATENTS`.

Source archives: https://github.com/nothings/stb and
https://github.com/webmproject/libwebp/releases/tag/v1.6.0.
No encoder, external executable, downloaded runtime or new installer dependency
is used. Decoder-only CMake targets are shared with the focused portable tests.
Update these pins when upstream publishes security fixes.
