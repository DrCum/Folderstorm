/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <cstddef>
#include <limits>
namespace LLCore
{
inline bool bodyChunkFits(std::size_t used, std::size_t size,
                          std::size_t count, std::size_t limit)
{
    if (size && count > std::numeric_limits<std::size_t>::max() / size)
        return false;
    const auto bytes = size * count;
    return !limit || (used <= limit && bytes <= limit - used);
}
}
