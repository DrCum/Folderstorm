# Folderstorm features

Short bullets for the website. The first group is what this build adds on top of the previous Folderstorm main branch. The second group is already in that main branch and ships with this build.

## New in this build

- Folderstorm is its own viewer name, with its own icons, login mark, and crash reports, split off from Firestorm.
- The login screen uses the winged folder mark, purple and blue splash art from folderstorm.sodie.net, and a purple Log In button.
- The login page shows the I'm Sodie feed and retitles the page as Folderstorm.
- A local assistant bridge lets Cursor, Codex, and Claude Code work with the viewer you already have open. It stays on this computer and is off unless you start the viewer with `--mcp-api`.
- The assistant can look through inventory, search it, and open folders, items, notecards, scripts, and landmarks.
- The assistant can make folders and new items, rename, move, copy, link, favorite, and set descriptions and thumbnails.
- Copying respects no-copy items. Unique items stay put until you confirm a move. Emptying Trash and permanent delete always ask first.
- The assistant can list My Outfits and what you are wearing, then wear an outfit, add pieces, or detach them after you confirm.
- The assistant can pose the camera for a portrait, full-body, front, back, left, or right shot, or place it at an exact spot in the region.
- The assistant can take a JPEG of the current view with the interface and HUD hidden, then hand that picture back to the chat.

## Already in Folderstorm

- A custom world viewport keeps the camera, HUD, and mouse in the visible world while Inventory and other windows use the rest of a wide screen.
- Toolbars, chat, and the top bars can sit around that viewport, with saved layout profiles you can apply, rename, and delete.
- Those layout controls are in the preferences for the included skins.
- Outfit Gallery thumbnails resize from a slider, from small tiles up to the full photo the gallery loads.
- Linux and Windows open builds decode JPEG2000 textures with AVX2, including a static Windows build that does not rely on a separate OpenJPEG DLL.
