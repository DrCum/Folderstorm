#include "fschatmediacodec.h"
#include "fschatanimation.h"
#include "fixtures/chat_media_images.inc"
#include <cassert>
#include <cstring>
using namespace fs_chat_media;
int main()
{
    const auto png = decode(pngBytes); assert(png && png->width == 3 && png->height == 2 && png->frames.size() == 1);
    assert(png->frames[0].rgba[2] == 255); // Bottom scanline is blue; GL orientation.
    assert(png->frames[0].rgba[12] == 255); // Top scanline is red.
    const auto jpeg = decode(jpegBytes); assert(jpeg && jpeg->frames.size() == 1);
    const auto rotated = decode(rotatedJpegBytes); assert(rotated && rotated->width == 2 && rotated->height == 3);
    const std::atomic<bool> live{false};
    const auto gif = decode(gifBytes); assert(gif && gif->frames.size() == 1 && gif->animation && gif->animation->frames == 3);
    AnimationDecoder gifs(gif->animation); auto a = gifs.next(live), b = gifs.next(live), c = gifs.next(live);
    assert(a && b && c && a->delay == 40 && b->delay == 60 && c->delay == 200);
    assert(a->rgba[0] == 255 && b->rgba[1] == 255 && c->rgba[2] == 255);
    assert(!gifs.next(live) && gifs.ended() && !gifs.failed());
    const auto webp = decode(webpBytes); assert(webp && webp->frames.size() == 1 && webp->animation);
    AnimationDecoder webps(webp->animation); a = webps.next(live); b = webps.next(live);
    assert(a && b && a->delay == 80 && b->delay == 120 && a->rgba[0] == 255 && b->rgba[1] == 255);
    assert(!decode({}) && !decode(std::vector<unsigned char>(MaxImageBytes + 1)));
    assert(!decode(std::vector<unsigned char>{'<','h','t','m','l','>'}));
    auto huge = pngBytes; huge[16] = 0x7f; huge[17] = 0xff; assert(!decode(huge));
    auto corrupt = gifBytes; corrupt.resize(16); assert(!decode(corrupt));
    for (std::size_t i = 0; i < webpBytes.size(); i += 7)
        assert(!decode(std::vector<unsigned char>(webpBytes.begin(), webpBytes.begin() + i)));
    for (const auto& source : {pngBytes, gifBytes, webpBytes})
        for (std::size_t i = 0; i < source.size(); i += 5)
        { auto mutation = source; mutation[i] ^= 0xff; const auto value = decode(mutation); if (value) assert(value->bytes() <= MaxDecodedBytes); }
}
