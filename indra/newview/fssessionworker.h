/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#ifndef FS_SESSION_WORKER_H
#define FS_SESSION_WORKER_H
#include <string>
class FSSessionWorker
{
public:
    enum class LoginGate { Allow, Wait, Deny };
    static bool initialize(); // Valid inherited capability or ordinary viewer.
    static bool managed();
    static void configure(); // Temporary settings only, after command line.
    static bool temporaryControl(const std::string& name);
    static void tick();       // Main-thread protocol/lifecycle; never a render timer.
    static bool renderAllowed();
    static bool hostForeground(); // Scheduling only; never fabricates keyboard focus.
    static void prepareDisplay(); // Current GL context only; reversible economy buffers.
    static bool monitorRendering();
    static void renderMonitor(); // Current GL context, after ordinary display maintenance.
    static void beginCameraFrame(); // Temporary renderer pose only, never LLAgentCamera.
    static void drawTransition(); // World-only fade, before native 2D controls.
    static void endCameraFrame();   // Paired even when display returns early.
    static bool inputAllowed();
    static bool voiceAllowed();
    static bool backgroundAudioMuted();
    static void nearbyMessage(const class LLChat& message);
    static bool keyAllowed(unsigned int key);
    static void keyReleased(unsigned int key);
    static bool textAllowed();
    static bool requestCharacterSwitch(unsigned int command); // Existing keybindings; source-bound intent only.
    static void focusHostedClient(); // Native window thread; foreground host only.
    static int backgroundYield(int normal);
    static void framePresented();
    static LoginGate loginGate(const std::string& grid, const std::string& name);
    static void quitCancelled(); // Cancellation hint only; never authorizes an action.
    static void shutdown();
};
#endif
