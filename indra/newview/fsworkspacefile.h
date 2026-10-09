/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */
#ifndef FS_WORKSPACE_FILE_H
#define FS_WORKSPACE_FILE_H
#include <string>
namespace FSWorkspaceFile
{
constexpr size_t MAX_TRANSFER_BYTES = 1024 * 1024;
inline bool boundedXML(const std::string& text)
{
    if (text.empty() || text.size() > MAX_TRANSFER_BYTES) return false;
    int depth = 0, nodes = 0;
    for (size_t i = 0; (i = text.find('<', i)) != std::string::npos;)
    {
        const auto end = text.find('>', i + 1);
        if (end == std::string::npos || i + 1 == end || ++nodes > 20000 || text[i + 1] == '!') return false;
        if (text[i + 1] != '?')
        {
            if (text[i + 1] == '/') --depth;
            else if (text[end - 1] != '/') ++depth;
            if (depth < 0 || depth > 24) return false;
        }
        i = end + 1;
    }
    return depth == 0;
}
}
#endif
