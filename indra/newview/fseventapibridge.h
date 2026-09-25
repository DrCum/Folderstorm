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
    void setPump(LLPumpIO& pump);
    bool start(LLPumpIO& pump);
    // Enables or disables the already-bound listener. The first enable binds
    // 127.0.0.1 once. Later enables rotate the token and rewrite discovery.
    // Disable rejects every request and deletes the discovery file. It does
    // not unbind the socket.
    bool applyEnabled(bool enabled);
    void stop();
    bool isRunning() const;
    int getPort() const;
    const std::string& getDiscoveryPath() const;

    // Bumps the integer published on inventory status. A hand-edited
    // permanent-delete value does not change what the bridge will run.
    static void notePermissionClassesChanged();
    static int getPolicyGeneration();

private:
    class State;
    class Node;
    bool bindOnce();
    bool writeDiscovery();
    void removeDiscoveryFile();

    LLPumpIO* mPump = nullptr;
    std::shared_ptr<State> mState;
    std::string mDiscoveryPath;
    int mPort = 0;
};

#endif
