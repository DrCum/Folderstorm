/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_NAVIGATION_LISTENER_H
#define FS_NAVIGATION_LISTENER_H
#include "lleventapi.h"
#include "fsassistantfeature.h"
class FSNavigationListener : public LLEventAPI,public FSAssistantFeature
{
public:
    FSNavigationListener();
    const char* assistantCapability() const override { return "navigation_tools"; }
    LLSD assistantLimits() const override;
    bool prepareAssistant(FSAssistantPreparedOperation&,std::string&) const override;
    bool validateAssistant(const FSAssistantPreparedOperation&,std::string&) const override;
private:
    void dispatch(const LLSD& request);
};
void fs_initialize_navigation_api();
void fs_navigation_tick(); // Also called before native autopilot produces movement.
#endif
