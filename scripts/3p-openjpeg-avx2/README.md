# AVX2 OpenJPEG 3p

Rebuilds [OpenJPEG 2.5.3](https://github.com/uclouvain/openjpeg/releases/tag/v2.5.3) (same tag as Linden `3p-openjpeg` v2.5.3-r1) with named **AVX2** flags (not `-march=native`) and packs it in the Linden autobuild layout (`include/openjpeg`, `lib/release`, `LICENSES`).

Linux/Windows **AVX2 viewer builds** (`USE_AVX2_OPTIMIZATION`) consume this library via `indra/cmake/OpenJPEG.cmake`:

1. Use `scripts/3p-openjpeg-avx2/stage/` if you already ran `build.sh`.
2. Otherwise **FetchContent** of `uclouvain/openjpeg` `v2.5.3` with `-mavx2` / `/arch:AVX2`.

Non-AVX2 configurations (`ReleaseFS_open` without `--avx2`) keep the stock Linden tarball in `autobuild.xml`. **darwin64** always stays on that stock package. Replacing the committed linux64/windows64 URLs with an AVX2 tarball would make SSE2 open builds crash on CPUs without AVX2 during JPEG2000 decode.

This fork's daily AVX2 target is `ReleaseFS_AVX2` on Linux (`--avx2 --opensim`, no Kakadu). On Windows use `ReleaseFS_open` plus `--avx2`. Those binaries need an AVX2 CPU.

## Linux

```bash
./scripts/3p-openjpeg-avx2/build.sh
./scripts/3p-openjpeg-avx2/smoke.sh
```

Outputs:

- `scripts/3p-openjpeg-avx2/stage/` — `include/openjpeg`, `lib/release/libopenjp2.a`
- `scripts/3p-openjpeg-avx2/dist/openjpeg-2.5.3-avx2-linux64.tar.zst`

To point a **local** autobuild at that tarball (optional; AVX2 viewer builds do not need this):

```bash
autobuild installables edit openjpeg platform=linux64 hash=<sha1> \
  url=file:///$PWD/scripts/3p-openjpeg-avx2/dist/openjpeg-2.5.3-avx2-linux64.tar.zst
```

Leave `platform=darwin64` on the Linden URL. Do not commit a `file://` path.

## Windows

Run the same `build.sh` from Git Bash after `cmake` and MSVC (`cl`) are on `PATH`. It passes `/arch:AVX2` and writes `openjpeg-2.5.3-avx2-windows64.tar.zst`. Viewer `--avx2` configure also compiles OpenJPEG via FetchContent when `stage/` is missing.

macOS is not supported here (universal x86_64+arm64 `lipo` is out of scope).
