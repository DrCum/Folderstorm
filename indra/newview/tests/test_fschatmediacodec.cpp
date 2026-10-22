#include "fschatmediacodec.h"
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
    const auto gif = decode(gifBytes); assert(gif && gif->frames.size() == 3);
    assert(gif->frames[0].delay == 40 && gif->frames[1].delay == 60 && gif->frames[2].delay == 200);
    assert(gif->frames[0].rgba[0] == 255 && gif->frames[1].rgba[1] == 255 && gif->frames[2].rgba[2] == 255);
    const auto webp = decode(webpBytes); assert(webp && webp->frames.size() == 2);
    assert(webp->frames[0].delay == 80 && webp->frames[1].delay == 120);
    assert(webp->frames[0].rgba[0] == 255 && webp->frames[1].rgba[1] == 255);
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
