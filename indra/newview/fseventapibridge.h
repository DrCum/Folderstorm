/**
 * @file fseventapibridge.h
 * @brief Authenticated loopback bridge for viewer Event APIs.
 */

#ifndef FS_EVENT_API_BRIDGE_H
#define FS_EVENT_API_BRIDGE_H

#include "llsingleton.h"
#include "llsd.h"

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

    struct StatusSnapshot
    {
        bool enabled = false;
        bool ready = false;
        int port = 0;
        int policyGeneration = 0;
        int pendingApprovals = 0;
        F64 lastExternalRequestAt = 0.0;
        F64 lastDiagnosticAt = 0.0;
        std::string lastExternalApi;
        std::string lastExternalOp;
        std::string error;
    };
    // Local, credential-free diagnostics. Times use totalTime()/1e6.
    StatusSnapshot getStatusSnapshot() const;

    // Publish a new generation and reject pending approvals whose required
    // permission was revoked. Permanent-delete settings remain irrelevant.
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
    std::string mLastError;
};

#endif
