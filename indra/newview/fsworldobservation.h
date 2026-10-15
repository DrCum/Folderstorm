/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_WORLD_OBSERVATION_H
#define FS_WORLD_OBSERVATION_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <utility>

// Viewer-independent rules shared by the native adapter and focused tests.
namespace fs_world
{
constexpr int MAX_ROWS = 100;
constexpr int DEFAULT_ROWS = 25;
constexpr int MAX_SCAN = 8192;
constexpr double MAX_RADIUS = 256.;
constexpr double DEFAULT_RADIUS = 64.;

inline bool operation(const std::string& op)
{
    return op == "location" || op == "parcel" || op == "avatars" ||
        op == "objects" || op == "audio" || op == "media";
}
inline bool bounds(double radius, int limit)
{
    return std::isfinite(radius) && radius >= 1. && radius <= MAX_RADIUS &&
        limit >= 1 && limit <= MAX_ROWS;
}
inline bool visible(const std::string& op, bool location, bool names, bool nearby)
{
    // Refuse the whole observation when safe masking is not established.
    // Owner/source IDs, positions, counts and media associations can otherwise
    // reveal an avatar hidden by the native viewer's name/nearby restrictions.
    return operation(op) && location && (op == "location" || (names && nearby));
}

struct Identity
{
    std::string account, login, grid, region;
    bool operator==(const Identity& other) const
    {
        return account == other.account && login == other.login &&
            grid == other.grid && region == other.region;
    }
};

class Epoch
{
public:
    void observe(const Identity& source, double now)
    {
        if (!(source == mSource))
        {
            mSource = source;
            ++mGeneration;
            mAudioSince = now;
        }
    }
    std::uint64_t generation() const { return mGeneration; }
    double audioSince() const { return mAudioSince; }
    bool containsSound(double started, double now) const
    {
        return mGeneration && std::isfinite(started) && std::isfinite(now) &&
            started >= mAudioSince && started <= now;
    }
private:
    Identity mSource;
    std::uint64_t mGeneration = 0;
    double mAudioSince = 0.;
};

// Bounded memory, stable UUID order, no offset/cursor over changing live state.
// Truncation reveals no hidden total and discarded rows are not retained.
template<class Row> class Rows
{
public:
    explicit Rows(std::size_t limit) : mLimit(limit) {}
    void add(const std::string& key, Row row)
    {
        if (!mLimit) { mTruncated = true; return; }
        mRows.emplace(key, std::move(row));
        if (mRows.size() > mLimit)
        {
            mRows.erase(std::prev(mRows.end()));
            mTruncated = true;
        }
    }
    const std::map<std::string, Row>& values() const { return mRows; }
    bool truncated() const { return mTruncated; }
private:
    std::size_t mLimit;
    bool mTruncated = false;
    std::map<std::string, Row> mRows;
};
}
#endif
