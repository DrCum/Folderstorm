/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_CODEC_H
#define FS_CHAT_MEDIA_CODEC_H
#include "fschatmediapolicy.h"
#include <memory>
namespace fs_chat_media
{
struct Frame { std::vector<unsigned char> rgba; unsigned delay = 100; };
struct Image
{
    int width = 0, height = 0;
    bool gif = false;
    std::vector<Frame> frames;
    std::size_t bytes() const { return std::size_t(width) * height * 4 * frames.size(); }
};
// Caller serializes decode work. No viewer, GL, networking or filesystem access.
std::shared_ptr<Image> decode(const std::vector<unsigned char>& bytes);
}
#endif
