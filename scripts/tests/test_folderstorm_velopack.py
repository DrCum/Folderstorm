import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

spec=importlib.util.spec_from_file_location("pack",Path(__file__).parents[1]/"folderstorm_velopack.py")
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)


def write_bundle(root):
    identity={"schema":1,"version":"7.2.5.123","commit":"a"*40,"installer":"velopack","protocol":16,"app_id":"Folderstorm.opensim.nightly.x86_64.avx2"}
    (root/"folderstorm-bundle.json").write_text(json.dumps(identity))
    (root/"folderstorm-session-viewer.txt").write_text("Folderstorm.exe\n")
    for name in ("Folderstorm.exe","folderstorm-session-host.exe","fs-mcp.exe","folderstorm-update.exe","migrate-settings.exe","fs-mcp-launcher-maintenance.exe"):
        (root/name).write_bytes(b"fixture")
    return identity


class Bundle(unittest.TestCase):
    def test_complete_exact_variant_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            identity=write_bundle(root)
            command=pack.command(root,root.parent/"output")
            self.assertEqual("7.2.5-123",command[command.index("--packVersion")+1])
            self.assertEqual(identity["app_id"],command[command.index("--packId")+1])
            (root/"folderstorm-update.exe").unlink()
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")

    def test_rejects_nsis_and_unrecognized_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/"folderstorm-bundle.json").write_text(json.dumps({"installer":"nsis"}))
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")


class Preflight(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        self.bundle = root / "bundle"
        self.bundle.mkdir()
        write_bundle(self.bundle)
        self.output = root / "output"
        self.previous = root / "previous"
        self.previous.mkdir()
        (self.previous / "retained.nupkg").write_bytes(b"previous package")

    def invoke(self, *arguments):
        argv = ["folderstorm_velopack.py", str(self.bundle), "--output", str(self.output), *arguments]
        with mock.patch("sys.argv", argv), mock.patch("sys.stdout", new_callable=io.StringIO), mock.patch("sys.stderr", new_callable=io.StringIO):
            pack.main()

    @mock.patch.object(pack.subprocess, "run")
    def test_pack_accepts_pinned_release_and_prerelease(self, run):
        for version in ("0.0.1535", "0.0.1535-gb21da2a"):
            with self.subTest(version=version):
                run.reset_mock()
                help_text = f"Description:\r\n  Velopack CLI {version} (prerelease), for distributing applications.\r\n"
                run.return_value = subprocess.CompletedProcess([], 0, stdout=help_text)
                self.invoke("--pack", "--previous", str(self.previous))
                self.assertEqual(run.call_count, 2)
                self.assertEqual(run.call_args_list[0], mock.call(["vpk", "--help"], capture_output=True, text=True, check=True))
                self.assertEqual(run.call_args_list[1], mock.call(pack.command(self.bundle, self.output), check=True))
                self.assertEqual((self.output / "retained.nupkg").read_bytes(), b"previous package")

    @mock.patch.object(pack.subprocess, "run")
    def test_pack_rejects_wrong_or_unrecognized_version_before_writing(self, run):
        banners = [f"Velopack CLI {version}, for distributing applications.\n"
                   for version in ("0.0.1536", "0.0.15350", "10.0.1535", "0.0.1535.1", "0.0.1535garbage")]
        banners += ["", "vpk 0.0.1535\n", "Velopack CLI 1.0.0\nUse 0.0.1535 to match the SDK.\n"]
        for index, help_text in enumerate(banners):
            with self.subTest(help_text=help_text):
                self.output = self.output.parent / f"output-{index}"
                run.reset_mock()
                run.return_value = subprocess.CompletedProcess([], 0, stdout=help_text)
                with self.assertRaises(SystemExit) as error:
                    self.invoke("--pack", "--previous", str(self.previous))
                self.assertEqual(error.exception.code, 1)
                self.assertEqual(run.call_count, 1)
                self.assertFalse(self.output.exists())

    @mock.patch.object(pack.subprocess, "run")
    def test_pack_stops_if_cli_is_missing_or_help_fails(self, run):
        for failure in (FileNotFoundError("vpk"), subprocess.CalledProcessError(1, ["vpk", "--help"], output="Velopack CLI 0.0.1535")):
            with self.subTest(failure=failure):
                run.reset_mock()
                run.side_effect = failure
                with self.assertRaises(SystemExit) as error:
                    self.invoke("--pack", "--previous", str(self.previous))
                self.assertEqual(error.exception.code, 1)
                self.assertEqual(run.call_count, 1)
                self.assertFalse(self.output.exists())

    @mock.patch.object(pack.subprocess, "run")
    def test_print_only_does_not_require_vpk(self, run):
        self.invoke()
        run.assert_not_called()
        self.assertFalse(self.output.exists())
