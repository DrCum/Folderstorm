/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include "fscrashpolicy.h"
namespace fs_crash
{
Consent consent();
bool setConsent(bool enabled);
bool configured();
void initialize(const std::string& role);
void offerConsent();
void poll();
void shutdown();
std::string status();
std::string environment();
inline std::string reportUrl(){return BugReportUrl;}
}
