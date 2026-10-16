import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location("pack",Path(__file__).parents[1]/"folderstorm_velopack.py")
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)


class Bundle(unittest.TestCase):
    def test_complete_exact_variant_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            identity={"schema":1,"version":"7.2.5.123","commit":"a"*40,"installer":"velopack","protocol":16,"app_id":"Folderstorm.opensim.nightly.x86_64.avx2"}
            (root/"folderstorm-bundle.json").write_text(json.dumps(identity))
            (root/"folderstorm-session-viewer.txt").write_text("Folderstorm.exe\n")
            for name in ("Folderstorm.exe","folderstorm-session-host.exe","fs-mcp.exe","folderstorm-update.exe","migrate-settings.exe"):(root/name).write_bytes(b"fixture")
            command=pack.command(root,root.parent/"output")
            self.assertEqual("7.2.5-123",command[command.index("--packVersion")+1])
            self.assertEqual(identity["app_id"],command[command.index("--packId")+1])
            (root/"folderstorm-update.exe").unlink()
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")

    def test_rejects_nsis_and_unrecognized_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/"folderstorm-bundle.json").write_text(json.dumps({"installer":"nsis"}))
            with self.assertRaises(ValueError):pack.command(root,root.parent/"output")
