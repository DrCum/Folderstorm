import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import struct

spec=importlib.util.spec_from_file_location("pack",Path(__file__).parents[1]/"folderstorm_velopack.py")
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)


class Bundle(unittest.TestCase):
    def test_optional_editor_is_required_when_declared_and_hash_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            identity={"schema":1,"version":"7.2.5.123","commit":"a"*40,"installer":"velopack","protocol":16,"app_id":"Folderstorm.opensim.nightly.x86_64.avx2","theme_editor":True}
            (root/"folderstorm-bundle.json").write_text(json.dumps(identity));(root/"folderstorm-session-viewer.txt").write_text("Folderstorm.exe\n")
            for name in ("Folderstorm.exe","folderstorm-session-host.exe","fs-mcp.exe","folderstorm-update.exe","migrate-settings.exe","fs-mcp-launcher-maintenance.exe"):(root/name).write_bytes(b"fixture")
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")
            publish=root/"theme-editor";publish.mkdir()
            for name in pack._editor.REQUIRED:(publish/name).write_bytes(b"fixture")
            image=bytearray(70);image[:2]=b"MZ";struct.pack_into("<I",image,60,64);image[64:]=b"PE\0\0\x64\x86";(publish/"Folderstorm.Theme.Editor.exe").write_bytes(image)
            (publish/"Folderstorm.Theme.Editor.runtimeconfig.json").write_text(json.dumps({"runtimeOptions":{"includedFrameworks":[{"name":"Microsoft.WindowsDesktop.App","version":"10.0.9"}]}}))
            pack._editor.seal(publish,"a"*40,"0.20.0");self.assertIn("--packDir",pack.command(root,root.parent/"output"))
            (publish/"coreclr.dll").write_bytes(b"changed")
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")

    def test_complete_exact_variant_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            identity={"schema":1,"version":"7.2.5.123","commit":"a"*40,"installer":"velopack","protocol":16,"app_id":"Folderstorm.opensim.nightly.x86_64.avx2"}
            (root/"folderstorm-bundle.json").write_text(json.dumps(identity))
            (root/"folderstorm-session-viewer.txt").write_text("Folderstorm.exe\n")
            for name in ("Folderstorm.exe","folderstorm-session-host.exe","fs-mcp.exe","folderstorm-update.exe","migrate-settings.exe","fs-mcp-launcher-maintenance.exe"):(root/name).write_bytes(b"fixture")
            command=pack.command(root,root.parent/"output")
            self.assertEqual("7.2.5-123",command[command.index("--packVersion")+1])
            self.assertEqual(identity["app_id"],command[command.index("--packId")+1])
            (root/"folderstorm-update.exe").unlink()
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")

    def test_rejects_nsis_and_unrecognized_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/"folderstorm-bundle.json").write_text(json.dumps({"installer":"nsis"}))
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")
