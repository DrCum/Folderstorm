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

// Standalone serializer regression probe. Emits entries for independent JSON/TOML parsing.
// Build with C++17+, the packaged Boost include directory and libboost_json.a.
// Parse the first two output lines as JSON; parse the remaining lines with an
// independent TOML parser (e.g. Python tomllib). Checks remain active with NDEBUG.
#include "../fsassistantconfiguration.h"
#include <stdexcept>
#include <iostream>

namespace
{
void check(bool value)
{
    if (!value) throw std::runtime_error("Assistant configuration round-trip failed");
}
}

int main()
{
    const std::string path = "C:\\Program Files\\雪 %example%\\Folderstorm\\fs-mcp.exe";
    const std::string discovery = "C:\\Users\\Márty\\custom \"settings\"\\user_settings";
    for (const std::string client : {"cursor", "claude"})
    {
        const bool adapter = client == "cursor";
        const auto config = FSAssistantConfiguration::generate(client, path, discovery, adapter);
        const auto data = boost::json::parse(config);
        const auto& entry = data.at("mcpServers").at("folderstorm");
        check(entry.at("env").at("FIRESTORM_MCP_DISCOVERY").as_string() == discovery);
        if (adapter)
        {
            check(entry.at("command").as_string() == "cmd.exe");
            check(entry.at("env").at("FOLDERSTORM_MCP_BINARY").as_string() == path);
            check(entry.at("args").as_array().back().as_string() == "\"\"%FOLDERSTORM_MCP_BINARY%\"\"");
        }
        else check(entry.at("command").as_string() == path);
        std::cout << config << '\n';
    }
    std::cout << FSAssistantConfiguration::generate("codex", path, discovery);
}
