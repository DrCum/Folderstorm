# Building Folderstorm from source

These are three separate builds. Do them in this order: the viewer, then the MCP sidecar, then the settings migrator. Building one does not build the others. The viewer CMake rules also compile `tools/migrate-settings` into the viewer package so the installer can ship that program. That step is Go, not Python, and it does not build `tools/fs-mcp`.

The viewer uses the bootstrap from this repo. `scripts/bootstrap-autobuild.cmd` and `scripts/bootstrap-autobuild.sh` create `.venv`, install `requirements.txt`, put that venv's `autobuild` on `PATH`, and set `AUTOBUILD_VARIABLES_FILE` to `fs-build-variables/variables`. `scripts/configure_firestorm.sh` uses that same path when the variable is unset. On Windows, the cmd script sets `AUTOBUILD_VSVER=170` only when both Visual Studio 2022 and Visual Studio 2026 are installed.

## Agent prompt

Copy this to an agent. It should run the commands below and not invent environment variables, paths, or extra configure flags.

```
Build three separate Folderstorm artifacts, in this order. Do not treat one build as producing the other two. Do not invent environment variables.

Checkout: the Folderstorm repo root (the directory that contains autobuild.xml, scripts/bootstrap-autobuild.cmd, scripts/bootstrap-autobuild.sh, and fs-build-variables/variables).

Toolchains already required, from doc/building_windows.md and doc/building_linux.md:
- Windows: Command Prompt (cmd.exe), not PowerShell. Visual Studio 2022 with the Desktop development with C++ workload. CMake on PATH. Cygwin with the patch package, with Cygwin's bin after CMake and before %SystemRoot%\system32. Git. NSIS (the Windows installer is makensis). Python so the bootstrap can create a venv (the py launcher's 3.10–3.13 if one is installed). Go 1.25 or newer on PATH (viewer CMake refuses to configure without go; tools/fs-mcp/go.mod says go 1.25.0).
- Linux: the apt packages in doc/building_linux.md, then Go 1.25 or newer on PATH.

1. Viewer, configuration ReleaseFS_open_AVX2, including the installer.

From the repo root, in a new shell:

Windows (cmd.exe):
  call scripts\bootstrap-autobuild.cmd
  autobuild configure -A 64 -c ReleaseFS_open_AVX2 -- --package
  autobuild build -A 64 -c ReleaseFS_open_AVX2 --no-configure

Linux (bash; source the script, do not execute it):
  source scripts/bootstrap-autobuild.sh
  autobuild configure -A 64 -c ReleaseFS_open_AVX2
  autobuild build -A 64 -c ReleaseFS_open_AVX2

Do not set AUTOBUILD_VARIABLES_FILE yourself. The bootstrap sets it to fs-build-variables/variables in this checkout. Do not set AUTOBUILD_VSVER yourself. The Windows bootstrap sets it to 170 only when both VS 2022 and VS 2026 are installed. Leave a value that is already set.

On Linux, autobuild.xml's ReleaseFS_open_AVX2 configure and build options already pass --platform linux --package --avx2. Do not add a second --package. On Windows that configuration does not pass --package; the -- --package after configure is what doc/building_windows.md says runs NSIS.

ReleaseFS_open_AVX2 is the open AVX2 target (no Kakadu, no FMOD). Do not configure ReleaseFS_AVX2. AVX2 binaries do not run on a CPU without AVX2.

2. MCP sidecar. Separate Go module. The viewer does not build it. From tools/fs-mcp, with Go 1.25 or newer:

  go test ./...
  go vet ./...
  go build -o fs-mcp ./cmd/fs-mcp

On Windows the output name in tools/fs-mcp/README.md is fs-mcp.exe:
  go build -o fs-mcp.exe ./cmd/fs-mcp

3. Settings migrator. Separate Go module. Not Python. From tools/migrate-settings (this is the command in .github/workflows/migrate-settings.yml):

  go test ./...
  go vet ./...
  go build -o migrate-settings .

Do not point AUTOBUILD_VARIABLES_FILE at a sibling checkout. Do not pip install autobuild outside the bootstrap venv. Stop when a command fails and report the command and the error.
```

## Quick guide

Three separate builds, in this order.

### 1. Viewer (`ReleaseFS_open_AVX2`, with the installer)

Windows, from the repo root in cmd.exe:

```
call scripts\bootstrap-autobuild.cmd
autobuild configure -A 64 -c ReleaseFS_open_AVX2 -- --package
autobuild build -A 64 -c ReleaseFS_open_AVX2 --no-configure
```

Linux, from the repo root in bash:

```
source scripts/bootstrap-autobuild.sh
autobuild configure -A 64 -c ReleaseFS_open_AVX2
autobuild build -A 64 -c ReleaseFS_open_AVX2
```

`ReleaseFS_open_AVX2` on Linux already includes `--package` and `--avx2` in `autobuild.xml`. The Windows configuration does not include `--package`; `-- --package` is the NSIS switch from `doc/building_windows.md`.

### 2. MCP sidecar (`tools/fs-mcp`)

Go 1.25 or newer. Not part of the viewer build.

```
cd tools/fs-mcp
go test ./...
go vet ./...
go build -o fs-mcp ./cmd/fs-mcp
```

Windows output name: `go build -o fs-mcp.exe ./cmd/fs-mcp`.

### 3. Settings migrator (`tools/migrate-settings`)

Go, not Python. Not the MCP build.

```
cd tools/migrate-settings
go test ./...
go vet ./...
go build -o migrate-settings .
```

## Handhold guide

You are producing three programs. They stay separate. Finish the viewer before the sidecar, and the sidecar before the migrator. A failure in one does not mean you should rerun the others with different flags.

One-time tools, already written up in the OS docs:

- Windows: [building_windows.md](building_windows.md). Use cmd.exe. Install Visual Studio 2022 (Desktop development with C++), CMake on PATH, Cygwin with `patch` (Cygwin's `bin` after CMake and before `%SystemRoot%\system32`), Git, and NSIS if you want the installer. The bootstrap needs Python; it prefers `py -3.10` through `py -3.13`, then `python`.
- Linux: [building_linux.md](building_linux.md). Ubuntu 22.04 is the documented base. Install the packages in that doc:

```
sudo apt install libgl1-mesa-dev libglu1-mesa-dev libpulse-dev build-essential python3-pip git libssl-dev libxinerama-dev libxrandr-dev libfontconfig-dev libfreetype6-dev gcc-11 cmake
```

Go has to be on `PATH` before the viewer configure. `indra/newview/CMakeLists.txt` stops with `Go 1.22 or newer is required to build migrate-settings` when `go` is missing. `tools/fs-mcp/go.mod` says `go 1.25.0`, and `tools/fs-mcp/README.md` says Go 1.25 or newer. Install Go 1.25 or newer once and use it for all three.

### Known machine setup

From the repo root, start the Autobuild session with the bootstrap. Do this again in each new terminal. Windows:

```
call scripts\bootstrap-autobuild.cmd
```

Linux bash (this has to be sourced; running it with `bash scripts/bootstrap-autobuild.sh` exits and does not keep the variables):

```
source scripts/bootstrap-autobuild.sh
```

That session then has:

- **Autobuild in a venv.** `.venv` is created if needed, `pip install -r requirements.txt` runs, and that environment's `autobuild` is on `PATH`. Check it with `autobuild --version` (Windows doc: 3.8 or higher; Linux doc: 3.9.3 or higher).
- **`AUTOBUILD_VARIABLES_FILE`.** Set to this checkout's `fs-build-variables/variables` (`fs-build-variables\variables` on Windows). You do not clone a second variables repo. If the variable is already set, the bootstrap leaves it. If it is unset when `configure_firestorm.sh` runs, that script selects the same in-repo file.
- **`AUTOBUILD_VSVER`.** Windows only. The cmd script sets it to `170` (Visual Studio 2022) when both VS 2022 and VS 2026 are installed, so Autobuild does not pick 2026. If you already set `AUTOBUILD_VSVER`, it is left alone. If `ProgramFiles(x86)` is missing, the script sets it to `C:\Program Files (x86)` so `vswhere.exe` can be found. `autobuild.xml` names the Windows build directory `build-vc${AUTOBUILD_VSVER|170}-64`, so with `170` the directory is `build-vc170-64`.

Do not export those three yourself unless you are deliberately overriding the bootstrap.

### 1. Viewer

`ReleaseFS_open_AVX2` is this fork's open AVX2 configuration: no Kakadu, no FMOD, viewer and OpenJPEG built with AVX2. `doc/building_windows.md` says not to use the name `ReleaseFS_AVX2`; that name is still Kakadu/FMOD and does not pass `--avx2`. The resulting binaries do not run on a CPU without AVX2.

Windows, still in the bootstrapped cmd window, repo root. NSIS must already be installed. `--package` is what makes the build run NSIS:

```
autobuild configure -A 64 -c ReleaseFS_open_AVX2 -- --package
autobuild build -A 64 -c ReleaseFS_open_AVX2 --no-configure
```

Linux, still in the bootstrapped bash, repo root. This configuration's options in `autobuild.xml` are already `--platform linux`, `--package`, and `--avx2`:

```
autobuild configure -A 64 -c ReleaseFS_open_AVX2
autobuild build -A 64 -c ReleaseFS_open_AVX2
```

The first configure downloads third-party libraries. `doc/building_windows.md` notes that download progress stays hidden unless you add `-v` (`autobuild configure -A 64 -v -c ReleaseFS_open_AVX2` on Windows, and add `-- --package` when you still want the installer).

What success looks like: the build command exits 0. `configure_firestorm.sh` prints `finished` when its own build step succeeds.

- Linux: a ready-to-run tree at `build-linux-x86_64/newview/packaged`, which is the directory `doc/building_linux.md` tells you to copy. `install.sh` is placed at the root of that package (`viewer_manifest.py` copies `linux_tools/install.sh`). For a Release build the archive name is `Phoenix-<app>_AVX2-<version>.tar.xz` (`fs_installer_basename` in `indra/newview/fs_viewer_manifest.py`, then `package_file = installer_name + '.tar.xz'`). The Linux doc copies `build-linux-x86_64/newview/Phoenix*.tar.*`.
- Windows: NSIS writes `Phoenix-<app>_AVX2-<version>_Setup.exe` (`fs_installer_basename` plus `_Setup.exe`) into the newview destination under `build-vc170-64` when `AUTOBUILD_VSVER` is 170. The Visual Studio generator is multi-config, so that destination includes the `Release` folder.

Viewer configure also runs `CGO_ENABLED=0 go build` in `tools/migrate-settings` and expects `go` on `PATH`. That copy is for the installer. It is not the MCP sidecar, and it is not a Python program.

### 2. MCP sidecar

`tools/fs-mcp/README.md` says the viewer does not link or build this program. Leave the Autobuild variables alone; this build does not read them.

```
cd tools/fs-mcp
go test ./...
go vet ./...
go build -o fs-mcp ./cmd/fs-mcp
```

On Windows use `go build -o fs-mcp.exe ./cmd/fs-mcp`.

What success looks like: `go test` and `go vet` exit 0, and `tools/fs-mcp/fs-mcp` exists (`fs-mcp.exe` on Windows). The sidecar talks MCP on stdin and stdout. It is not the viewer executable.

### 3. Settings migrator

`tools/migrate-settings` is a Go program. There is no Python build for it. The workflow `.github/workflows/migrate-settings.yml` builds it on its own:

```
cd tools/migrate-settings
go test ./...
go vet ./...
go build -o migrate-settings .
```

What success looks like: those three commands exit 0, and `tools/migrate-settings/migrate-settings` exists. CMake's packaged Windows name is `migrate-settings.exe` because it passes `CMAKE_EXECUTABLE_SUFFIX`. The workflow command above is the standalone build, including on the Windows job.

Run `migrate-settings` (or `migrate-settings.exe`) from a terminal when you want the copy. `doc/help.md` describes what it asks and what it refuses to copy. That run is not part of the build.
