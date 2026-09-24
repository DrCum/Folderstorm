#!/usr/bin/env python3
"""Tests for migrate_settings.py. Run: python3 test_migrate_settings.py"""

import os
import stat
import sys
import tempfile
import unittest
from io import StringIO
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import migrate_settings as mig


def _run(argv, root, platform, extra_env=None, interactive=False, stdin_text=""):
    env = {"HOME": str(root), "USERPROFILE": str(root)}
    if extra_env:
        env.update(extra_env)
    stdout = StringIO()
    stderr = StringIO()
    code = mig.main(
        argv,
        stdin=StringIO(stdin_text),
        stdout=stdout,
        stderr=stderr,
        environ=env,
        platform=platform,
        interactive=interactive,
    )
    return code, stdout.getvalue(), stderr.getvalue()


class LocateTests(unittest.TestCase):
    def test_linux_names_and_env_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            home = root / "home"
            custom = root / "custom-fs"
            (custom / "user_settings").mkdir(parents=True)
            (custom / "user_settings" / "settings.xml").write_text("<settings/>")
            (home / ".firestorm_x64" / "user_settings").mkdir(parents=True)
            (home / ".firestorm_x64" / "user_settings" / "settings.xml").write_text("default")
            dest_root = root / "custom-folder"
            env = {
                "HOME": str(home),
                "FIRESTORM_X64_USER_DIR": str(custom),
                "FOLDERSTORM_X64_USER_DIR": str(dest_root),
            }
            source = mig.choose_source(mig.source_candidates("linux", home, env))
            dest = mig.destination_dir("linux", home, env, None)
            self.assertEqual(source, custom / "user_settings")
            self.assertEqual(dest, dest_root / "user_settings")

    def test_linux_prefers_x64_when_both_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            for name in (".firestorm_x64", ".firestorm"):
                folder = home / name / "user_settings"
                folder.mkdir(parents=True)
                (folder / "settings.xml").write_text(name)
            source = mig.choose_source(mig.source_candidates("linux", home, {"HOME": str(home)}))
            self.assertEqual(source, home / ".firestorm_x64" / "user_settings")
            dest = mig.destination_dir("linux", home, {}, None)
            self.assertEqual(dest, home / ".folderstorm_x64" / "user_settings")

    def test_windows_uses_appdata_and_ignores_linux_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            appdata = root / "Roaming"
            (appdata / "Firestorm_x64" / "user_settings").mkdir(parents=True)
            (appdata / "Firestorm_x64" / "user_settings" / "settings.xml").write_text("win")
            env = {
                "HOME": str(root / "home"),
                "APPDATA": str(appdata),
                "FIRESTORM_X64_USER_DIR": str(root / "ignored"),
                "FOLDERSTORM_X64_USER_DIR": str(root / "ignored-dest"),
            }
            source = mig.choose_source(mig.source_candidates("windows", root / "home", env))
            dest = mig.destination_dir("windows", root / "home", env, None)
            self.assertEqual(source, appdata / "Firestorm_x64" / "user_settings")
            self.assertEqual(dest, appdata / "Folderstorm_x64" / "user_settings")

    def test_macos_settings_have_no_x64_suffix(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            support = home / "Library" / "Application Support"
            (support / "Firestorm" / "user_settings").mkdir(parents=True)
            (support / "Firestorm" / "user_settings" / "settings.xml").write_text("mac")
            (support / "Firestorm_x64" / "user_settings").mkdir(parents=True)
            (support / "Firestorm_x64" / "user_settings" / "settings.xml").write_text("other")
            source = mig.choose_source(mig.source_candidates("darwin", home, {}))
            dest = mig.destination_dir("darwin", home, {}, None)
            self.assertEqual(source, support / "Firestorm" / "user_settings")
            self.assertEqual(dest, support / "Folderstorm" / "user_settings")

    def test_skips_empty_candidate_for_one_with_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            (home / ".firestorm_x64" / "user_settings").mkdir(parents=True)
            folder = home / ".firestorm" / "user_settings"
            folder.mkdir(parents=True)
            (folder / "settings.xml").write_text("old")
            source = mig.choose_source(mig.source_candidates("linux", home, {}))
            self.assertEqual(source, folder)

    def test_unsupported_platform(self):
        code, _out, err = _run(["--check"], Path("/tmp"), "")
        self.assertEqual(code, mig.EXIT_ERROR)
        self.assertIn("Linux, Windows, and macOS", err)


class CopyTests(unittest.TestCase):
    def _seed(self, home):
        source = home / ".firestorm_x64" / "user_settings"
        source.mkdir(parents=True)
        (source / "settings.xml").write_text("prefs")
        (source / "key_bindings.xml").write_text("keys")
        (source / "password.dat").write_text("secret-password")
        (source / "bin_conf.dat").write_text("secret-store")
        (source / "fs-mcp-4242.json").write_text('{"token":"secret"}')
        (source / "presets" / "graphic").mkdir(parents=True)
        (source / "presets" / "graphic" / "photo.xml").write_text("preset")
        cache = source / "cache"
        cache.mkdir()
        (cache / "texture.dat").write_text("cached")
        (source / "browser_profile").mkdir()
        (source / "browser_profile" / "Cookies").write_text("cookie")
        (source / "logs").mkdir()
        (source / "logs" / "viewer.log").write_text("log")
        link = source / "linked-secret.xml"
        link.symlink_to(source / "password.dat")
        # Sibling cache must not be part of the user_settings copy.
        sibling = home / ".firestorm_x64" / "cache"
        sibling.mkdir()
        (sibling / "huge.bin").write_text("nope")
        return source

    def test_yes_copies_settings_and_skips_secrets_and_caches(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            code, out, err = _run(["--yes"], home, "linux")
            self.assertEqual(code, 0, err)
            dest = home / ".folderstorm_x64" / "user_settings"
            self.assertEqual((dest / "settings.xml").read_text(), "prefs")
            self.assertEqual((dest / "presets" / "graphic" / "photo.xml").read_text(), "preset")
            self.assertFalse((dest / "password.dat").exists())
            self.assertFalse((dest / "bin_conf.dat").exists())
            self.assertFalse((dest / "fs-mcp-4242.json").exists())
            self.assertFalse((dest / "cache").exists())
            self.assertFalse((dest / "browser_profile").exists())
            self.assertFalse((dest / "logs").exists())
            self.assertFalse((dest / "linked-secret.xml").exists())
            self.assertFalse((home / ".folderstorm_x64" / "cache").exists())
            self.assertIn("Left", out)
            self.assertIn("credential", out)

    def test_refuses_without_yes_when_not_interactive(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            code, _out, err = _run([], home, "linux", interactive=False)
            self.assertEqual(code, mig.EXIT_DECLINED)
            self.assertIn("--yes", err)
            self.assertFalse((home / ".folderstorm_x64").exists())

    def test_prompt_no_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            code, _out, err = _run([], home, "linux", interactive=True, stdin_text="n\n")
            self.assertEqual(code, mig.EXIT_DECLINED)
            self.assertFalse((home / ".folderstorm_x64").exists())
            self.assertIn("unchanged", err)

    def test_prompt_yes_copies(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            code, _out, err = _run([], home, "linux", interactive=True, stdin_text="yes\n")
            self.assertEqual(code, 0, err)
            dest = home / ".folderstorm_x64" / "user_settings" / "settings.xml"
            self.assertTrue(dest.is_file())

    def test_does_not_overwrite_without_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            dest = home / ".folderstorm_x64" / "user_settings"
            dest.mkdir(parents=True)
            (dest / "settings.xml").write_text("already")
            (dest / "folderstorm-only.xml").write_text("keep")
            code, _out, err = _run(["--yes"], home, "linux")
            self.assertEqual(code, mig.EXIT_DEST_EXISTS)
            self.assertIn("--overwrite", err)
            self.assertEqual((dest / "settings.xml").read_text(), "already")
            self.assertEqual((dest / "folderstorm-only.xml").read_text(), "keep")
            self.assertFalse((dest / "key_bindings.xml").exists())

    def test_overwrite_replaces_copied_files_and_keeps_extras(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            dest = home / ".folderstorm_x64" / "user_settings"
            dest.mkdir(parents=True)
            (dest / "settings.xml").write_text("already")
            (dest / "folderstorm-only.xml").write_text("keep")
            (dest / "password.dat").write_text("dest-secret")
            code, _out, err = _run(["--yes", "--overwrite"], home, "linux")
            self.assertEqual(code, 0, err)
            self.assertEqual((dest / "settings.xml").read_text(), "prefs")
            self.assertEqual((dest / "folderstorm-only.xml").read_text(), "keep")
            self.assertEqual((dest / "password.dat").read_text(), "dest-secret")
            self.assertFalse((dest / "bin_conf.dat").exists())

    def test_overwrite_prompt_required_when_interactive(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            dest = home / ".folderstorm_x64" / "user_settings"
            dest.mkdir(parents=True)
            (dest / "settings.xml").write_text("already")
            code, _out, _err = _run(
                ["--yes"], home, "linux", interactive=True, stdin_text="n\n"
            )
            self.assertEqual(code, mig.EXIT_DEST_EXISTS)
            self.assertEqual((dest / "settings.xml").read_text(), "already")
            code, _out, err = _run(
                ["--yes"], home, "linux", interactive=True, stdin_text="y\n"
            )
            self.assertEqual(code, 0, err)
            self.assertEqual((dest / "settings.xml").read_text(), "prefs")

    def test_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            code, out, err = _run(["--dry-run"], home, "linux")
            self.assertEqual(code, 0, err)
            self.assertIn("Dry run", out)
            self.assertIn("Would copy", out)
            self.assertFalse((home / ".folderstorm_x64").exists())

    def test_credential_only_source_is_not_copied(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            source = home / ".firestorm_x64" / "user_settings"
            source.mkdir(parents=True)
            (source / "password.dat").write_text("secret")
            (source / "bin_conf.dat").write_text("secret")
            (source / "cache").mkdir()
            (source / "cache" / "blob").write_text("cache")
            code, _out, err = _run(["--yes"], home, "linux")
            self.assertEqual(code, mig.EXIT_NO_SOURCE)
            self.assertFalse((home / ".folderstorm_x64").exists())
            self.assertIn("No Firestorm settings", err)

    def test_check_exit_codes(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            code, out, _err = _run(["--check"], home, "linux")
            self.assertEqual(code, mig.EXIT_NO_SOURCE)
            self.assertEqual(out, "")
            self._seed(home)
            code, out, _err = _run(["--check"], home, "linux")
            self.assertEqual(code, 0)
            self.assertEqual(out, "")
            dest = home / ".folderstorm_x64" / "user_settings"
            dest.mkdir(parents=True)
            (dest / "settings.xml").write_text("already")
            code, _out, _err = _run(["--check"], home, "linux")
            self.assertEqual(code, mig.EXIT_DEST_EXISTS)

    def test_credential_only_destination_is_not_occupied(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self._seed(home)
            dest = home / ".folderstorm_x64" / "user_settings"
            dest.mkdir(parents=True)
            (dest / "password.dat").write_text("leave-me")
            code, _out, err = _run(["--yes"], home, "linux")
            self.assertEqual(code, 0, err)
            self.assertEqual((dest / "settings.xml").read_text(), "prefs")
            self.assertEqual((dest / "password.dat").read_text(), "leave-me")

    def test_explicit_source_and_dest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "from" / "user_settings"
            source.mkdir(parents=True)
            (source / "settings.xml").write_text("moved")
            dest = root / "to" / "user_settings"
            code, _out, err = _run(
                ["--yes", "--source", str(root / "from"), "--dest", str(dest)],
                root / "home",
                "linux",
            )
            self.assertEqual(code, 0, err)
            self.assertEqual((dest / "settings.xml").read_text(), "moved")

    def test_refuses_nested_dest(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            source = self._seed(home)
            nested = source / "nested"
            code, _out, err = _run(
                ["--yes", "--source", str(source), "--dest", str(nested)],
                home,
                "linux",
            )
            self.assertEqual(code, mig.EXIT_ERROR)
            self.assertIn("inside", err)
            self.assertFalse(nested.exists())

    def test_does_not_copy_symlink_to_password(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            source = self._seed(home)
            mode = (source / "password.dat").stat().st_mode
            self.assertTrue(stat.S_ISREG(mode))
            code, _out, err = _run(["--yes"], home, "linux")
            self.assertEqual(code, 0, err)
            dest = home / ".folderstorm_x64" / "user_settings"
            names = {path.name for path in dest.rglob("*")}
            self.assertNotIn("password.dat", names)
            self.assertNotIn("linked-secret.xml", names)


class PlatformDetectTests(unittest.TestCase):
    def test_detect(self):
        self.assertEqual(mig.detect_platform("linux"), "linux")
        self.assertEqual(mig.detect_platform("win32"), "windows")
        self.assertEqual(mig.detect_platform("darwin"), "darwin")
        self.assertEqual(mig.detect_platform("freebsd13"), "")


if __name__ == "__main__":
    unittest.main()
