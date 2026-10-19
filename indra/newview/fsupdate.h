/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <string>
#include <cstdint>
#include <functional>
#include <optional>
namespace fs_update
{
// Lifetime lease starts before host/viewer work and survives native logout/UI.
bool enter();
bool prepare();
// Deferred viewer UI: create, respond and release on the same UI thread.
// No prompt means setup is already complete, unavailable or failed (lastError).
struct ShortcutPrompt
{
    std::uint32_t choices=0;
    std::function<bool(std::optional<std::uint32_t>)> respond;
};
std::optional<ShortcutPrompt> shortcutPrompt(bool manual);
void offerShortcuts();
bool configureShortcuts();
void removeShortcuts();
std::string lastError();
}
