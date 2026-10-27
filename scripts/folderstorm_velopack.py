#!/usr/bin/env python3
"""Validate a matching staged app bundle; optionally pack it with Velopack."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import importlib.util

_editor_spec = importlib.util.spec_from_file_location("folderstorm_theme_editor", Path(__file__).with_name("folderstorm_theme_editor.py"))
_editor = importlib.util.module_from_spec(_editor_spec)
_editor_spec.loader.exec_module(_editor)


def command(bundle_dir, output):
    marker = bundle_dir / "folderstorm-bundle.json"
    if marker.stat().st_size > 4096:
        raise ValueError("Invalid bundle identity")
    bundle = json.loads(marker.read_text(encoding="utf-8"))
    if bundle.get("schema") != 1 or bundle.get("installer") != "velopack" or not isinstance(bundle.get("protocol"), int) or bundle["protocol"] < 16:
        raise ValueError("Use the matching Velopack staging directory")
    if not re.fullmatch(r"[0-9a-f]{40}", bundle.get("commit", "")) or not re.fullmatch(r"Folderstorm\.(opensim|secondlife)\.(stable|nightly)\.(x86_64|arm64)\.(baseline|avx|avx2)", bundle.get("app_id", "")):
        raise ValueError("Invalid bundle variant/commit")
    version = bundle.get("version", "")
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("Invalid four-part build version")
    guide = (bundle_dir / "folderstorm-session-viewer.txt").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,180}\.exe", guide):
        raise ValueError("Invalid packaged viewer filename")
    for name in (guide, "folderstorm-session-host.exe", "fs-mcp.exe", "folderstorm-update.exe", "migrate-settings.exe", "fs-mcp-launcher-maintenance.exe"):
        path = bundle_dir / name
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"Missing bundle component: {name}")
    _editor.validate_bundle(bundle_dir, bundle)
    parts = version.rsplit(".", 1)
    return ["vpk", "pack", "--packId", bundle["app_id"], "--packVersion", "-".join(parts),
            "--packDir", str(bundle_dir), "--mainExe", guide, "--packTitle", "Folderstorm",
            "--outputDir", str(output), "--shortcuts", "", "--exclude", r".*\.pdb|.*\.map|.*\.lib|.*\.exp|.*Setup\.exe"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--previous", type=Path, help="Previously published Velopack output; preserves delta bases")
    parser.add_argument("--pack", action="store_true", help="Actually invoke the pinned vpk CLI; default only validates/prints")
    args = parser.parse_args()
    try:
        source, output = args.bundle.resolve(strict=True), args.output.resolve()
        if output == source or source in output.parents or output in source.parents:
            raise ValueError("Keep package output separate from the staged app")
        invocation = command(source, output)
        print(json.dumps({"command": invocation, "previous": str(args.previous) if args.previous else None}, indent=2))
        if args.pack:
            version = subprocess.run(["vpk", "--version"], capture_output=True, text=True, check=True).stdout
            if "0.0.1535" not in version:
                raise ValueError("Use vpk 0.0.1535 to match the pinned native SDK")
            if args.previous:
                previous = args.previous.resolve(strict=True)
                if previous == output or output in previous.parents or previous in output.parents:
                    raise ValueError("Previous and output directories must be separate")
                shutil.copytree(previous, output, dirs_exist_ok=True)
            output.mkdir(parents=True, exist_ok=True)
            # vpk uses retained full packages in output to generate deltas.
            subprocess.run(invocation, check=True)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
