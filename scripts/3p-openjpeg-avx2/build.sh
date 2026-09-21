#!/usr/bin/env bash
# Build OpenJPEG 2.5.3 with AVX2 and pack it in Linden 3p layout
# (include/openjpeg, lib/release, LICENSES).
#
# Linux:
#   ./scripts/3p-openjpeg-avx2/build.sh
# Windows (Git Bash / Cygwin, with cmake + MSVC in PATH):
#   ./scripts/3p-openjpeg-avx2/build.sh
#
# Output:
#   scripts/3p-openjpeg-avx2/stage/     install tree
#   scripts/3p-openjpeg-avx2/dist/*.tar.zst  autobuild-style tarball
#
# Do not use -march=native: this is a named AVX2 package, same as ReleaseFS_open_AVX2.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OPENJPEG_TAG="${OPENJPEG_TAG:-v2.5.3}"
SRC_DIR="${ROOT}/src/openjpeg"
BUILD_DIR="${ROOT}/build"
STAGE_DIR="${ROOT}/stage"
DIST_DIR="${ROOT}/dist"

uname_s="$(uname -s)"
case "${uname_s}" in
  Linux*)  PLATFORM=linux64 ;;
  Darwin*) echo "macOS OpenJPEG AVX2 is out of scope for this recipe." >&2; exit 1 ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT*) PLATFORM=windows64 ;;
  *)
    if command -v cl.exe >/dev/null 2>&1; then
      PLATFORM=windows64
    else
      echo "Unsupported host: ${uname_s}" >&2
      exit 1
    fi
    ;;
esac

echo "Building OpenJPEG ${OPENJPEG_TAG} AVX2 for ${PLATFORM}"

mkdir -p "${ROOT}/src" "${DIST_DIR}"
if [ ! -d "${SRC_DIR}/.git" ]; then
  git clone --depth 1 --branch "${OPENJPEG_TAG}" https://github.com/uclouvain/openjpeg.git "${SRC_DIR}"
else
  git -C "${SRC_DIR}" fetch --depth 1 origin "refs/tags/${OPENJPEG_TAG}:refs/tags/${OPENJPEG_TAG}" || true
  git -C "${SRC_DIR}" checkout "${OPENJPEG_TAG}"
fi

rm -rf "${BUILD_DIR}" "${STAGE_DIR}"
mkdir -p "${BUILD_DIR}" "${STAGE_DIR}/include/openjpeg" "${STAGE_DIR}/lib/release" "${STAGE_DIR}/LICENSES"

CMAKE_EXTRA=()
if [ "${PLATFORM}" = "windows64" ]; then
  CMAKE_EXTRA+=(
    -DCMAKE_C_FLAGS="/O2 /DNDEBUG /arch:AVX2"
    -DCMAKE_BUILD_TYPE=Release
  )
else
  CMAKE_EXTRA+=(
    -DCMAKE_C_FLAGS="-O3 -mavx2 -DNDEBUG"
    -DCMAKE_BUILD_TYPE=Release
  )
fi

# BUILD_SHARED_LIBS=OFF must produce static openjp2.lib (not a DLL) so
# autobuild layout lib/release/ stays correct on Windows/MSVC.
cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_INSTALL_PREFIX="${STAGE_DIR}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_CODEC=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_DOC=OFF \
  "${CMAKE_EXTRA[@]}"

cmake --build "${BUILD_DIR}" --config Release --parallel
cmake --install "${BUILD_DIR}" --config Release

# Linden 3p layout uses an unversioned include/openjpeg directory.
if [ -d "${STAGE_DIR}/include/openjpeg-2.5" ]; then
  cp -a "${STAGE_DIR}/include/openjpeg-2.5/." "${STAGE_DIR}/include/openjpeg/"
elif [ -d "${STAGE_DIR}/include/openjpeg-2.4" ]; then
  cp -a "${STAGE_DIR}/include/openjpeg-2.4/." "${STAGE_DIR}/include/openjpeg/"
fi

# Static lib may land in lib/ or lib64/ depending on distro.
for candidate in \
  "${STAGE_DIR}/lib/libopenjp2.a" \
  "${STAGE_DIR}/lib64/libopenjp2.a" \
  "${STAGE_DIR}/lib/Release/openjp2.lib" \
  "${STAGE_DIR}/lib/openjp2.lib"
do
  if [ -f "${candidate}" ]; then
    cp "${candidate}" "${STAGE_DIR}/lib/release/"
  fi
done

cp "${SRC_DIR}/LICENSE" "${STAGE_DIR}/LICENSES/openjpeg.txt"
echo "${OPENJPEG_TAG}.avx2" > "${STAGE_DIR}/VERSION.txt"

TARBALL="${DIST_DIR}/openjpeg-${OPENJPEG_TAG#v}-avx2-${PLATFORM}.tar.zst"
if command -v zstd >/dev/null 2>&1; then
  tar -C "${STAGE_DIR}" -cf - include lib LICENSES VERSION.txt | zstd -19 -o "${TARBALL}"
else
  TARBALL="${DIST_DIR}/openjpeg-${OPENJPEG_TAG#v}-avx2-${PLATFORM}.tar.gz"
  tar -C "${STAGE_DIR}" -czf "${TARBALL}" include lib LICENSES VERSION.txt
fi

echo "Installed to ${STAGE_DIR}"
echo "Tarball: ${TARBALL}"
if [ "${PLATFORM}" = "linux64" ]; then
  LIB="${STAGE_DIR}/lib/release/libopenjp2.a"
  if [ -f "${LIB}" ]; then
    ymm_ops="$(objdump -d "${LIB}" | grep -c ymm || true)"
    echo "SIMD check: ${ymm_ops} ymm-using instructions in libopenjp2.a"
    if [ "${ymm_ops}" = "0" ]; then
      echo "WARNING: no ymm ops found; AVX2 kernels may not have been enabled" >&2
    fi
  fi
fi
