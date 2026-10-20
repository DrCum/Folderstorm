/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_BUILD_LISTENER_H
#define FS_BUILD_LISTENER_H
#include "lleventapi.h"
#include "fsassistantfeature.h"
class FSBuildListener : public LLEventAPI, public FSAssistantFeature
{
public:
    FSBuildListener();
    const char* assistantCapability() const override { return "building_tools"; }
    LLSD assistantLimits() const override;
    bool prepareAssistant(FSAssistantPreparedOperation&,std::string&) const override;
    bool validateAssistant(const FSAssistantPreparedOperation&,std::string&) const override;
private:
    void dispatch(const LLSD& request);
};
void fs_initialize_build_api();
#endif
