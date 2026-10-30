// Optional script in a linked child prim. Inspect its exact Contents separately.
default
{
    state_entry() { llOwnerSay("child fixture: ready"); }
    link_message(integer sender, integer number, string text, key data)
    {
        if (number == 7001 && text == "ping") llOwnerSay("child fixture: pong");
    }
}
