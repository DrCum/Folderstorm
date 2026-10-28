/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fschatmediacodec.h"
#include <cstring>
#include <cstdlib>
#include <limits>
#include <cstddef>
#include <mutex>
#include "third_party/chat_media/libwebp/src/webp/decode.h"
#include "third_party/chat_media/libwebp/src/webp/demux.h"
namespace
{
// stb's PNG inflater can grow past the declared canvas size. Bound *all* of
// its allocations, including inflate buffers and malformed GIF frame tables.
struct alignas(std::max_align_t) Allocation { std::size_t size; };
thread_local std::size_t allocated = 0;
void* imageAlloc(std::size_t size)
{
    if (size > fs_chat_media::MaxDecodedBytes - allocated) return nullptr;
    auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!block) return nullptr;
    block->size = size; allocated += size; return block + 1;
}
void imageFree(void* pointer)
{
    if (!pointer) return;
    auto* block = static_cast<Allocation*>(pointer) - 1;
    allocated -= block->size; std::free(block);
}
void* imageRealloc(void* pointer, std::size_t size)
{
    if (!pointer) return imageAlloc(size);
    auto* block = static_cast<Allocation*>(pointer) - 1; const auto old = block->size;
    if (size > fs_chat_media::MaxDecodedBytes - (allocated - old)) return nullptr;
    block = static_cast<Allocation*>(std::realloc(block, sizeof(Allocation) + size));
    if (!block) return nullptr;
    allocated = allocated - old + size; block->size = size; return block + 1;
}
}
#define STBI_MALLOC(size) imageAlloc(size)
#define STBI_REALLOC(pointer, size) imageRealloc(pointer, size)
#define STBI_FREE(pointer) imageFree(pointer)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_MAX_DIMENSIONS 16384
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include "third_party/chat_media/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs_chat_media
{
namespace
{
unsigned gifFrames(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < 13) return 0;
    std::size_t at = 13; unsigned count = 0;
    if (bytes[10] & 128) at += 3u << ((bytes[10] & 7u) + 1u);
    const auto blocks = [&]()
    {
        while (at < bytes.size())
        {
            const auto size = bytes[at++]; if (!size) return true;
            if (size > bytes.size() - at) return false;
            at += size;
        }
        return false;
    };
    while (at < bytes.size())
    {
        const auto marker = bytes[at++];
        if (marker == 0x3b) return count;
        if (marker == 0x21) { if (at == bytes.size()) return 0; ++at; if (!blocks()) return 0; }
        else if (marker == 0x2c)
        {
            if (bytes.size() - at < 9 || ++count > MaxFrames) return 0;
            const auto packed = bytes[at + 8]; at += 9;
            if (packed & 128) at += 3u << ((packed & 7u) + 1u);
            if (at >= bytes.size()) return 0;
            ++at; if (!blocks()) return 0;
        }
        else return 0;
    }
    return 0;
}
bool validDimensions(int width, int height)
{ return width > 0 && height > 0 && std::size_t(width) <= MaxPixels / std::size_t(height); }
unsigned jpegOrientation(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < 4 || bytes[0] != 255 || bytes[1] != 216) return 1;
    for (std::size_t at = 2; at + 4 <= bytes.size();)
    {
        if (bytes[at++] != 255) return 1;
        while (at < bytes.size() && bytes[at] == 255) ++at;
        if (at >= bytes.size()) return 1;
        const auto marker = bytes[at++]; if (marker == 218 || marker == 217) return 1;
        if (at + 2 > bytes.size()) return 1;
        const auto length = std::size_t(bytes[at]) * 256 + bytes[at + 1];
        if (length < 2 || length > bytes.size() - at) return 1;
        const auto end = at + length;
        if (marker == 225 && length >= 16 && std::memcmp(bytes.data() + at + 2, "Exif\0\0", 6) == 0)
        {
            const auto base = at + 8; const bool little = bytes[base] == 'I' && bytes[base + 1] == 'I';
            if (!little && !(bytes[base] == 'M' && bytes[base + 1] == 'M')) return 1;
            const auto read = [&](std::size_t offset, unsigned size) -> std::uint32_t
            {
                if (offset > end || size > end - offset) return 0;
                std::uint32_t value = 0;
                for (unsigned i = 0; i < size; ++i) value = (value << 8) | bytes[offset + (little ? size - 1 - i : i)];
                return value;
            };
            const auto offset = read(base + 4, 4);
            if (offset > end - base || end - base - offset < 2) return 1;
            const auto directory = base + offset; const auto count = read(directory, 2);
            if (count > 256 || count > (end - directory - 2) / 12) return 1;
            for (unsigned i = 0; i < count; ++i)
            {
                const auto item = directory + 2 + i * 12;
                if (read(item, 2) == 274 && read(item + 2, 2) == 3 && read(item + 4, 4) == 1)
                { const auto value = read(item + 8, 2); return value >= 1 && value <= 8 ? value : 1; }
            }
        }
        at = end;
    }
    return 1;
}
void orient(Image& image, unsigned orientation)
{
    if (orientation == 1) return;
    const auto width = image.width, height = image.height;
    if (orientation >= 5) std::swap(image.width, image.height);
    for (auto& frame : image.frames)
    {
        std::vector<unsigned char> output(frame.rgba.size());
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
        {
            int sx = x, sy = y;
            switch (orientation)
            {
                case 2: sx = width - 1 - x; break;
                case 3: sx = width - 1 - x; sy = height - 1 - y; break;
                case 4: sy = height - 1 - y; break;
                case 5: sx = y; sy = x; break;
                case 6: sx = y; sy = height - 1 - x; break;
                case 7: sx = width - 1 - y; sy = height - 1 - x; break;
                case 8: sx = width - 1 - y; sy = x; break;
                default: break;
            }
            std::memcpy(output.data() + (std::size_t(image.height - 1 - y) * image.width + x) * 4,
                frame.rgba.data() + (std::size_t(height - 1 - sy) * width + sx) * 4, 4);
        }
        frame.rgba = std::move(output);
    }
}
Frame scaledFrame(const unsigned char* pixels, int width, int height, Fit target, unsigned delay)
{
    Frame frame; frame.delay = delay; frame.rgba.resize(std::size_t(target.width) * target.height * 4);
    for (int y = 0; y < target.height; ++y)
        for (int x = 0; x < target.width; ++x)
        {
            const auto src = (std::size_t(y) * height / target.height * width + std::size_t(x) * width / target.width) * 4;
            // GL/UI textures use bottom-up scanlines.
            const auto dst = (std::size_t(target.height - 1 - y) * target.width + x) * 4;
            std::memcpy(frame.rgba.data() + dst, pixels + src, 4);
        }
    return frame;
}
}
std::shared_ptr<Image> decode(const std::vector<unsigned char>& bytes, int maxDimension, bool firstFrame)
{
    static std::mutex decodeMutex;
    std::lock_guard<std::mutex> lock(decodeMutex);
    if (bytes.empty() || bytes.size() > MaxImageBytes || maxDimension < 1 || maxDimension > 1024) return {};
    auto image = std::make_shared<Image>(); int width = 0, height = 0, components = 0;
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0)
    {
        WebPBitstreamFeatures features{};
        if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK || !validDimensions(features.width, features.height)) return {};
        width = features.width; height = features.height;
        WebPData data{bytes.data(), bytes.size()}; WebPAnimDecoderOptions options{};
        if (!WebPAnimDecoderOptionsInit(&options)) return {};
        options.color_mode = MODE_RGBA; options.use_threads = 0;
        const auto deleter = [](WebPAnimDecoder* value) { WebPAnimDecoderDelete(value); };
        std::unique_ptr<WebPAnimDecoder, decltype(deleter)> decoder(WebPAnimDecoderNew(&data, &options), deleter);
        WebPAnimInfo info{};
        if (!decoder || !WebPAnimDecoderGetInfo(decoder.get(), &info) || !info.frame_count || info.frame_count > MaxFrames) return {};
        const auto size = fit(width, height, maxDimension, maxDimension); image->width = size.width; image->height = size.height;
        const auto output = std::size_t(size.width) * size.height * 4;
        const bool animate = std::size_t(width) * height * 4 * 4 + output * info.frame_count <= MaxDecodedBytes;
        const auto count = animate && !firstFrame ? info.frame_count : 1u; int previous = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned char* frame = nullptr; int timestamp = 0;
            if (!WebPAnimDecoderGetNext(decoder.get(), &frame, &timestamp) || !frame) return {};
            image->frames.push_back(scaledFrame(frame, width, height, size, frameDelay(timestamp - previous))); previous = timestamp;
        }
    }
    else
    {
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &components) || !validDimensions(width, height)) return {};
        const auto size = fit(width, height, maxDimension, maxDimension); image->width = size.width; image->height = size.height;
        const bool gif = bytes.size() >= 6 && (std::memcmp(bytes.data(), "GIF87a", 6) == 0 || std::memcmp(bytes.data(), "GIF89a", 6) == 0);
        image->gif = gif;
        const auto frames = gif ? gifFrames(bytes) : 1u; if (!frames) return {};
        const auto canvas = std::size_t(width) * height * 4;
        const auto output = std::size_t(size.width) * size.height * 4;
        const bool animate = gif && !firstFrame && canvas * (frames + 4u) + output * frames <= MaxDecodedBytes;
        int* delays = nullptr; int loadedFrames = 1;
        auto* pixels = animate ? stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays, &width, &height, &loadedFrames, &components, 4) :
            stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &components, 4);
        const auto release = [](void* value) { stbi_image_free(value); };
        std::unique_ptr<unsigned char, decltype(release)> owned(pixels, release);
        std::unique_ptr<int, decltype(release)> ownedDelays(delays, release);
        if (!pixels || loadedFrames < 1 || static_cast<unsigned>(loadedFrames) > frames) return {};
        for (int i = 0; i < loadedFrames; ++i)
            image->frames.push_back(scaledFrame(pixels + std::size_t(i) * canvas, width, height, size, delays ? frameDelay(delays[i]) : 100));
    }
    if (!image->frames.empty()) orient(*image, jpegOrientation(bytes));
    return image->frames.empty() ? std::shared_ptr<Image>{} : image;
}
}
