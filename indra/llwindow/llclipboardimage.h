/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef LL_CLIPBOARD_IMAGE_H
#define LL_CLIPBOARD_IMAGE_H
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <limits>
struct LLClipboardImage
{
    bool dib = false;
    std::vector<unsigned char> bytes;
    std::vector<std::string> files;
};
namespace ll_clipboard_image
{
constexpr std::size_t MaxPixels = 16 * 1024 * 1024;
constexpr std::size_t MaxBitmapBytes = MaxPixels * 4 + 4096;
constexpr std::size_t MaxPngBytes = 10 * 1024 * 1024;
constexpr unsigned MaxFiles = 4;
struct Dib
{
    int width = 0, height = 0;
    std::size_t offset = 0, stride = 0;
    unsigned bits = 0;
    bool topDown = false, alpha = false;
};
inline bool inspect(const std::vector<unsigned char>& data, Dib& out)
{
    out = {};
    if (data.size() < 40 || data.size() > MaxBitmapBytes) return false;
    const auto u16 = [&](std::size_t at) { return unsigned(data[at]) | (unsigned(data[at + 1]) << 8); };
    const auto u32 = [&](std::size_t at)
    { return std::uint32_t(data[at]) | (std::uint32_t(data[at + 1]) << 8) | (std::uint32_t(data[at + 2]) << 16) | (std::uint32_t(data[at + 3]) << 24); };
    const auto header = u32(0), width = u32(4), height = u32(8), compression = u32(16);
    if ((header != 40 && header != 108 && header != 124) || header > data.size() || !width || width > 16384 ||
        !height || height == 0x80000000u || u16(12) != 1 || (u16(14) != 24 && u16(14) != 32) ||
        (compression != 0 && compression != 3) || u32(32) != 0) return false;
    const auto absoluteHeight = height & 0x80000000u ? (~height + 1u) : height;
    if (absoluteHeight > 16384 || std::size_t(width) > MaxPixels / absoluteHeight) return false;
    out.width = static_cast<int>(width); out.height = static_cast<int>(absoluteHeight); out.topDown = (height & 0x80000000u) != 0;
    out.bits = u16(14); out.offset = header;
    if (compression == 3)
    {
        if (out.bits != 32 || (header == 40 && data.size() < 52)) return false;
        if (u32(40) != 0x00ff0000u || u32(44) != 0x0000ff00u || u32(48) != 0x000000ffu) return false;
        if (header == 40) out.offset += 12;
    }
    if (header >= 108)
    {
        const auto alpha = u32(52);
        if (alpha && alpha != 0xff000000u) return false;
        out.alpha = out.bits == 32 && alpha != 0;
    }
    out.stride = ((std::size_t(width) * out.bits + 31) / 32) * 4;
    return out.offset <= data.size() && std::size_t(out.height) <= (data.size() - out.offset) / out.stride;
}
// Output scanlines use the viewer's bottom-up RGBA convention.
inline bool unpack(const std::vector<unsigned char>& data, unsigned char* rgba, std::size_t size)
{
    Dib info;
    if (!inspect(data, info) || !rgba || size < std::size_t(info.width) * info.height * 4) return false;
    for (int y = 0; y < info.height; ++y)
    {
        const auto* src = data.data() + info.offset + std::size_t(info.topDown ? info.height - 1 - y : y) * info.stride;
        auto* dst = rgba + std::size_t(y) * info.width * 4;
        for (int x = 0; x < info.width; ++x)
        { dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = info.alpha ? src[3] : 255; src += info.bits / 8; dst += 4; }
    }
    return true;
}
}
#endif
