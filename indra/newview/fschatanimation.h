/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_ANIMATION_H
#define FS_CHAT_ANIMATION_H
#include "fschatmediacodec.h"
#include <atomic>
namespace fs_chat_media
{
// Immutable, bounded encoded data. No network, viewer, GL or file dependencies.
class AnimationSource
{
public:
    enum class Format { Gif, WebP };
    static std::shared_ptr<AnimationSource> create(const std::vector<unsigned char>& bytes, int maxDimension, bool posterOnly = false);
    int width = 0, height = 0, sourceWidth = 0, sourceHeight = 0;
    unsigned frames = 1, loops = 1; // zero loops means infinite; GIF repeat counts include the first pass here.
    Format format = Format::Gif;
    const std::vector<unsigned char>& encoded() const { return mEncoded; }
private:
    std::vector<unsigned char> mEncoded;
    std::shared_ptr<void> mReservation;
};
class AnimationDecoder
{
public:
    explicit AnimationDecoder(std::shared_ptr<const AnimationSource> source);
    ~AnimationDecoder();
    AnimationDecoder(const AnimationDecoder&) = delete;
    AnimationDecoder& operator=(const AnimationDecoder&) = delete;
    // One frame, or null on end/error/cancellation. Call only on a worker.
    std::shared_ptr<Frame> next(const std::atomic<bool>& cancelled);
    bool ended() const;
    bool failed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
struct AnimationMemory { std::size_t encoded, decoded, scratch, textures; };
AnimationMemory animationMemory();
// A frame owns its reservation through all subscribers.
std::shared_ptr<void> reserveAnimationPixels(std::size_t bytes);
std::shared_ptr<void> reserveAnimationTexture(std::size_t bytes);
}
#endif
