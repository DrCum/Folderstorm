/**
 * @file llimagej2cpack.h
 * @brief Planar int32 <-> interleaved uint8 conversion for OpenJPEG (Y-flipped).
 *
 * JPEG 2000 / OpenJPEG store the first row at the top. LLImageRaw is GL-style
 * bottom-up. These helpers flip while packing so callers do not nest
 * per-pixel, per-channel loops.
 */

#ifndef LL_LLIMAGEJ2CPACK_H
#define LL_LLIMAGEJ2CPACK_H

#include <cstddef>
#include <cstdint>

namespace llimagej2cpack
{

inline std::uint8_t clamp_u8(int sample)
{
    if (sample < 0)
    {
        return 0;
    }
    if (sample > 255)
    {
        return 255;
    }
    return static_cast<std::uint8_t>(sample);
}

// Planar OpenJPEG int32 planes -> interleaved uint8, Y-flipped.
template <typename Sample>
inline void pack_planar_int32_to_interleaved_u8(
    std::uint8_t* dst,
    int dst_channels,
    const Sample* const* planes,
    int first_plane,
    int channels,
    unsigned width,
    unsigned height,
    unsigned src_stride)
{
    if (!dst || channels <= 0 || dst_channels <= 0 || width == 0 || height == 0)
    {
        return;
    }

    for (unsigned y = 0; y < height; ++y)
    {
        const unsigned src_y = height - 1 - y;
        std::uint8_t* row = dst + static_cast<std::size_t>(y) * width * static_cast<unsigned>(dst_channels);

        if (channels == 1)
        {
            const Sample* p0 = planes[first_plane] + static_cast<std::size_t>(src_y) * src_stride;
            for (unsigned x = 0; x < width; ++x)
            {
                row[x * static_cast<unsigned>(dst_channels)] = clamp_u8(static_cast<int>(p0[x]));
            }
        }
        else if (channels == 3 && dst_channels == 3)
        {
            const Sample* p0 = planes[first_plane] + static_cast<std::size_t>(src_y) * src_stride;
            const Sample* p1 = planes[first_plane + 1] + static_cast<std::size_t>(src_y) * src_stride;
            const Sample* p2 = planes[first_plane + 2] + static_cast<std::size_t>(src_y) * src_stride;
            for (unsigned x = 0; x < width; ++x)
            {
                row[x * 3]     = clamp_u8(static_cast<int>(p0[x]));
                row[x * 3 + 1] = clamp_u8(static_cast<int>(p1[x]));
                row[x * 3 + 2] = clamp_u8(static_cast<int>(p2[x]));
            }
        }
        else if (channels == 4 && dst_channels == 4)
        {
            const Sample* p0 = planes[first_plane] + static_cast<std::size_t>(src_y) * src_stride;
            const Sample* p1 = planes[first_plane + 1] + static_cast<std::size_t>(src_y) * src_stride;
            const Sample* p2 = planes[first_plane + 2] + static_cast<std::size_t>(src_y) * src_stride;
            const Sample* p3 = planes[first_plane + 3] + static_cast<std::size_t>(src_y) * src_stride;
            for (unsigned x = 0; x < width; ++x)
            {
                row[x * 4]     = clamp_u8(static_cast<int>(p0[x]));
                row[x * 4 + 1] = clamp_u8(static_cast<int>(p1[x]));
                row[x * 4 + 2] = clamp_u8(static_cast<int>(p2[x]));
                row[x * 4 + 3] = clamp_u8(static_cast<int>(p3[x]));
            }
        }
        else
        {
            for (unsigned x = 0; x < width; ++x)
            {
                for (int c = 0; c < channels; ++c)
                {
                    const Sample* plane =
                        planes[first_plane + c] + static_cast<std::size_t>(src_y) * src_stride;
                    row[x * static_cast<unsigned>(dst_channels) + static_cast<unsigned>(c)] =
                        clamp_u8(static_cast<int>(plane[x]));
                }
            }
        }
    }
}

// Interleaved uint8 (LLImageRaw) -> planar int32, Y-flipped.
template <typename Sample>
inline void unpack_interleaved_u8_to_planar_int32(
    Sample* const* planes,
    const std::uint8_t* src,
    int channels,
    unsigned width,
    unsigned height)
{
    if (!src || channels <= 0 || width == 0 || height == 0)
    {
        return;
    }

    const unsigned src_stride = width * static_cast<unsigned>(channels);

    for (unsigned dst_y = 0; dst_y < height; ++dst_y)
    {
        const unsigned src_y = height - 1 - dst_y;
        const std::uint8_t* row = src + static_cast<std::size_t>(src_y) * src_stride;
        const std::size_t plane_row = static_cast<std::size_t>(dst_y) * width;

        if (channels == 1)
        {
            Sample* p0 = planes[0] + plane_row;
            for (unsigned x = 0; x < width; ++x)
            {
                p0[x] = row[x];
            }
        }
        else if (channels == 3)
        {
            Sample* p0 = planes[0] + plane_row;
            Sample* p1 = planes[1] + plane_row;
            Sample* p2 = planes[2] + plane_row;
            for (unsigned x = 0; x < width; ++x)
            {
                p0[x] = row[x * 3];
                p1[x] = row[x * 3 + 1];
                p2[x] = row[x * 3 + 2];
            }
        }
        else if (channels == 4)
        {
            Sample* p0 = planes[0] + plane_row;
            Sample* p1 = planes[1] + plane_row;
            Sample* p2 = planes[2] + plane_row;
            Sample* p3 = planes[3] + plane_row;
            for (unsigned x = 0; x < width; ++x)
            {
                p0[x] = row[x * 4];
                p1[x] = row[x * 4 + 1];
                p2[x] = row[x * 4 + 2];
                p3[x] = row[x * 4 + 3];
            }
        }
        else
        {
            for (unsigned x = 0; x < width; ++x)
            {
                for (int c = 0; c < channels; ++c)
                {
                    planes[c][plane_row + x] = row[x * static_cast<unsigned>(channels) + static_cast<unsigned>(c)];
                }
            }
        }
    }
}

} // namespace llimagej2cpack

#endif
