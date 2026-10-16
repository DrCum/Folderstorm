#!/usr/bin/env python3
"""Validate exact-build native symbols; upload only with --upload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def prepare(version, commit, files):
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version):
        raise ValueError("Use the exact four-part viewer version")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Use the exact source commit")
    result = {"release": f"folderstorm@{version}+{commit}", "files": []}
    names = set()
    for source in files:
        path = Path(source).resolve(strict=True)
        if not path.is_file() or path.suffix.lower() != ".pdb" or path.name.lower() in names:
            raise ValueError("Provide distinct viewer/host/handler PDB files")
        names.add(path.name.lower())
        with path.open("rb") as stream:
            if not stream.read(32).startswith(b"Microsoft C/C++ MSF 7.00"):
                raise ValueError(f"Not a native PDB: {path.name}")
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        result["files"].append({"path": str(path), "sha256": digest, "bytes": path.stat().st_size})
    if len(result["files"]) < 2:
        raise ValueError("Include at least the matching viewer and host PDBs")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--org")
    parser.add_argument("--project")
    parser.add_argument("--upload", action="store_true")
    parser.add_argument("pdb", nargs="+")
    args = parser.parse_args()
    try:
        manifest = prepare(args.version, args.commit, args.pdb)
        print(json.dumps(manifest, indent=2))
        if args.upload:
            if not args.org or not args.project or not os.environ.get("SENTRY_AUTH_TOKEN"):
                raise ValueError("Upload requires --org, --project and SENTRY_AUTH_TOKEN in the environment")
            paths = [entry["path"] for entry in manifest["files"]]
            for path in paths:
                subprocess.run(["sentry-cli", "debug-files", "check", path], check=True)
            subprocess.run(["sentry-cli", "debug-files", "upload", "--org", args.org,
                            "--project", args.project, *paths], check=True)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
