/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_PREVIEW_H
#define FS_SESSION_PREVIEW_H
#include "fssessionprotocol.h"
namespace fs_session
{
constexpr std::uint32_t PreviewMaxWidth = 640, PreviewMaxHeight = 360;
constexpr std::size_t PreviewBytes = static_cast<std::size_t>(PreviewMaxWidth) * PreviewMaxHeight * 4;
struct PreviewSize { std::uint32_t width = 0, height = 0; };
inline bool previewSize(PreviewSize size)
{
    return size.width >= 4 && size.height >= 4 && size.width <= PreviewMaxWidth &&
        size.height <= PreviewMaxHeight && size.width % 4 == 0;
}
inline bool previewPolicy(std::uint32_t width, std::uint32_t height, std::uint32_t rate)
{
    if (!width && !height && !rate) return true;
    return ((width == 320 && height == 180) || (width == 480 && height == 270) || (width == 640 && height == 360)) &&
        (rate == 1 || rate == 2 || rate == 4 || rate == 10); // half-frames/sec: .5, 1, 2, 5
}
inline PreviewSize fittedPreview(std::uint32_t width, std::uint32_t height, PreviewSize cap)
{
    if (!width || !height || !previewSize(cap)) return {};
    std::uint64_t w = cap.width, h = w * height / width;
    if (h > cap.height) { h = cap.height; w = h * width / height; }
    w -= w % 4;
    PreviewSize result{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
    return previewSize(result) ? result : PreviewSize{};
}
inline bool previewIdentity(const Message& frame, const Message& session)
{
    return frame.kind == Kind::Status && frame.worker == session.worker && frame.pid == session.pid &&
        frame.generation == session.generation && !frame.account.empty() && frame.account == session.account && frame.grid == session.grid &&
        frame.state == State::Ready && session.state == State::Ready && frame.mode != Mode::Active && session.mode != Mode::Active &&
        (frame.flags & PreviewFrame) && !(frame.flags & (Error | PreviewUnavailable)) && frame.event &&
        previewSize({frame.width, frame.height});
}
}
#endif
