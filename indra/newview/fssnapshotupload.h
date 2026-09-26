/**
 * @file fssnapshotupload.h
 * @brief Cost gate for outfit snapshot uploads. No viewer dependencies.
 *
 * Thumbnail uploads quote L$0. A texture upload uses the quoted texture price.
 * Never denies. Ask shows the price. Allow proceeds only when that price is 0.
 * Allow with a price above 0 is treated as Ask.
 */

#ifndef LL_FSSNAPSHOTUPLOAD_H
#define LL_FSSNAPSHOTUPLOAD_H

#include <string>

namespace fs_snapshot
{

constexpr int kMinEdge = 64;
constexpr int kMaxEdge = 2048;
constexpr int kDefaultEdge = 1024;

enum class UploadAction
{
    Proceed,
    Ask,
    Deny
};

inline int clamp_edge(int value)
{
    if (value < kMinEdge)
    {
        return kMinEdge;
    }
    if (value > kMaxEdge)
    {
        return kMaxEdge;
    }
    return value;
}

// Empty means thumbnail. Unknown text is rejected.
inline bool normalize_destination(const std::string& destination, std::string& out)
{
    if (destination.empty() || destination == "thumbnail")
    {
        out = "thumbnail";
        return true;
    }
    if (destination == "texture")
    {
        out = "texture";
        return true;
    }
    out.clear();
    return false;
}

// Thumbnail is always L$0, including when a texture quote was passed in.
inline int quoted_cost(const std::string& destination, int texture_cost)
{
    if (destination != "texture")
    {
        return 0;
    }
    return texture_cost < 0 ? 0 : texture_cost;
}

// level is the bridge value: allow, ask, or deny. deny is the Never setting.
inline UploadAction decide(const std::string& level, int cost)
{
    if (level == "deny")
    {
        return UploadAction::Deny;
    }
    if (level == "ask" || cost > 0)
    {
        return UploadAction::Ask;
    }
    return UploadAction::Proceed;
}

}

#endif
