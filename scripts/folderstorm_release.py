#!/usr/bin/env python3
"""Prepare a release/feed from retained installers; optionally upload a DRAFT.

No build, website deployment or release publication is performed by this tool.
Input JSON: version, channel, commit, notes_file, assets [{file, platform,
architecture, cpu, flavor, installer}]. Paths are relative to the input JSON.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import quote, urlsplit

MAX_ASSETS, MAX_RELEASES, MAX_NOTES = 24, 32, 32768
REPOSITORY = "DrCum/Folderstorm"
RELEASES = "https://github.com/" + REPOSITORY + "/releases"


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value):
        raise ValueError("version must contain four canonical numeric components")
    if any(int(part) > 2**64-1 for part in value.split(".")):
        raise ValueError("version component exceeds uint64")
    return tuple(map(int, value.split(".")))


def https_base(value):
    parsed = urlsplit(value)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.port:
        raise ValueError("download base must be a public HTTPS URL without credentials, port, query or fragment")
    if not re.fullmatch(r"[a-z0-9.-]+", parsed.netloc) or "\\" in value or any(ord(c) < 33 for c in value):
        raise ValueError("invalid download base")
    return value.rstrip("/")


def bounded_json(path, maximum=2*1024*1024):
    with path.open("rb") as stream:
        raw = stream.read(maximum+1)
    if len(raw) > maximum:
        raise ValueError("input manifest is too large")
    return json.loads(raw)


def prepare(input_path, output, download_base=None, previous=None):
    data = bounded_json(input_path)
    version(data["version"])
    if data["channel"] not in ("stable", "nightly") or not re.fullmatch(r"[0-9a-f]{40}", data["commit"]):
        raise ValueError("channel and exact source commit are required")
    tag = "folderstorm-" + data["channel"] + "-" + data["version"]
    release_url = RELEASES + "/tag/" + tag
    base = https_base(download_base) if download_base else RELEASES + "/download/" + tag
    notes_path = input_path.parent / data["notes_file"]
    notes = notes_path.read_bytes()
    if len(notes) > MAX_NOTES or any(byte < 32 and byte not in (9, 10, 13) for byte in notes):
        raise ValueError("changelog exceeds 32 KiB or contains control characters")
    notes = notes.decode("utf-8").replace("\r\n", "\n")
    if "<!-- folderstorm-release-v1" in notes:
        raise ValueError("changelog must not contain release metadata markers")
    if not isinstance(data["assets"], list) or not 1 <= len(data["assets"]) <= MAX_ASSETS:
        raise ValueError("a release needs 1 to 24 installer assets")
    assets, paths, names, variants = [], [], set(), set()
    for entry in data["assets"]:
        path = (input_path.parent / entry["file"]).resolve(strict=True)
        if not path.is_file() or path.stat().st_size <= 0:
            raise ValueError("installer must be a nonempty regular file")
        if not download_base and path.stat().st_size >= 2*1024**3:
            raise ValueError("GitHub release assets must be under 2 GiB; use --download-base for larger installers")
        if not re.fullmatch(r"[A-Za-z0-9._-]{1,180}", path.name) or path.name in names:
            raise ValueError("installer names must be unique simple filenames")
        allowed = {"platform": ("windows", "macos", "linux"), "architecture": ("x86_64", "arm64"),
                   "cpu": ("baseline", "avx", "avx2"), "flavor": ("opensim", "secondlife"),
                   "installer": ("nsis", "dmg", "tar", "velopack")}
        if any(entry.get(key) not in values for key, values in allowed.items()):
            raise ValueError("installer platform/architecture/cpu/flavor/type is invalid")
        variant = tuple(entry[key] for key in allowed)
        if variant in variants:
            raise ValueError("duplicate installer variant")
        if entry["platform"] == "windows" and path.suffix.lower() != ".exe":
            raise ValueError("Windows installer must be an .exe")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024*1024), b""):
                digest.update(block)
        assets.append({**{key: entry[key] for key in allowed}, "url": base + "/" + quote(path.name),
                       "sha256": digest.hexdigest(), "bytes": path.stat().st_size})
        paths.append(path); names.add(path.name); variants.add(variant)
    release = {"schema": 1, "version": data["version"], "channel": data["channel"], "commit": data["commit"],
               "url": release_url, "notes": notes, "assets": assets}
    releases = []
    if previous:
        prior = bounded_json(previous)
        if prior.get("schema") != 1 or not isinstance(prior.get("releases"), list) or len(prior["releases"]) > MAX_RELEASES:
            raise ValueError("invalid previous feed")
        for item in prior["releases"]:
            version(item["version"])
            if item["version"] == release["version"] and item["channel"] == release["channel"]:
                if item != release:
                    raise ValueError("published release identity is immutable; assign a new build version")
            else:
                releases.append(item)
    releases.append(release)
    releases.sort(key=lambda item: version(item["version"]), reverse=True)
    output.mkdir(parents=True, exist_ok=True)
    (output / "folderstorm-releases.json").write_text(json.dumps({"schema": 1, "releases": releases[:MAX_RELEASES]}, indent=2)+"\n", encoding="utf-8")
    (output / "folderstorm-release.json").write_text(json.dumps(release, indent=2)+"\n", encoding="utf-8")
    (output / "release-notes.md").write_text(notes + "\n\n<!-- folderstorm-release-v1\n" + json.dumps(release, separators=(",", ":")) + "\n-->\n", encoding="utf-8")
    return tag, release, paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--previous-feed", type=Path)
    parser.add_argument("--download-base")
    parser.add_argument("--github-draft", action="store_true", help="Explicitly create/upload a private draft release; never publish it")
    args = parser.parse_args()
    try:
        tag, release, paths = prepare(args.manifest.resolve(), args.output, args.download_base, args.previous_feed)
        if args.github_draft:
            if args.download_base:
                raise ValueError("upload external assets to your hosting first; --github-draft uploads GitHub assets only")
            command = ["gh", "release", "create", tag, "--repo", REPOSITORY, "--draft", "--target", release["commit"],
                       "--title", "Folderstorm " + release["version"], "--notes-file", str(args.output / "release-notes.md")]
            if release["channel"] == "nightly":
                command.append("--prerelease")
            subprocess.run(command, check=True)
            # A failed upload leaves a draft, never an advertised partial release.
            subprocess.run(["gh", "release", "upload", tag, "--repo", REPOSITORY] + [str(path) for path in paths] +
                           [str(args.output / "folderstorm-release.json")], check=True)
        print(f"Prepared {tag}. Verify uploads and native acceptance before explicitly publishing the release/feed.")
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
