/** SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include "fscrashpolicy.h"
#include <functional>
namespace fs_crash
{
Consent consent();
bool setConsent(bool enabled);
bool configured();
void initialize(const std::string& role);
// The viewer supplies nonblocking UI; host callers can use offerConsent().
// Create, respond and release the prompt on the same UI thread.
struct ConsentPrompt {std::function<bool(std::optional<bool>)> respond;};
std::optional<ConsentPrompt> consentPrompt();
void offerConsent();
void poll();
void shutdown();
std::string status();
std::string environment();
inline std::string reportUrl(){return BugReportUrl;}
}
