/**
 * @file llcameralistener.h
 * @brief Local Event API for camera pose and snapshots.
 */

#ifndef LL_LLCAMERALISTENER_H
#define LL_LLCAMERALISTENER_H

#include "lleventapi.h"

class LLCameraListener : public LLEventAPI
{
public:
    LLCameraListener();

private:
    void get(LLSD const& data);
    void setPose(LLSD const& data);
    void set(LLSD const& data);
    void reset(LLSD const& data);
    void snapshot(LLSD const& data);
};

#endif
