#!/usr/bin/env bash
# Verify the AVX2 OpenJPEG 3p: SIMD in libopenjp2, plus encode/decode smoke
# (full / truncated prefixes / discard levels / 1- and 4-channel).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "${ROOT}/../.." && pwd)"
STAGE="${ROOT}/stage"
LIB="${STAGE}/lib/release/libopenjp2.a"
INC="${STAGE}/include/openjpeg"

if [ ! -f "${LIB}" ]; then
  echo "Building AVX2 OpenJPEG first..."
  "${ROOT}/build.sh"
fi

echo "== SIMD check =="
ymm_ops="$(objdump -d "${LIB}" | grep -c ymm || true)"
vpaddd="$(objdump -d "${LIB}" | grep -c vpaddd || true)"
echo "libopenjp2.a: ${ymm_ops} ymm ops, ${vpaddd} vpaddd"
if [ "${ymm_ops}" = "0" ]; then
  echo "ERROR: no ymm instructions; AVX2 DWT/MCT was not compiled in" >&2
  exit 1
fi

echo "== OpenJPEG encode/decode smoke =="
cc -O2 -std=c11 -I "${INC}" "${ROOT}/roundtrip.c" "${LIB}" -lm -lpthread -o /tmp/openjpeg-avx2-roundtrip
/tmp/openjpeg-avx2-roundtrip

echo "== pack/unpack (scalar and -mavx2) =="
g++ -O2 -std=c++17 -I "${REPO}/indra/llimagej2coj" \
  "${REPO}/indra/llimagej2coj/tests/test_j2cpack.cpp" -o /tmp/test_j2cpack
/tmp/test_j2cpack
g++ -O2 -std=c++17 -mavx2 -I "${REPO}/indra/llimagej2coj" \
  "${REPO}/indra/llimagej2coj/tests/test_j2cpack.cpp" -o /tmp/test_j2cpack_avx2
/tmp/test_j2cpack_avx2

echo "== pack + OpenJPEG encode/decode (1-channel and 1024 RGBA) =="
g++ -O2 -std=c++17 -mavx2 -I "${INC}" -I "${REPO}/indra/llimagej2coj" \
  "${REPO}/indra/llimagej2coj/tests/test_j2c_roundtrip.cpp" "${LIB}" -lm -lpthread \
  -o /tmp/test_j2c_roundtrip
/tmp/test_j2c_roundtrip

echo "All OpenJPEG AVX2 smoke checks passed."
