/**
 * @file fsavatarpalette.h
 * @brief Bit-preserving conversion to the viewer's skinning palette layout.
 */

#ifndef FS_AVATAR_PALETTE_H
#define FS_AVATAR_PALETTE_H

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#include "sse2neon.h"
#else
#include <xmmintrin.h>
#endif

namespace FSAvatarPalette
{
// Read one complete 16-float matrix and write exactly 12 floats. Both pointers
// must be valid and non-overlapping; 16-byte alignment is not required.
// Keep xyz from each of the first three rows and put translation xyz in w.
inline void packMatrix(const float* matrix, float* output)
{
    const __m128 a = _mm_loadu_ps(matrix);
    const __m128 b = _mm_loadu_ps(matrix + 4);
    const __m128 c = _mm_loadu_ps(matrix + 8);
    const __m128 translation = _mm_loadu_ps(matrix + 12);

    const __m128 ax = _mm_shuffle_ps(a, translation, _MM_SHUFFLE(0, 0, 2, 2));
    const __m128 by = _mm_shuffle_ps(b, translation, _MM_SHUFFLE(1, 1, 2, 2));
    const __m128 cz = _mm_shuffle_ps(c, translation, _MM_SHUFFLE(2, 2, 2, 2));
    _mm_storeu_ps(output,     _mm_shuffle_ps(a, ax, _MM_SHUFFLE(2, 0, 1, 0)));
    _mm_storeu_ps(output + 4, _mm_shuffle_ps(b, by, _MM_SHUFFLE(2, 0, 1, 0)));
    _mm_storeu_ps(output + 8, _mm_shuffle_ps(c, cz, _MM_SHUFFLE(2, 0, 1, 0)));
}
}

#endif
