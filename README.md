<p align="center">
  <img src="indra/newview/icons/release/firestorm_256.png" alt="Folderstorm" width="128">
</p>

# Folderstorm

Folderstorm is a Second Life viewer based on [Firestorm](https://github.com/FirestormViewer/phoenix-firestorm). It is not an official Firestorm build.

Folderstorm was inspired by a need for better inventory management. Sorting outfits, landmarks, and objects is easier with a larger workspace. Two features do that work: multi-monitor support w/ adjustable 3d viewport, and a local assistant on your own computer.

## World viewport

Stretch the window across more than one monitor and keep the 3D world on one of them. The camera and HUD stay in that slice. The rest of the window is free for other panels: more inventory windows, a map while you drive, chat on its own screen, and similar.

The controls are in Preferences, under Move & View. Toolbars and top bars can sit around that view, and that arrangement can be saved.

Login and the two-factor prompt use that same slice when it is a saved viewport.

## Local assistant

`fs-mcp` is a local MCP sidecar. The viewer runs an in-viewer bridge, and a permission grid controls what the assistant may do. Permanent delete stays off. It can search and tidy inventory, change your outfit, and take a picture. It stays off until you turn it on.

It can capture a snapshot of the world viewport and set that image on an outfit or inventory thumbnail. The default is a free thumbnail. Uploading a texture asks when it costs L$.

## Also included

- An opt-in migrator can copy Firestorm settings into Folderstorm.
- Configure finds Go 1.25 or newer before the rest of the build. The installer ships `fs-mcp` and the settings migrator next to the viewer. On Windows it also adds a space-free link at `C:\ProgramData\Folderstorm\fs-mcp.exe`.

## Get a build

Get a build from [folderstorm.sodie.net](https://folderstorm.sodie.net). To build from source, see [building from source](doc/building-from-source.md).

## MCP setup

How to turn the bridge on and connect a client is in [doc/help.md](doc/help.md) and [tools/fs-mcp/README.md](tools/fs-mcp/README.md). On Windows, the command is `C:\ProgramData\Folderstorm\fs-mcp.exe`.

## License

Folderstorm remains under the same license as Firestorm: the [GNU Lesser General Public License, version 2.1](LICENSE).
