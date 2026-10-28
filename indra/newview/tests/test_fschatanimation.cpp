#include "fschatanimation.h"
#include "fixtures/chat_media_images.inc"
#include "fixtures/chat_animation_disposal.inc"
#include <cassert>
using namespace fs_chat_media;
// Repeating the fixture's complete three-frame record set preserves palettes/LZW.
std::vector<unsigned char> longGif(unsigned count)
{
    std::vector<unsigned char> out(gifBytes.begin(), gifBytes.begin() + 44);
    for (unsigned i = 0; i < count; ++i)
    {
        const std::size_t starts[]{44, 71, 110}, ends[]{71, 110, 149};
        out.insert(out.end(), gifBytes.begin() + starts[i % 3], gifBytes.begin() + ends[i % 3]);
    }
    out.push_back(59); return out;
}
int main()
{
    const std::atomic<bool> live{false}, cancelled{true};
    for (unsigned count : {119u, 120u, 121u, 300u, 1000u, 3000u})
    {
        auto bytes = longGif(count); auto image = decode(bytes);
        assert(image && image->frames.size() == 1 && image->animation && image->animation->frames == count);
        assert(decode(bytes, 320, true) && !decode(bytes, 320, true)->animation);
        auto before = animationMemory();
        {
            AnimationDecoder player(image->animation);
            for (unsigned i = 0; i < count; ++i)
            {
                auto frame = player.next(live); assert(frame && frame->rgba.size() == 24 && frame->rgba[i % 3] == 255);
                assert(animationMemory().decoded <= before.decoded + 24); // No frame-history accumulation.
            }
            assert(!player.next(live) && player.ended());
        }
        assert(animationMemory().scratch == before.scratch);
        AnimationDecoder player(image->animation); assert(!player.next(cancelled));
    }
    { auto tooLong = longGif(MaxFrames + 1); auto image = decode(tooLong); assert(image && !image->animation); assert(decode(tooLong, 320, true)); }
    { auto tail = longGif(300); tail.resize(tail.size() - 4); auto poster = decode(tail, 320, true); assert(poster && !poster->animation); }
    // A missing/invalid first image does not become an accepted poster.
    assert(!decode(std::vector<unsigned char>(gifBytes.begin(), gifBytes.begin() + 50), 320, true));
    auto memory = animationMemory(); assert(memory.encoded == 0 && memory.decoded == 0 && memory.scratch == 0);
    // Disposal 3 restores the canvas before the previous image; no stale frame pointers.
    auto source = AnimationSource::create(disposalBytes, 320); assert(source && source->loops == 3);
    AnimationDecoder player(source);
    for (const auto& expected : disposalPixels) { auto frame = player.next(live); assert(frame && frame->rgba == expected); }
    auto texture = reserveAnimationTexture(32 * 1024 * 1024); assert(texture && !reserveAnimationTexture(4));
    texture.reset(); assert(animationMemory().textures == 0);
}
