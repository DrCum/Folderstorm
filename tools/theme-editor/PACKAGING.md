# Optional Windows editor

Ordinary native builds leave `FOLDERSTORM_BUILD_THEME_EDITOR=OFF`. That path does
not discover or run dotnet. Missing editor files leave the host/viewer usable;
their Theme lab launch points show a message.

Enable the component for a Windows x64 build:

```text
-DFOLDERSTORM_BUILD_THEME_EDITOR=ON
-DFOLDERSTORM_THEME_EDITOR_DOTNET=C:/path/to/dotnet.exe
-DFOLDERSTORM_THEME_EDITOR_VERSION=0.20.0
```

The optional component requires SDK 10 or newer on the build machine. End users
receive a self-contained Windows Desktop runtime and do not need an SDK or .NET
installation. The source commit and editor version are explicit build inputs.

CMake publishes to a source/version/configuration directory, then seals a file
manifest. Packaging checks the x64 apphost, bundled runtime, exact source and all
file hashes. NSIS receives explicit file records under `theme-editor/`; Velopack
validates the same component before packing its staged directory. Private PDBs
stay in the publish output for retention and are omitted from installed files.

Changing the option to OFF removes only a recognized, manifest-owned staged
editor folder. Unrecognized or linked folders cause an error. Packaged revisions
and user theme libraries are outside this staging operation.

The native bundle records whether the editor is enabled. An enabled package
cannot silently omit it; a disabled package cannot retain stale editor files.
Editor updates travel with the application. User projects and immutable theme
library revisions remain in their existing per-user locations.

Fixture checks cover SDK-off/missing-SDK behavior, actual self-contained publish,
source/hash rejection, stale staging, production NSIS file listing and Velopack
validation. A matched native build and installer test are separate evidence.
