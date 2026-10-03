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

// Standalone test: C++17+, packaged Boost headers; no viewer or Boost libraries.
#include "../fsassistantlaunchpath.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("Fixture write failed");
}
void manifest(const std::filesystem::path& directory, const std::string& id, const std::string& version)
{
    write(directory / "sq.version", "<?xml version=\"1.0\"?><package xmlns=\"test\"><metadata><id>" +
        id + "</id><version>" + version + "</version></metadata></package>");
}
}

int main()
{
    using namespace FSAssistantLaunchPath;
    const auto root = std::filesystem::temp_directory_path() /
        ("folderstorm-launch-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    const auto nsis = root / "Program Files" / "Folderstorm A";
    write(nsis / "fs-mcp.exe", "sidecar-a");
    write(nsis / "uninst.exe", "uninstall-a");
    auto result = resolve(nsis / "fs-mcp.exe", "FolderstormA", "1.2.3-4");
    check(result.state == State::Direct && result.command == nsis / "fs-mcp.exe", "NSIS direct install preserved");

    const auto framework = root / "Marty %work%" / "Folderstorm B";
    const auto current = framework / "current";
    write(current / "fs-mcp.exe", "sidecar-b");
    write(framework / "Update.exe", "framework updater");
    manifest(current, "FolderstormB", "1.2.3-4");
    result = resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4");
    check(result.state == State::StableCurrent && result.command == current / "fs-mcp.exe", "logical current command preserved");
    manifest(current, "FolderstormB", "1.2.3-5");
    check(!resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4").canCopy(), "switched package rejected until restart");
    manifest(current, "OtherChannel", "1.2.3-4");
    result = resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4");
    check(result.state == State::RecopyAfterUpdate && result.command == current / "fs-mcp.exe", "other channel never substituted");
    manifest(current, "FolderstormB", "1.2.3-4");
    std::filesystem::remove(framework / "Update.exe");
    check(resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4").state == State::RecopyAfterUpdate, "marker alone does not prove layout");

    const auto old_version = framework / "app-1.2.3";
    write(old_version / "fs-mcp.exe", "old-sidecar");
    manifest(old_version, "FolderstormB", "1.2.3-4");
    write(framework / "Update.exe", "framework updater");
    result = resolve(old_version / "fs-mcp.exe", "FolderstormB", "1.2.3-4");
    check(result.state == State::RecopyAfterUpdate && result.command == old_version / "fs-mcp.exe", "no guessed version-folder parent");
    write(current / "sq.version", "<broken><metadata>");
    check(resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4").state == State::RecopyAfterUpdate, "malformed manifest is fallback");
    write(current / "sq.version", std::string(65537, 'x'));
    check(resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4").state == State::RecopyAfterUpdate, "oversized manifest is bounded");

    // Framework rename/rollback changes manifest version, never the logical command.
    manifest(current, "FolderstormB", "1.2.3-4");
    check(resolve(current / "fs-mcp.exe", "FolderstormB", "1.2.3-4").command == current / "fs-mcp.exe", "rollback keeps same durable command");
    std::cout << "Assistant installation launch path tests passed\n";
}
