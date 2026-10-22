/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fschatanimation.h"
#include "third_party/chat_media/giflib/gif_lib.h"
#include "third_party/chat_media/libwebp/src/webp/decode.h"
#include "third_party/chat_media/libwebp/src/webp/demux.h"
#include <cstring>
#include <limits>

namespace fs_chat_media
{
namespace
{
std::atomic<std::size_t> encodedUsed{0}, decodedUsed{0}, scratchUsed{0}, texturesUsed{0};
struct Reservation
{
    std::atomic<std::size_t>& pool; std::size_t bytes;
    Reservation(std::atomic<std::size_t>& p, std::size_t n) : pool(p), bytes(n) {}
    ~Reservation() { pool.fetch_sub(bytes); }
};
std::shared_ptr<void> reserve(std::atomic<std::size_t>& pool, std::size_t bytes, std::size_t limit = MaxDecodedBytes)
{
    auto used = pool.load();
    do { if (bytes > limit || used > limit - bytes) return {}; }
    while (!pool.compare_exchange_weak(used, used + bytes));
    try { return std::make_shared<Reservation>(pool, bytes); }
    catch (...) { pool.fetch_sub(bytes); return {}; }
}
bool dimensions(int w, int h) { return w > 0 && h > 0 && std::size_t(w) <= MaxPixels / std::size_t(h); }
bool gifMetadata(const std::vector<unsigned char>& bytes, unsigned& frames, unsigned& loops)
{
    std::size_t at = 13; frames = 0; loops = 1; unsigned delay = 100, duration = 0;
    if (bytes.size() < at) return false;
    if (bytes[10] & 128) at += 3u << ((bytes[10] & 7u) + 1u);
    const auto blocks = [&]()
    {
        while (at < bytes.size()) { const auto n = bytes[at++]; if (!n) return true; if (n > bytes.size() - at) return false; at += n; }
        return false;
    };
    while (at < bytes.size())
    {
        const auto marker = bytes[at++];
        if (marker == 0x3b) return frames > 0;
        if (marker == 0x21)
        {
            if (at == bytes.size()) return false;
            const auto label = bytes[at++];
            if (label == 0xf9 && bytes.size() - at >= 6 && bytes[at] == 4)
                delay = frameDelay((int(bytes[at + 3]) * 256 + bytes[at + 2]) * 10);
            if (label == 0xff && bytes.size() - at >= 17 && bytes[at] == 11 &&
                (!std::memcmp(bytes.data() + at + 1, "NETSCAPE2.0", 11) || !std::memcmp(bytes.data() + at + 1, "ANIMEXTS1.0", 11)) &&
                bytes[at + 12] == 3 && bytes[at + 13] == 1)
            { const auto repeat = unsigned(bytes[at + 14]) + unsigned(bytes[at + 15]) * 256; loops = repeat ? repeat + 1 : 0; }
            if (!blocks()) return false;
        }
        else if (marker == 0x2c)
        {
            if (bytes.size() - at < 9 || ++frames > MaxFrames || delay > MaxAnimationMs - duration) return false;
            duration += delay; delay = 100;
            const auto packed = bytes[at + 8]; at += 9;
            if (packed & 128) at += 3u << ((packed & 7u) + 1u);
            if (at >= bytes.size()) return false;
            ++at; if (!blocks()) return false;
        }
        else return false;
    }
    return false;
}
}
AnimationMemory animationMemory() { return {encodedUsed.load(), decodedUsed.load(), scratchUsed.load(), texturesUsed.load()}; }
std::shared_ptr<void> reserveAnimationPixels(std::size_t bytes) { return reserve(decodedUsed, bytes); }
std::shared_ptr<void> reserveAnimationTexture(std::size_t bytes) { return reserve(texturesUsed, bytes, 32 * 1024 * 1024); }

std::shared_ptr<AnimationSource> AnimationSource::create(const std::vector<unsigned char>& bytes, int maximum, bool posterOnly)
{
    if (bytes.empty() || bytes.size() > MaxImageBytes || maximum < 1 || maximum > 1024) return {};
    auto source = std::make_shared<AnimationSource>();
    if (bytes.size() >= 13 && (!std::memcmp(bytes.data(), "GIF87a", 6) || !std::memcmp(bytes.data(), "GIF89a", 6)))
    {
        source->sourceWidth = int(bytes[6]) + int(bytes[7]) * 256;
        source->sourceHeight = int(bytes[8]) + int(bytes[9]) * 256;
        if (!posterOnly && !gifMetadata(bytes, source->frames, source->loops)) return {};
    }
    else
    {
        WebPBitstreamFeatures features{};
        if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) return {};
        source->format = Format::WebP; source->sourceWidth = features.width; source->sourceHeight = features.height;
        if (!posterOnly)
        {
            WebPData data{bytes.data(), bytes.size()};
            auto* demux = WebPDemux(&data); if (!demux) return {};
            source->frames = WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT); source->loops = WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT);
            WebPIterator frame{}; unsigned duration = 0; bool valid = source->frames && source->frames <= MaxFrames;
            if (valid && WebPDemuxGetFrame(demux, 1, &frame))
            {
                do { const auto delay = frameDelay(frame.duration); if (delay > MaxAnimationMs - duration) { valid = false; break; } duration += delay; }
                while (WebPDemuxNextFrame(&frame));
                WebPDemuxReleaseIterator(&frame);
            }
            else valid = false;
            WebPDemuxDelete(demux); if (!valid) return {};
        }
    }
    if (!dimensions(source->sourceWidth, source->sourceHeight)) return {};
    const auto size = fit(source->sourceWidth, source->sourceHeight, maximum, maximum);
    source->width = size.width; source->height = size.height;
    source->mReservation = reserve(encodedUsed, bytes.size()); if (!source->mReservation) return {};
    source->mEncoded = bytes; return source;
}

struct AnimationDecoder::Impl
{
    std::shared_ptr<const AnimationSource> source;
    std::shared_ptr<void> scratch;
    GifFileType* gif = nullptr; WebPAnimDecoder* webp = nullptr;
    std::vector<unsigned char> canvas, previous, line;
    GraphicsControlBlock control{DISPOSAL_UNSPECIFIED, false, 10, NO_TRANSPARENT_COLOR}, prior = control;
    GifImageDesc old{};
    std::size_t offset = 0; int timestamp = 0;
    unsigned count = 0, duration = 0;
    bool error = false, done = false;
    const std::atomic<bool>* cancellation = nullptr;
    explicit Impl(std::shared_ptr<const AnimationSource> src) : source(std::move(src))
    {
        if (!source) { error = true; return; }
        const auto pixels = std::size_t(source->sourceWidth) * source->sourceHeight;
        // Covers both full GIF canvases, row, giflib state/palettes; WebP owns up to four canvases.
        scratch = reserve(scratchUsed, pixels * (source->format == AnimationSource::Format::Gif ? 8u : 16u) + source->sourceWidth + 256 * 1024);
        if (!scratch) { error = true; return; }
        if (source->format == AnimationSource::Format::Gif)
        {
            int status = 0; gif = DGifOpen(this, read, &status);
            if (!gif) { error = true; return; }
            canvas.resize(pixels * 4); line.resize(source->sourceWidth);
        }
        else
        {
            WebPData data{source->encoded().data(), source->encoded().size()}; WebPAnimDecoderOptions options{};
            if (!WebPAnimDecoderOptionsInit(&options)) { error = true; return; }
            options.color_mode = MODE_RGBA; options.use_threads = 0; webp = WebPAnimDecoderNew(&data, &options); error = !webp;
        }
    }
    ~Impl() { if (gif) { int status = 0; DGifCloseFile(gif, &status); } if (webp) WebPAnimDecoderDelete(webp); }
    static int read(GifFileType* file, GifByteType* output, int requested)
    {
        auto& self = *static_cast<Impl*>(file->UserData); const auto& bytes = self.source->encoded();
        if (requested <= 0 || (self.cancellation && *self.cancellation)) return 0;
        const auto n = (std::min)(std::size_t(requested), bytes.size() - self.offset);
        std::memcpy(output, bytes.data() + self.offset, n); self.offset += n; return static_cast<int>(n);
    }
    bool gifFrame()
    {
        if (count)
        {
            if (prior.DisposalMode == DISPOSE_PREVIOUS && !previous.empty()) canvas = previous;
            else if (prior.DisposalMode == DISPOSE_BACKGROUND)
                for (int y = old.Top; y < old.Top + old.Height; ++y)
                    std::memset(canvas.data() + (std::size_t(y) * source->sourceWidth + old.Left) * 4, 0, std::size_t(old.Width) * 4);
        }
        GifRecordType type{};
        while (!cancellation || !*cancellation)
        {
            if (DGifGetRecordType(gif, &type) == GIF_ERROR) return false;
            if (type == TERMINATE_RECORD_TYPE) { done = true; return false; }
            if (type == EXTENSION_RECORD_TYPE)
            {
                int code = 0; GifByteType* block = nullptr;
                if (DGifGetExtension(gif, &code, &block) == GIF_ERROR) return false;
                if (code == GRAPHICS_EXT_FUNC_CODE && block && block[0] == 4)
                    if (DGifExtensionToGCB(4, block + 1, &control) == GIF_ERROR) return false;
                while (block) { if ((cancellation && *cancellation) || DGifGetExtensionNext(gif, &block) == GIF_ERROR) return false; }
                continue;
            }
            if (type != IMAGE_DESC_RECORD_TYPE || DGifGetImageHeader(gif) == GIF_ERROR) return false;
            const auto& image = gif->Image;
            if (image.Width < 1 || image.Height < 1 || image.Left < 0 || image.Top < 0 ||
                image.Width > source->sourceWidth - image.Left || image.Height > source->sourceHeight - image.Top) return false;
            const auto* palette = image.ColorMap ? image.ColorMap : gif->SColorMap; if (!palette) return false;
            if (control.DisposalMode == DISPOSE_PREVIOUS) previous = canvas;
            const auto row = [&](int y)
            {
                if ((cancellation && *cancellation) || DGifGetLine(gif, line.data(), image.Width) == GIF_ERROR) return false;
                for (int x = 0; x < image.Width; ++x)
                {
                    const auto index = line[x]; if (index == control.TransparentColor) continue;
                    if (index >= palette->ColorCount) return false;
                    const auto color = palette->Colors[index]; auto* p = canvas.data() + (std::size_t(y + image.Top) * source->sourceWidth + x + image.Left) * 4;
                    p[0] = color.Red; p[1] = color.Green; p[2] = color.Blue; p[3] = 255;
                }
                return true;
            };
            if (image.Interlace)
            { const int starts[]{0,4,2,1}, steps[]{8,8,4,2}; for (int pass = 0; pass < 4; ++pass) for (int y = starts[pass]; y < image.Height; y += steps[pass]) if (!row(y)) return false; }
            else for (int y = 0; y < image.Height; ++y) if (!row(y)) return false;
            old = image; old.ColorMap = nullptr; prior = control; control = {DISPOSAL_UNSPECIFIED, false, 10, NO_TRANSPARENT_COLOR}; return true;
        }
        return false;
    }
};
AnimationDecoder::AnimationDecoder(std::shared_ptr<const AnimationSource> source) : m(new Impl(std::move(source))) {}
AnimationDecoder::~AnimationDecoder() = default;
bool AnimationDecoder::ended() const { return m->done; }
bool AnimationDecoder::failed() const { return m->error; }
std::shared_ptr<Frame> AnimationDecoder::next(const std::atomic<bool>& cancelled)
{
    if (cancelled || m->done || m->error) return {};
    m->cancellation = &cancelled; const unsigned char* pixels = nullptr; unsigned delay = 100;
    if (m->gif)
    {
        if (!m->gifFrame()) { m->error = !m->done && !cancelled; return {}; }
        pixels = m->canvas.data(); delay = frameDelay(m->prior.DelayTime * 10);
    }
    else
    {
        if (!m->webp || !WebPAnimDecoderHasMoreFrames(m->webp)) { m->done = true; return {}; }
        unsigned char* data = nullptr; int timestamp = 0;
        if (!WebPAnimDecoderGetNext(m->webp, &data, &timestamp) || timestamp < m->timestamp) { m->error = true; return {}; }
        pixels = data; delay = frameDelay(timestamp - m->timestamp); m->timestamp = timestamp;
    }
    if (++m->count > MaxFrames || delay > MaxAnimationMs - m->duration) { m->error = true; return {}; }
    m->duration += delay;
    auto frame = std::make_shared<Frame>(); const auto& s = *m->source;
    frame->reservation = reserveAnimationPixels(std::size_t(s.width) * s.height * 4); if (!frame->reservation) { m->error = true; return {}; }
    frame->rgba.resize(std::size_t(s.width) * s.height * 4); frame->delay = delay;
    for (int y = 0; y < s.height; ++y)
    {
        if (cancelled) return {};
        for (int x = 0; x < s.width; ++x)
            std::memcpy(frame->rgba.data() + (std::size_t(s.height - 1 - y) * s.width + x) * 4,
                pixels + (std::size_t(y) * s.sourceHeight / s.height * s.sourceWidth + std::size_t(x) * s.sourceWidth / s.width) * 4, 4);
    }
    return frame;
}
}
