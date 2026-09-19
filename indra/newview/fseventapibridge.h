/**
 * @file fseventapibridge.h
 * @brief Authenticated loopback bridge for viewer Event APIs.
 */

#ifndef FS_EVENT_API_BRIDGE_H
#define FS_EVENT_API_BRIDGE_H

#include "llsingleton.h"

#include <memory>
#include <string>

class LLPumpIO;

class FSEventAPIBridge final : public LLSingleton<FSEventAPIBridge>
{
    LLSINGLETON(FSEventAPIBridge);
    ~FSEventAPIBridge();

public:
    bool start(LLPumpIO& pump);
    void stop();
    bool isRunning() const;
    const std::string& getDiscoveryPath() const;

private:
    class State;
    class Node;
    std::shared_ptr<State> mState;
    std::string mDiscoveryPath;
};

#endif
