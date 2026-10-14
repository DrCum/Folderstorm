#include "fssessionchoicestate.h"
#include "fssessionaudio.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    ChoiceTransaction selection;
    selection.begin({"nearby","sodie","group"},"nearby");
    assert(selection.open); // CLOSEUP does not end ownership or refresh IDs.
    assert(selection.accept(1) == std::optional<std::string>{"sodie"});
    selection.begin({"nearby","sodie"},"sodie");
    assert(selection.cancel() == std::optional<std::string>{"sodie"});
    selection.begin({"old"},"old"); selection.reset();
    assert(!selection.accept(0)); // Source/privacy invalidation rejects old acceptance.
    assert(backgroundHostAlertAllowed(true,true,false,false,false,false,true));
    assert(backgroundHostAlertDecision(true,true,false,false,false,false,true)==WorkerAlertDecision::Allowed);
    assert(backgroundHostAlertDecision(true,false,false,false,false,false,true)==WorkerAlertDecision::Active);
    assert(backgroundHostAlertDecision(true,true,false,false,true,false,true)==WorkerAlertDecision::Dnd);
    assert(backgroundHostAlertDecision(true,true,false,false,false,true,true)==WorkerAlertDecision::Muted);
    assert(backgroundHostAlertDecision(true,true,false,false,false,false,false)==WorkerAlertDecision::NotIncoming);
    assert(!backgroundHostAlertAllowed(true,false,false,false,false,false,true));
    assert(!backgroundHostAlertAllowed(true,true,false,false,true,false,true));
    assert(!backgroundHostAlertAllowed(true,true,false,false,false,true,true));
    assert(!backgroundHostAlertAllowed(true,true,false,true,false,false,true));
    assert(!backgroundHostAlertAllowed(true,true,true,false,false,false,true));
    assert(!backgroundHostAlertAllowed(true,true,false,false,false,false,false));
    std::cout << "Dropdown acceptance/cancellation and independent background alert policy passed.\n";
}
