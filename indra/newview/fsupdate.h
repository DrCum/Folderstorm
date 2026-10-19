/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <string>
namespace fs_update
{
// Lifetime lease starts before host/viewer work and survives native logout/UI.
bool enter();
bool prepare();
void offerShortcuts();
bool configureShortcuts();
void removeShortcuts();
std::string lastError();
}
