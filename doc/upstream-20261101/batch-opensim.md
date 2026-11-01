# OpenSim URL, grid and map correctness

Base: main `e462dd1d1f`. Independent main-based batch; no earlier batch required.

Plan: preserve UTF-8 region bytes and Hypergrid constructor arguments, resolve
saved-grid aliases to canonical entries, split map rectangles at the simulator's
64-region limit, and refuse reconnect before reading an empty endpoint list.
Retain authentication data for an explicit later retry. No credentials, grid
definitions, MCP navigation permissions or host login ownership are changed.

Firestorm sources: `f34effb39127d32d37053c7651955a0eaa7de6b2`,
`9879049d15ebf13de76e1338bf139c36528a91a1`,
`00da4e64b3c8e75ab55cf1dd0bd2e14a04dc6bc5`,
`b4c12a79fc1feab8d3aa71383bbbec56867e99eb`,
`528481bec46b46f16c7670559ed095b1452c5141`, and
`a9c503e0f4b937a7809a15b4068463f2616dbec5`.
Original authors and source trailers are retained. These do not duplicate #32
or #66. The reconnect guard is adapted to the older progress-update code.

Five focused checks pass: four compiled production method/snippet adapters plus
Hypergrid constructor forwarding checks. Coverage includes UTF-8/control bytes,
alias/hostname precedence and case-sensitive lookup, cache-block coverage without
overlap, repeat request caching, and missing endpoint then successful retry.
Native transport and URL parsing beyond the extracted filtering/constructor
boundaries are not simulated. Real-header syntax checks cover the changed .cpp
files with the login component's normal include path.

Trial merges with #160/#161/#162 and the safety/UI/avatar batches are clean,
without overlapping changed files. Native acceptance still requires SL/OpenSim
login and reconnect, nickname/label selection, Unicode SLURLs, Hypergrid links,
variable-size regions and map panning/zooming. No full viewer/Windows build ran.
