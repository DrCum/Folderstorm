/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_LIFECYCLE_H
#define FS_SESSION_LIFECYCLE_H
#include "fssessionregistry.h"
#include "fssessionrestart.h"
namespace fs_session
{
// Rendering recovery UI does not grant managed simulator input or voice.
inline bool sessionRenderAllowed(bool disconnected, bool readyApplied, Mode mode)
{ return disconnected || !readyApplied || mode == Mode::Active; }

// Used by the real host. Management selection is independent of the active
// world; an unavailable character must remain selectable for lifecycle actions.
class HostLifecycle
{
public:
    int selection() const { return selected; }
    int observeSelection(int choice)
    { if (validSlot(choice)) selected = choice; return selected; }
    void completedHandoff(int active) { observeSelection(active); }
    void beginManagement() { managing = true; } // Explicit launch/restart accepted.
    template<class Slots> void stopManaging(Slots& slots)
    {
        managing = false;
        for (auto& slot : slots) if (slot) slot->restart.cancel();
    }
    Restart::Result pollRestart(Restart& restart, bool alive, bool cancelled, std::uint64_t now, bool allowLaunch)
    {
        if (!managing) { restart.cancel(); return Restart::Result::None; }
        return restart.poll(alive, cancelled, now, allowLaunch);
    }
private:
    int selected = 0;
    bool managing = true;
};

// Only the panel containing the message target may process its navigation.
// This avoids routing a detached panel's Tab through the root host first.
template<class Window, class Panels, class Owns, class Process>
bool routePanelDialog(Window target, const Panels& panels, Owns owns, Process process)
{
    for (auto panel : panels) if (panel && owns(panel, target)) return process(panel);
    return false;
}
}
#endif
