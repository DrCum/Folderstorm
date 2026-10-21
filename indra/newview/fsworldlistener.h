/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_WORLD_LISTENER_H
#define FS_WORLD_LISTENER_H

#include "lleventapi.h"
#include "fsassistantfeature.h"
#include "fsworldobservation.h"

class FSWorldListener : public LLEventAPI, public FSAssistantFeature
{
public:
    FSWorldListener();
    const char* assistantCapability() const override { return "world_observation"; }
    LLSD assistantLimits() const override;
    bool prepareAssistant(FSAssistantPreparedOperation& operation, std::string& error) const override;
    bool validateAssistant(const FSAssistantPreparedOperation& operation, std::string& error) const override;
private:
    void observe(const LLSD& request);
    fs_world::Epoch mEpoch;
};

void fs_initialize_world_api();
#endif
