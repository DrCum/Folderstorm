#!/usr/bin/env python3
"""Seal and validate the optional self-contained Windows theme editor."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import stat
import os
import zipfile

MARKER = ".folderstorm-theme-editor.json"
MAX_FILES, MAX_BYTES = 1024, 512 * 1024 * 1024
REQUIRED = {"Folderstorm.Theme.Editor.exe", "Folderstorm.Theme.Editor.dll", "Folderstorm.Theme.Core.dll",
            "Folderstorm.Theme.Editor.runtimeconfig.json", "Folderstorm.Theme.Editor.deps.json",
            "hostfxr.dll", "coreclr.dll", "PresentationFramework.dll", "System.Xaml.dll"}


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def linked(path):
    return path.is_symlink() or bool(getattr(path.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def files(root):
    if linked(root) or not root.is_dir():
        raise ValueError("Editor output must be a real directory")
    result, names, total = {}, set(), 0
    for path in root.rglob("*"):
        if linked(path):
            raise ValueError("Editor output cannot contain links")
        if not path.is_file():
            continue
        name = path.relative_to(root).as_posix()
        if name == MARKER or path.suffix.lower() == ".pdb":
            continue
        if name.casefold() in names or len(result) >= MAX_FILES:
            raise ValueError("Editor output has duplicate or excessive files")
        names.add(name.casefold()); total += path.stat().st_size
        if total > MAX_BYTES:
            raise ValueError("Editor output exceeds 512 MiB")
        result[name] = {"size": path.stat().st_size, "sha256": digest(path)}
    return dict(sorted(result.items()))


def identity(source, version):
    if not isinstance(source, str) or not re.fullmatch(r"[0-9a-f]{40}", source):
        raise ValueError("Editor requires an exact source commit")
    if not isinstance(version, str) or not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("Editor requires a canonical three-part version")


def runtime(root, entries):
    if not REQUIRED.issubset(entries) or any(not entries[name]["size"] for name in REQUIRED):
        raise ValueError("Editor publish is missing its self-contained runtime")
    app = root / "Folderstorm.Theme.Editor.exe"
    with app.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError("Editor apphost is not a Windows executable")
        offset = struct.unpack_from("<I", header, 60)[0]
        if offset > 16 * 1024 * 1024:
            raise ValueError("Invalid editor PE header")
        stream.seek(offset); machine = stream.read(6)
        if machine != b"PE\0\0\x64\x86":
            raise ValueError("Editor apphost must be Windows x64")
    config_path = root / "Folderstorm.Theme.Editor.runtimeconfig.json"
    if config_path.stat().st_size > 65536:
        raise ValueError("Editor runtime configuration is oversized")
    settings = json.loads(config_path.read_text(encoding="utf-8"))
    config = settings.get("runtimeOptions") if isinstance(settings, dict) else None
    if not isinstance(config, dict):
        raise ValueError("Invalid editor runtime configuration")
    if "framework" in config or "frameworks" in config:
        raise ValueError("Editor must not require an installed .NET runtime")
    included = config.get("includedFrameworks", [])
    if not isinstance(included, list) or not any(isinstance(item, dict) and item.get("name") == "Microsoft.WindowsDesktop.App" and str(item.get("version", "")).startswith("10.") for item in included):
        raise ValueError("Editor must include its .NET 10 Windows Desktop runtime")


def seal(root, source, version):
    root = Path(root); identity(source, version); entries = files(root); runtime(root, entries)
    if any(Path(name).name.casefold().startswith("createdump") for name in entries):
        raise ValueError("Editor publish contains createdump; publish to a fresh directory with the diagnostic tool excluded before sealing")
    document = {"schema": 1, "kind": "folderstorm-theme-editor", "source_commit": source, "editor_version": version,
                "platform": "windows-x64", "self_contained": True, "files": entries}
    (root / MARKER).write_text(json.dumps(document, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return document


def validate(root, source):
    root = Path(root); marker = root / MARKER
    if not marker.is_file() or linked(marker) or marker.stat().st_size > 512 * 1024:
        raise ValueError("Editor package manifest is missing or oversized")
    document = json.loads(marker.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError("Invalid editor package manifest")
    identity(document.get("source_commit"), document.get("editor_version"))
    if document.get("schema") != 1 or document.get("kind") != "folderstorm-theme-editor" or document.get("source_commit") != source or document.get("platform") != "windows-x64" or document.get("self_contained") is not True:
        raise ValueError("Editor package does not match the native source/build")
    actual = files(root)
    if actual != document.get("files"):
        raise ValueError("Editor package files changed after publish")
    runtime(root, actual)
    return document


def remove_staged(destination):
    """Only replace the manifest-owned direct child of this packaging output."""
    destination = Path(destination).resolve(); folder = destination / "theme-editor"
    if not os.path.lexists(folder):
        return
    if linked(folder) or folder.resolve().parent != destination:
        raise ValueError("Staged editor must remain inside its packaging output")
    marker = folder / MARKER
    if not marker.is_file() or linked(marker) or marker.stat().st_size > 512 * 1024:
        raise ValueError("Refusing to replace an unrecognized staged editor directory")
    document = json.loads(marker.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError("Refusing to replace an unrecognized staged editor directory")
    identity(document.get("source_commit"), document.get("editor_version"))
    if document.get("schema") != 1 or document.get("kind") != "folderstorm-theme-editor":
        raise ValueError("Refusing to replace an unrecognized staged editor directory")
    for path in folder.rglob("*"):
        if linked(path):
            raise ValueError("Refusing to replace a linked staged editor")
    shutil.rmtree(folder)


def validate_bundle(root, bundle):
    folder = Path(root) / "theme-editor"
    enabled = bundle.get("theme_editor", False)
    if not isinstance(enabled, bool):
        raise ValueError("Invalid optional editor bundle flag")
    if enabled:
        return validate(folder, bundle["commit"])
    if os.path.lexists(folder):
        raise ValueError("Editor files are present in a bundle configured without the editor")
    return None


def validate_archive(root, bundle, package):
    """Check the installed editor bytes, including transforms performed by vpk."""
    root = Path(root)
    document = validate_bundle(root, bundle)
    expected = document["files"] if document else {}
    marker = (root / "theme-editor" / MARKER).read_bytes() if document else None
    prefix = "lib/app/theme-editor/"
    try:
        with zipfile.ZipFile(package) as archive:
            entries = {}
            folded = set()
            for entry in archive.infolist():
                if not entry.filename.casefold().startswith(prefix) or entry.is_dir():
                    continue
                name = entry.filename[len(prefix):]
                if (entry.filename[:len(prefix)] != prefix or name.casefold() in folded
                        or len(entries) >= MAX_FILES + 1 or stat.S_ISLNK(entry.external_attr >> 16)):
                    raise ValueError("Packaged editor has invalid or duplicate entries")
                entries[name] = entry
                folded.add(name.casefold())
            names = set(expected) | ({MARKER} if document else set())
            if set(entries) != names:
                missing = sorted(names - entries.keys())
                extra = sorted(entries.keys() - names)
                raise ValueError(f"Packaged editor inventory differs: missing {missing}, extra {extra}")
            for name, entry in entries.items():
                size = len(marker) if name == MARKER else expected[name]["size"]
                if entry.file_size != size:
                    raise ValueError(f"Packaged editor size changed: {name}")
                with archive.open(entry) as stream:
                    if name == MARKER:
                        if stream.read() != marker:
                            raise ValueError("Packaged editor manifest changed")
                    else:
                        value = hashlib.sha256()
                        for block in iter(lambda: stream.read(1024 * 1024), b""):
                            value.update(block)
                        if value.hexdigest() != expected[name]["sha256"]:
                            raise ValueError(f"Packaged editor hash changed: {name}")
    except (zipfile.BadZipFile, RuntimeError, NotImplementedError) as error:
        raise ValueError(f"Cannot read packaged editor: {error}") from error
    return document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("publish", type=Path); parser.add_argument("--source", required=True); parser.add_argument("--version", required=True)
    args = parser.parse_args()
    document = seal(args.publish, args.source, args.version)
    print(f"Sealed editor {document['editor_version']}: {len(document['files'])} files, source {document['source_commit']}")


if __name__ == "__main__":
    main()
