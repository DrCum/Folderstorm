/** Token-free client configuration serialization, independent of viewer/UI state. */
#ifndef FS_ASSISTANT_CONFIGURATION_H
#define FS_ASSISTANT_CONFIGURATION_H

#include <boost/json.hpp>
#include <string>
#include <vector>

namespace FSAssistantConfiguration
{
// The Windows adapter is needed only for Cursor's cmd-based command launcher.
// Using an environment reference avoids interpreting percent signs in the path.
inline std::string generate(const std::string& client, const std::string& executable,
                            const std::string& discovery, bool cursor_cmd_adapter = false)
{
    const auto quoted = [](const std::string& text) {
        return boost::json::serialize(boost::json::value(text));
    };
    if (client == "codex")
    {
        // JSON escaping also represents these strings in TOML basic strings.
        return "[mcp_servers.folderstorm]\ncommand = " + quoted(executable) +
            "\nargs = []\ntool_timeout_sec = 120\n\n[mcp_servers.folderstorm.env]\nFIRESTORM_MCP_DISCOVERY = " +
            quoted(discovery) + "\n";
    }
    boost::json::object env{{"FIRESTORM_MCP_DISCOVERY", discovery}};
    boost::json::array args;
    std::string command = executable;
    if (client == "cursor" && cursor_cmd_adapter)
    {
        command = "cmd.exe";
        args = {"/d", "/s", "/c", "\"\"%FOLDERSTORM_MCP_BINARY%\"\""};
        env["FOLDERSTORM_MCP_BINARY"] = executable;
    }
    boost::json::object entry{{"command", command}, {"args", args}, {"env", env}};
    if (client == "claude") entry["type"] = "stdio";
    return boost::json::serialize(boost::json::object{{"mcpServers", boost::json::object{{"folderstorm", entry}}}});
}
}
#endif
