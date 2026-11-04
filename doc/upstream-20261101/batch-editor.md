# Editor and inventory usability

Base: main `e462dd1d1f`. No other upstream batch required.

Plan/sources: Firestorm `911881389c5d26a29e623fe9bf1211e70e5883b0`
enables clipboard references for native-linkable no-copy items;
`b564148e858c11aebac8e511a24aef0d113c861a` retains live script token
color references; SL `3ab8a4fdb4e54e4aa5e6be111ffb0c85500b7545`
preserves the incoming online/offline flag in the login buddy queue;
Firestorm `d942e1118d01c3383ef7d6b64f248af77452b0dc` keeps the browser
visible to initialize while putting failure text above it until ready.

The clipboard stores references; native item COPY checks and native paste/link
permissions remain intact. No MCP permission or inventory upload path changes.
Friend status stays in its existing per-viewer native tracker. Browser fixes
affect explicitly opened native browser panels, not chat-image decoding or a
new background browser. Prior #32/#66 imports are distinct.

Three focused checks pass: production clipboard eligibility with copyable,
no-copy linkable/nonlinkable, mixed and disabled selections; a compiled native
color-reference declaration/getter with live table changes; all three changed
browser XML layouts parse and keep failure text above the visible browser.
All four changed .cpp files pass real-header Linux syntax. Trial merges with
#160/#161/#162 and the earlier UI batch are clean, with no overlapping files.

Native acceptance: Ctrl+C then Paste Link for no-copy items, with normal Paste
still refusing unauthorized copies; live script keyword colors; offline/online
friend notices received before buddy initialization; browser/plugin failure,
first navigation and resize for Web, Media and Marketplace floaters. Full
Windows/viewer/CEF runtime tests remain unrun.
