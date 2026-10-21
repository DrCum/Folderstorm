/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
// The Windows SDK declares annotation GUIDs that MSVC's uuid.lib does not
// provide. Instantiate the SDK's own definitions in exactly one translation unit.
#include <windows.h>
#include <initguid.h>
#include <oleacc.h>
