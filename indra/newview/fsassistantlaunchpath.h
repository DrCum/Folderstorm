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

/** Installation-bound command selection; no shared launcher aliases are used. */
#ifndef FS_ASSISTANT_LAUNCH_PATH_H
#define FS_ASSISTANT_LAUNCH_PATH_H

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace FSAssistantLaunchPath
{
enum class State { Direct, StableCurrent, RecopyAfterUpdate, UpdateInProgress };
struct Result
{
    std::filesystem::path command;
    State state = State::Direct;
    bool canCopy() const { return state != State::UpdateInProgress; }
};

// The pinned Windows Velopack locator uses root/current/sq.version and root/Update.exe.
// Keep that logical current path: canonicalizing it into a version directory would
// turn a durable command into one removed on update. Never search/guess app-* roots.
inline Result resolve(const std::filesystem::path& actualSidecar, const std::string& packageID,
                      const std::string& runningVersion)
{
    Result result{actualSidecar, State::Direct};
    const auto directory = actualSidecar.parent_path();
    const auto manifest = directory / "sq.version";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec))
    {
        // NSIS installs retain the direct install path. An unrecognized layout
        // has no evidence of a stable update root and receives truthful guidance.
        if (ec || !std::filesystem::is_regular_file(directory / "uninst.exe", ec) || ec)
            result.state = State::RecopyAfterUpdate;
        return result;
    }
    result.state = State::RecopyAfterUpdate;
    const auto size = std::filesystem::file_size(manifest, ec);
    if (ec || size == 0 || size > 65536) return result;
    std::ifstream input(manifest, std::ios::binary);
    if (!input) return result;
    // Bound the parse even if another process grows the file after file_size().
    std::string raw(65537, '\0');
    input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
    raw.resize(static_cast<size_t>(input.gcount()));
    if (raw.size() > 65536 || input.bad()) return result;
    try
    {
        boost::property_tree::ptree tree;
        std::istringstream xml(raw);
        boost::property_tree::read_xml(xml, tree, boost::property_tree::xml_parser::trim_whitespace);
        if (tree.get<std::string>("package.metadata.id", "") != packageID) return result;
        const auto version = tree.get<std::string>("package.metadata.version", "");
        if (version.empty()) return result;
        // Only this exact SDK-supported layout is recognized. Case variants or
        // physically resolved version directories safely retain the actual path.
        if (directory.filename() != "current" ||
            !std::filesystem::is_regular_file(directory.parent_path() / "Update.exe", ec) || ec)
            return result;
        result.state = version == runningVersion ? State::StableCurrent : State::UpdateInProgress;
    }
    catch (const boost::property_tree::xml_parser_error&) { }
    catch (const boost::property_tree::ptree_error&) { }
    return result;
}
}
#endif
