<p align="center">
  <img src="indra/newview/icons/release/firestorm_256.png" alt="Folderstorm" width="128">
</p>

# Folderstorm

Folderstorm is a Second Life viewer based on [Firestorm](https://github.com/FirestormViewer/phoenix-firestorm). It is not an official Firestorm build.

## What is different

- **Local assistant.** `fs-mcp` is a local MCP sidecar. The viewer runs an in-viewer bridge, and a permission grid controls what the assistant may do. Permanent delete stays off.
- **Settings from Firestorm.** An opt-in migrator can copy Firestorm settings into Folderstorm.
- **Login on the world view.** When a saved world viewport is a real slice of a stretched window, the login screen and the two-factor prompt sit in that slice.
- **Outfit snapshots.** The assistant can capture a square of the world viewport and set it on an outfit. The default is a free thumbnail. Uploading a texture asks when it costs L$.
- **Build and install.** Configure finds Go 1.25 or newer before the rest of the build. The installer ships `fs-mcp` and the settings migrator next to the viewer. On Windows it also adds a space-free link at `C:\ProgramData\Folderstorm\fs-mcp.exe`.

## Get a build

A release build is not published yet. To build from source, see [building from source](doc/building-from-source.md).

## MCP setup

How to turn the bridge on and connect a client is in [doc/help.md](doc/help.md) and [tools/fs-mcp/README.md](tools/fs-mcp/README.md). On Windows, the command is `C:\ProgramData\Folderstorm\fs-mcp.exe`.

## License

Folderstorm remains under the same license as Firestorm: the [GNU Lesser General Public License, version 2.1](LICENSE).
