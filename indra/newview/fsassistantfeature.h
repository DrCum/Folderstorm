/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_FEATURE_H
#define FS_ASSISTANT_FEATURE_H
#include "fsassistantoperation.h"

// Implemented by exact, typed feature listeners, never by generic viewer APIs.
// The bridge owns authentication, source identity and compound permission gates.
class FSAssistantFeature
{
public:
    virtual ~FSAssistantFeature() = default;
    virtual const char* assistantCapability() const = 0;
    virtual LLSD assistantLimits() const = 0;
    // Freeze targets/material inputs before native Ask; additional required
    // permission classes may be appended. Never perform a mutation here.
    virtual bool prepareAssistant(FSAssistantPreparedOperation& operation, std::string& error) const = 0;
    // Recheck native target/revision/restriction state before dispatch and at
    // later async boundaries. Read services check visibility again at delivery.
    virtual bool validateAssistant(const FSAssistantPreparedOperation& operation, std::string& error) const = 0;
};
#endif
