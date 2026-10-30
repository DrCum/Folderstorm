/**
 * @file fsanimationkeys.h
 * @brief Build-selected animation key storage and scoped decoding.
 *
 * Copyright (c) 2026, Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef FS_ANIMATION_KEYS_H
#define FS_ANIMATION_KEYS_H

#include <map>

#ifndef FS_CONTIGUOUS_ANIMATION_KEYS
#define FS_CONTIGUOUS_ANIMATION_KEYS 0
#endif

#if FS_CONTIGUOUS_ANIMATION_KEYS
#include <boost/container/flat_map.hpp>
#include <optional>
#include <utility>
#endif

namespace FSAnimationKeys
{
template<class Time, class Key>
using KeyMap =
#if FS_CONTIGUOUS_ANIMATION_KEYS
    boost::container::flat_map<Time, Key>;
#else
    std::map<Time, Key>;
#endif

// Decode a fresh, empty curve. Call commit once before publishing; do not reuse the builder.
template<class Map> class Builder
{
public:
    explicit Builder(Map& target) : mTarget(target) {}
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    void set(const typename Map::key_type& time, const typename Map::mapped_type& key)
    {
#if FS_CONTIGUOUS_ANIMATION_KEYS
        (*mStaging)[time] = key;
#else
        mTarget[time] = key;
#endif
    }

    void commit()
    {
#if FS_CONTIGUOUS_ANIMATION_KEYS
        Map packed(boost::container::ordered_unique_range, mStaging->begin(), mStaging->end());
        mTarget = std::move(packed);
        mStaging.reset(); // Release scratch before decoding the next curve.
#endif
    }

private:
    Map& mTarget;
#if FS_CONTIGUOUS_ANIMATION_KEYS
    using StagingMap = std::map<typename Map::key_type, typename Map::mapped_type>;
    std::optional<StagingMap> mStaging{std::in_place};
#endif
};
}

#endif
