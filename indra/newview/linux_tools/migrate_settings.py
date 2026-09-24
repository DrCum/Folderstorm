#!/usr/bin/env python3
"""Copy Firestorm user settings into Folderstorm.

Folderstorm does not read the Firestorm settings folder. This tool copies
that folder only when asked. It leaves caches and stored credentials behind,
and it will not write into a Folderstorm settings folder that already has
files unless --overwrite is set or the prompt is confirmed.

The directory names match the viewer:

- Linux 64-bit user dir is ~/.folderstorm_x64 (lldir_linux.cpp lowercases
  the app name passed from llappviewer.cpp, which is Folderstorm_x64).
  FOLDERSTORM_X64_USER_DIR replaces it. Firestorm's matching variable is
  FIRESTORM_X64_USER_DIR.
- Windows user dir is %APPDATA%\\Folderstorm_x64. There is no environment
  override on Windows.
- macOS settings, logs, and temp files use ~/Library/Application Support/Folderstorm
  with no _x64 suffix (lldir_mac.cpp). The _x64 suffix is the cache directory
  only.

Run from a checkout, from the Linux package next to install.sh, from the
Windows install folder, or from Folderstorm.app/Contents/Resources:

    python3 migrate_settings.py
    python3 migrate_settings.py --yes
    python3 migrate_settings.py --yes --overwrite
    python3 migrate_settings.py --dry-run
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import shutil
import sys
from pathlib import Path
from typing import Iterable, List, Mapping, Optional, Sequence, TextIO, Tuple


# Login secrets live in the protected store and the legacy password file.
# fs-mcp-*.json is the local assistant bridge token. plugin_cookies are
# per-account browser cookies; skip them if they show up in this tree.
CREDENTIAL_FILE_NAMES = frozenset({
    "password.dat",
    "bin_conf.dat",
    "plugin_cookies.txt",
    "plugin_cookies.xml",
})
CREDENTIAL_DIR_NAMES = frozenset({
    "browser_profile",
})
# Cache directories used by lldir / the texture, object, and CEF caches.
# "logs" is not settings. A nested logs directory is left behind with the caches.
CACHE_DIR_NAMES = frozenset({
    "cache",
    "texturecache",
    "objectcache",
    "soundcache",
    "cef_cache",
    "logs",
})

# Linux-only. lldir_linux.cpp reads <UPPER(app_name)>_USER_DIR. The 64-bit
# viewer passes Folderstorm_x64 / Firestorm_x64. Older and OpenSim-named
# folders are fallbacks for installs that used those app names.
LINUX_SOURCE_ENVS = (
    "FIRESTORM_X64_USER_DIR",
    "FIRESTORM_USER_DIR",
    "FIRESTORMOS_X64_USER_DIR",
    "FIRESTORMOS_USER_DIR",
)
LINUX_DEST_ENV = "FOLDERSTORM_X64_USER_DIR"

SOURCE_DIR_NAMES = {
    "linux": (
        ".firestorm_x64",
        ".firestorm",
        ".firestormos_x64",
        ".firestormos",
    ),
    "windows": (
        "Firestorm_x64",
        "Firestorm",
        "FirestormOS_x64",
        "FirestormOS",
    ),
    # macOS settings use the product name. _x64 is not the settings folder.
    "darwin": (
        "Firestorm",
        "FirestormOS",
        "Firestorm_x64",
        "FirestormOS_x64",
    ),
}
DEST_DIR_NAME = {
    "linux": ".folderstorm_x64",
    "windows": "Folderstorm_x64",
    "darwin": "Folderstorm",
}

EXIT_OK = 0
EXIT_DECLINED = 1
EXIT_NO_SOURCE = 2
EXIT_DEST_EXISTS = 3
EXIT_ERROR = 4


class MigrationError(Exception):
    def __init__(self, message: str, code: int) -> None:
        super().__init__(message)
        self.code = code


def detect_platform(name: Optional[str] = None) -> str:
    """Return linux, windows, or darwin. Empty string if this repo has no layout."""
    name = sys.platform if name is None else name
    if name.startswith("linux"):
        return "linux"
    if name == "darwin":
        return "darwin"
    if name == "win32":
        return "windows"
    return ""


def as_user_settings(path: Path) -> Path:
    """Accept either the app data directory or the user_settings directory inside it."""
    if path.name.casefold() == "user_settings":
        return path
    return path / "user_settings"


def settings_root(platform: str, home: Path, environ: Mapping[str, str]) -> Path:
    if platform == "windows":
        appdata = environ.get("APPDATA")
        if appdata:
            return Path(appdata)
        return home / "AppData" / "Roaming"
    if platform == "darwin":
        return home / "Library" / "Application Support"
    if platform == "linux":
        return home
    raise MigrationError(
        "This operating system is not one Folderstorm packages for. "
        "The viewer settings layout is implemented for Linux, Windows, and macOS.",
        EXIT_ERROR,
    )


def _home_dir(environ: Mapping[str, str]) -> Path:
    home = environ.get("HOME") or environ.get("USERPROFILE")
    if home:
        return Path(home)
    return Path.home()


def source_candidates(
    platform: str,
    home: Path,
    environ: Mapping[str, str],
) -> List[Path]:
    candidates: List[Path] = []
    # The Firestorm viewer only honors *_USER_DIR on Linux.
    if platform == "linux":
        for key in LINUX_SOURCE_ENVS:
            value = environ.get(key, "").strip()
            if value:
                candidates.append(as_user_settings(Path(value)))
    root = settings_root(platform, home, environ)
    for name in SOURCE_DIR_NAMES[platform]:
        candidates.append(root / name / "user_settings")
    return candidates


def destination_dir(
    platform: str,
    home: Path,
    environ: Mapping[str, str],
    dest_arg: Optional[str],
) -> Path:
    if dest_arg:
        return as_user_settings(Path(os.path.expanduser(dest_arg)))
    if platform == "linux":
        value = environ.get(LINUX_DEST_ENV, "").strip()
        if value:
            return as_user_settings(Path(value))
    root = settings_root(platform, home, environ)
    return root / DEST_DIR_NAME[platform] / "user_settings"


def _dir_skip_reason(name: str) -> Optional[str]:
    folded = name.casefold()
    if folded in CREDENTIAL_DIR_NAMES:
        return "credentials"
    if folded in CACHE_DIR_NAMES or folded.endswith(".old_texturecache"):
        return "cache"
    return None


def _file_skip_reason(name: str) -> Optional[str]:
    folded = name.casefold()
    if folded in CREDENTIAL_FILE_NAMES:
        return "credentials"
    if fnmatch.fnmatch(folded, "fs-mcp-*.json"):
        return "credentials"
    return None


def iter_tree(root: Path) -> Iterable[Tuple[str, Path, Optional[str]]]:
    """Yield ('dir'|'file', relative path, skip reason or None).

    Skipped directories are not descended into. Symlinks are reported and
    not followed, so a link cannot pull in a credential file from elsewhere.
    """
    if not root.is_dir():
        return
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        current = Path(dirpath)
        rel_dir = current.relative_to(root)
        kept: List[str] = []
        for name in dirnames:
            rel = rel_dir / name
            full = current / name
            if full.is_symlink():
                yield ("dir", rel, "link")
                continue
            reason = _dir_skip_reason(name)
            if reason:
                yield ("dir", rel, reason)
                continue
            kept.append(name)
        dirnames[:] = kept
        for name in filenames:
            rel = rel_dir / name
            full = current / name
            if full.is_symlink():
                yield ("file", rel, "link")
                continue
            reason = _file_skip_reason(name)
            if reason:
                yield ("file", rel, reason)
                continue
            if not full.is_file():
                yield ("file", rel, "other")
                continue
            yield ("file", rel, None)


def has_copyable_files(root: Path) -> bool:
    if not root.is_dir():
        return False
    for kind, _rel, reason in iter_tree(root):
        if kind == "file" and reason is None:
            return True
    return False


def choose_source(candidates: Sequence[Path]) -> Optional[Path]:
    """Prefer the first folder that actually contains settings files."""
    first_existing: Optional[Path] = None
    for candidate in candidates:
        if not candidate.is_dir():
            continue
        if first_existing is None:
            first_existing = candidate
        if has_copyable_files(candidate):
            return candidate
    return first_existing


def _norm(path: Path) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def _contains(parent: Path, child: Path) -> bool:
    parent_norm = _norm(parent)
    child_norm = _norm(child)
    if parent_norm == child_norm:
        return False
    try:
        return os.path.commonpath([parent_norm, child_norm]) == parent_norm
    except ValueError:
        return False


def _ask(prompt: str, stdin: TextIO, stdout: TextIO) -> bool:
    stdout.write(prompt)
    stdout.flush()
    line = stdin.readline()
    if not line:
        return False
    return line.strip().casefold() in ("y", "yes")


def _rel_list(items: Iterable[Tuple[str, Path, Optional[str]]], reason: str) -> List[Tuple[str, Path]]:
    return [(kind, rel) for kind, rel, item_reason in items if item_reason == reason]


def describe_plan(
    source: Path,
    dest: Path,
    entries: Sequence[Tuple[str, Path, Optional[str]]],
    dest_occupied: bool,
    stdout: TextIO,
    dry_run: bool,
) -> None:
    copy_files = [rel for kind, rel, reason in entries if kind == "file" and reason is None]
    credentials = _rel_list(entries, "credentials")
    caches = _rel_list(entries, "cache")
    links = _rel_list(entries, "link")
    verb = "Would copy" if dry_run else "Copied"
    stdout.write("From: %s\n" % source)
    stdout.write("To:   %s\n" % dest)
    if dest_occupied and dry_run:
        stdout.write("Folderstorm already has settings. A real copy needs --overwrite.\n")
    stdout.write("%s %d file(s).\n" % (verb, len(copy_files)))
    if credentials:
        stdout.write("Left %d stored-credential path(s) behind.\n" % len(credentials))
    if caches:
        stdout.write("Left %d cache or log path(s) behind.\n" % len(caches))
    if links:
        stdout.write("Left %d link(s) behind.\n" % len(links))


def copy_settings(source: Path, dest: Path, overwrite: bool) -> int:
    """Copy copyable files. Return the number of files written.

    Does not delete destination files that were not part of the copy.
    """
    dest.mkdir(parents=True, exist_ok=True)
    written = 0
    for kind, rel, reason in iter_tree(source):
        if reason is not None:
            continue
        target = dest / rel
        if kind == "dir":
            target.mkdir(parents=True, exist_ok=True)
            continue
        if target.exists() and not overwrite:
            raise MigrationError(
                "Refusing to replace %s. Re-run with --overwrite." % target,
                EXIT_DEST_EXISTS,
            )
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / rel, target)
        written += 1
    return written


def migrate(
    platform: str,
    environ: Mapping[str, str],
    source_arg: Optional[str],
    dest_arg: Optional[str],
    assume_yes: bool,
    overwrite: bool,
    dry_run: bool,
    check_only: bool,
    interactive: bool,
    stdin: TextIO,
    stdout: TextIO,
) -> int:
    if not platform:
        raise MigrationError(
            "This operating system is not one Folderstorm packages for. "
            "The viewer settings layout is implemented for Linux, Windows, and macOS.",
            EXIT_ERROR,
        )
    home = _home_dir(environ)
    if source_arg:
        source = as_user_settings(Path(os.path.expanduser(source_arg)))
    else:
        source = choose_source(source_candidates(platform, home, environ))
    dest = destination_dir(platform, home, environ, dest_arg)

    if source is None or not source.is_dir() or not has_copyable_files(source):
        if check_only:
            return EXIT_NO_SOURCE
        where = source if source is not None else "the usual Firestorm folder"
        raise MigrationError(
            "No Firestorm settings to copy (looked for %s)." % where,
            EXIT_NO_SOURCE,
        )
    if _norm(source) == _norm(dest) or _contains(source, dest) or _contains(dest, source):
        raise MigrationError(
            "Source and destination are the same folder or one is inside the other.",
            EXIT_ERROR,
        )

    occupied = dest.is_dir() and has_copyable_files(dest)
    if check_only:
        return EXIT_DEST_EXISTS if occupied else EXIT_OK

    entries = list(iter_tree(source))
    if dry_run:
        describe_plan(source, dest, entries, occupied, stdout, dry_run=True)
        stdout.write("Dry run. Nothing was written.\n")
        return EXIT_OK

    if not assume_yes:
        if not interactive:
            raise MigrationError(
                "Not copying settings. Re-run with --yes to copy, "
                "and --overwrite if Folderstorm already has settings.",
                EXIT_DECLINED,
            )
        stdout.write("Copy Firestorm settings\n")
        stdout.write("  from %s\n" % source)
        stdout.write("  to   %s\n" % dest)
        stdout.write("Saved passwords and caches are not copied.\n")
        if not _ask("Copy these settings? [y/N] ", stdin, stdout):
            raise MigrationError("Leaving settings unchanged.", EXIT_DECLINED)

    if occupied and not overwrite:
        if not interactive:
            raise MigrationError(
                "Folderstorm already has settings in %s. "
                "Re-run with --overwrite to replace files copied from Firestorm. "
                "Files that exist only in Folderstorm are kept." % dest,
                EXIT_DEST_EXISTS,
            )
        stdout.write("Folderstorm already has settings in %s\n" % dest)
        if not _ask(
            "Replace files copied from Firestorm? Files that exist only in Folderstorm are kept. [y/N] ",
            stdin,
            stdout,
        ):
            raise MigrationError(
                "Leaving Folderstorm settings unchanged.",
                EXIT_DEST_EXISTS,
            )

    copy_settings(source, dest, overwrite=occupied)
    describe_plan(source, dest, entries, False, stdout, dry_run=False)
    return EXIT_OK


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="migrate_settings.py",
        description=(
            "Copy Firestorm user settings into Folderstorm. "
            "Does nothing unless you pass --yes or answer the prompt. "
            "Does not replace an existing Folderstorm settings folder unless "
            "you pass --overwrite or confirm that prompt."
        ),
    )
    parser.add_argument(
        "--yes",
        action="store_true",
        help="Copy without the first confirmation prompt.",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace files in a Folderstorm settings folder that already has settings.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the plan and write nothing.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help=(
            "Print nothing. Exit 0 if a copy can proceed, "
            "2 if there is nothing to copy, 3 if Folderstorm already has settings."
        ),
    )
    parser.add_argument(
        "--source",
        help="Firestorm user_settings directory, or the Firestorm data directory that contains it.",
    )
    parser.add_argument(
        "--dest",
        help="Folderstorm user_settings directory, or the Folderstorm data directory that contains it.",
    )
    return parser


def main(
    argv: Optional[Sequence[str]] = None,
    stdin: Optional[TextIO] = None,
    stdout: Optional[TextIO] = None,
    stderr: Optional[TextIO] = None,
    environ: Optional[Mapping[str, str]] = None,
    platform: Optional[str] = None,
    interactive: Optional[bool] = None,
) -> int:
    stdin = sys.stdin if stdin is None else stdin
    stdout = sys.stdout if stdout is None else stdout
    stderr = sys.stderr if stderr is None else stderr
    environ = os.environ if environ is None else environ
    if platform is None:
        platform = detect_platform()
    if interactive is None:
        interactive = bool(getattr(stdin, "isatty", lambda: False)())
    args = build_parser().parse_args(list(argv) if argv is not None else None)
    try:
        return migrate(
            platform=platform,
            environ=environ,
            source_arg=args.source,
            dest_arg=args.dest,
            assume_yes=args.yes,
            overwrite=args.overwrite,
            dry_run=args.dry_run,
            check_only=args.check,
            interactive=interactive,
            stdin=stdin,
            stdout=stdout,
        )
    except MigrationError as exc:
        stderr.write("%s\n" % exc)
        return exc.code
    except OSError as exc:
        stderr.write("Could not copy settings: %s\n" % exc)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
