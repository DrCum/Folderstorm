/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_GESTURE_LISTENER_H
#define FS_GESTURE_LISTENER_H
#include "lleventapi.h"
#include "fsassistantfeature.h"
#include <memory>
class LLUUID;
class FSGestureListener : public LLEventAPI, public FSAssistantFeature {
public:
    FSGestureListener();
    ~FSGestureListener() override;
    const char* assistantCapability()const override{return "gesture_tools";}
    LLSD assistantLimits()const override;
    bool prepareAssistant(FSAssistantPreparedOperation&,std::string&)const override;
    bool validateAssistant(const FSAssistantPreparedOperation&,std::string&)const override;
    void tick();
    void animationRequest(const LLUUID&);
    void animationReset();
private:
    void dispatch(const LLSD&);
    struct State; std::shared_ptr<State> mState;
};
void fs_initialize_gesture_api();
void fs_assistant_gestures_tick();
void fs_assistant_animation_request(const LLUUID& id);
void fs_assistant_animation_reset();
#endif
