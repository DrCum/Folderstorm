import ast
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("editor_package", ROOT / "scripts/folderstorm_theme_editor.py")
editor = importlib.util.module_from_spec(spec); spec.loader.exec_module(editor)


def fixture(root):
    root.mkdir(parents=True, exist_ok=True)
    for name in editor.REQUIRED:
        (root / name).write_bytes(b"fixture")
    image = bytearray(70); image[:2] = b"MZ"; struct.pack_into("<I", image, 60, 64); image[64:] = b"PE\0\0\x64\x86"
    (root / "Folderstorm.Theme.Editor.exe").write_bytes(image)
    (root / "Folderstorm.Theme.Editor.runtimeconfig.json").write_text(json.dumps({"runtimeOptions": {"includedFrameworks": [{"name": "Microsoft.WindowsDesktop.App", "version": "10.0.9"}]}}))
    (root / "Folderstorm.Theme.Editor.pdb").write_bytes(b"private symbols")
    return root


def package(path, root, omit=(), replacements=None, extras=None):
    with zipfile.ZipFile(path, "w") as archive:
        for file in root.rglob("*"):
            name = file.relative_to(root).as_posix()
            if file.is_file() and file.suffix != ".pdb" and name not in omit:
                archive.writestr("lib/app/theme-editor/" + name, (replacements or {}).get(name, file.read_bytes()))
        for name, data in (extras or {}).items():
            archive.writestr("lib/app/theme-editor/" + name, data)


class EditorPackage(unittest.TestCase):
    def test_sealing_rejects_dump_tools_before_recording_an_unshippable_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = fixture(Path(directory) / "publish")
            (root / "createdump.exe").write_bytes(b"optional diagnostic tool")
            with self.assertRaisesRegex(ValueError, "createdump"):
                editor.seal(root, "a" * 40, "0.21.0")
            self.assertFalse((root / editor.MARKER).exists())
            self.assertTrue((root / "createdump.exe").exists())

    def test_finished_archive_requires_exact_inventory_manifest_and_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = fixture(Path(directory) / "theme-editor")
            document = editor.seal(root, "a" * 40, "0.21.0")
            bundle = {"commit": "a" * 40, "theme_editor": True}
            archive = Path(directory) / "full.nupkg"
            package(archive, root)
            self.assertEqual(document, editor.validate_archive(root.parent, bundle, archive))
            marker = (root / editor.MARKER).read_bytes()
            cases = [
                {"omit": ["coreclr.dll"]},
                {"replacements": {"coreclr.dll": b"altered"}},
                {"replacements": {editor.MARKER: marker.replace(b'"schema": 1', b'"schema": 2')}},
                {"extras": {"extra.dll": b"unsealed"}},
                {"extras": {"CORECLR.dll": b"fixture"}},
            ]
            for case in cases:
                with self.subTest(case=case):
                    package(archive, root, **case)
                    with self.assertRaises(ValueError): editor.validate_archive(root.parent, bundle, archive)
            package(archive, root)
            with self.assertRaises(ValueError):
                editor.validate_archive(root.parent, {"theme_editor": False}, archive)

    def test_legacy_createdump_omission_is_detected_after_successful_tool_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = fixture(Path(directory) / "app/theme-editor")
            document = editor.seal(root, "a" * 40, "0.21.0")
            # Reproduce the older valid staged inventory from the blocked build.
            (root / "createdump.exe").write_bytes(b"optional diagnostic tool")
            document["files"] = editor.files(root)
            (root / editor.MARKER).write_text(json.dumps(document))
            self.assertEqual(document, editor.validate(root, "a" * 40))
            bundle = {"commit": "a" * 40, "theme_editor": True, "installer": "velopack", "app_id": "Folderstorm.opensim.nightly.x86_64.avx2"}
            (root.parent / "folderstorm-bundle.json").write_text(json.dumps(bundle))
            releases = root.parent.parent / "Releases"; releases.mkdir()
            archive = releases / (bundle["app_id"] + "-7.2.5-123-full.nupkg")
            package(archive, root, omit=["createdump.exe"])
            tree = ast.parse((ROOT / "indra/newview/viewer_manifest.py").read_text())
            method = next(node for owner in tree.body if isinstance(owner, ast.ClassDef) for node in owner.body
                          if isinstance(node, ast.FunctionDef) and node.name == "velopack_package_finish")
            namespace = {"json": json, "Path": Path, "os": os, "shutil": shutil, "theme_editor_package": editor, "ManifestError": ValueError}
            exec(compile(ast.fix_missing_locations(ast.Module(body=[method], type_ignores=[])), "production-velopack-finish", "exec"), namespace)
            class Manifest:
                args = {"version": ["7", "2", "5", "123"]}
                def get_dst_prefix(self): return str(root.parent)
                def get_src_prefix(self): return str(root.parent.parent / "source")
                def final_exe(self): return "Folderstorm.exe"
                def fs_installer_basename(self): return "Folderstorm"
                def icon_path(self): return "icons"
            with patch.dict(os.environ, {"GITHUB_ACTIONS": ""}), patch("subprocess.run", return_value=SimpleNamespace(returncode=0, stdout="", stderr="")):
                with self.assertRaisesRegex(ValueError, "createdump.exe"):
                    namespace["velopack_package_finish"](Manifest())
                package(archive, root)
                namespace["velopack_package_finish"](Manifest())

    def test_sealed_self_contained_publish_rejects_changed_extra_and_wrong_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = fixture(Path(directory) / "publish")
            manifest = editor.seal(root, "a" * 40, "0.20.0")
            self.assertEqual(manifest, editor.validate(root, "a" * 40))
            self.assertNotIn("Folderstorm.Theme.Editor.pdb", manifest["files"])
            with self.assertRaises(ValueError): editor.validate(root, "b" * 40)
            (root / "extra.dll").write_bytes(b"changed")
            with self.assertRaises(ValueError): editor.validate(root, "a" * 40)
            (root / "extra.dll").unlink(); (root / "coreclr.dll").write_bytes(b"different")
            with self.assertRaises(ValueError): editor.validate(root, "a" * 40)

    def test_runtime_and_architecture_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = fixture(Path(directory) / "publish")
            (root / "coreclr.dll").unlink()
            with self.assertRaises(ValueError): editor.seal(root, "a" * 40, "0.20.0")
            fixture(root); (root / "Folderstorm.Theme.Editor.exe").write_bytes(b"not PE")
            with self.assertRaises(ValueError): editor.seal(root, "a" * 40, "0.20.0")
            fixture(root); (root / "Folderstorm.Theme.Editor.runtimeconfig.json").write_text('{"runtimeOptions":{"framework":{"name":"Microsoft.WindowsDesktop.App"}}}')
            with self.assertRaises(ValueError): editor.seal(root, "a" * 40, "0.20.0")

    def test_staged_cleanup_requires_owned_child_and_preserves_unrecognized_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); output = root / "stage"; output.mkdir()
            unknown = output / "theme-editor"; unknown.mkdir(); (unknown / "keep.txt").write_text("keep")
            with self.assertRaises(ValueError): editor.remove_staged(output)
            self.assertTrue((unknown / "keep.txt").is_file())
            fixture(unknown); editor.seal(unknown, "a" * 40, "0.20.0"); editor.remove_staged(output)
            self.assertFalse(unknown.exists())
            editor.remove_staged(output)

    def test_bundle_flag_requires_exact_editor_or_absence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); bundle = {"commit": "a" * 40, "theme_editor": True}
            with self.assertRaises(ValueError): editor.validate_bundle(root, bundle)
            publish = fixture(root / "theme-editor"); editor.seal(publish, "a" * 40, "0.20.0")
            editor.validate_bundle(root, bundle)
            bundle["theme_editor"] = False
            with self.assertRaises(ValueError): editor.validate_bundle(root, bundle)
            editor.remove_staged(root); self.assertIsNone(editor.validate_bundle(root, bundle))

    def test_production_manifest_stages_file_list_and_off_removes_only_owned_editor(self):
        # Isolate the production method from unrelated platform/llsd imports.
        tree = ast.parse((ROOT / "indra/newview/viewer_manifest.py").read_text())
        owner = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "ViewerManifest")
        method = next(node for node in owner.body if isinstance(node, ast.FunctionDef) and node.name == "stage_theme_editor")
        module = ast.Module(body=[method], type_ignores=[]); namespace = {"json": json, "Path": Path, "os": os, "theme_editor_package": editor, "ManifestError": ValueError}
        exec(compile(ast.fix_missing_locations(module), "production-stage-theme-editor", "exec"), namespace)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); build = root / "build"; build.mkdir(); output = root / "stage"; output.mkdir()
            publish = fixture(root / "publish"); sealed = editor.seal(publish, "a" * 40, "0.20.0")
            (build / "folderstorm-bundle.json").write_text(json.dumps({"commit": "a" * 40, "theme_editor": True}))
            class Manifest:
                args = {"theme-editor": "ON", "theme-editor-source": str(publish)}
                def get_build_prefix(self): return str(build)
                def get_dst_prefix(self): return str(output)
                def path(self, source, destination):
                    target = output / destination; target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target); self.records.append(destination)
            manifest = Manifest(); manifest.records = []; namespace["stage_theme_editor"](manifest)
            self.assertEqual(len(sealed["files"]) + 1, len(manifest.records))
            self.assertTrue((output / "theme-editor/Folderstorm.Theme.Editor.exe").is_file())
            self.assertFalse((output / "theme-editor/Folderstorm.Theme.Editor.pdb").exists())
            (build / "folderstorm-bundle.json").write_text(json.dumps({"commit": "a" * 40, "theme_editor": False}))
            manifest.args = {"theme-editor": "OFF", "theme-editor-source": "not a publish"}; namespace["stage_theme_editor"](manifest)
            self.assertFalse((output / "theme-editor").exists())


if __name__ == "__main__": unittest.main()
