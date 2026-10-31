// Optional disposable MCP fixture. Install stopped, compile, then start it.
// Markers are ordinary owner chat, not authentication or proof of correctness.
integer COMMAND_CHANNEL = -900123;
integer DIALOG_CHANNEL = -900124;

default
{
    state_entry()
    {
        llListen(COMMAND_CHANNEL, "", llGetOwner(), "");
        llListen(DIALOG_CHANNEL, "", llGetOwner(), "");
        llOwnerSay("fixture: ready");
    }
    touch_start(integer count)
    {
        if (llDetectedKey(0) != llGetOwner()) return;
        llOwnerSay("fixture: touched");
        llDialog(llGetOwner(), "Disposable game test", ["Ping", "Text"], DIALOG_CHANNEL);
    }
    listen(integer channel, string name, key sender, string text)
    {
        if (sender != llGetOwner()) return;
        if (channel == COMMAND_CHANNEL && text == "ping")
        {
            llOwnerSay("fixture: pong");
            llMessageLinked(LINK_SET, 7001, "ping", NULL_KEY);
        }
        else if (channel == DIALOG_CHANNEL && text == "Ping")
            llOwnerSay("fixture: dialog pong");
        else if (channel == DIALOG_CHANNEL && text == "Text")
            llTextBox(llGetOwner(), "Enter a test value", DIALOG_CHANNEL);
        else if (channel == DIALOG_CHANNEL)
            llOwnerSay("fixture: text=" + text);
    }
}
