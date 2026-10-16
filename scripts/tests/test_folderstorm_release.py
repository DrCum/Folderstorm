import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("release", Path(__file__).resolve().parents[1] / "folderstorm_release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleasePublishing(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "notes.md").write_text("Fixed hosted typing.\n", encoding="utf-8")
        (self.root / "Folderstorm-Setup.exe").write_bytes(b"MZ retained native installer fixture")
        self.data = {"version": "7.2.5.82125", "channel": "nightly", "commit": "a"*40, "notes_file": "notes.md",
                     "assets": [{"file": "Folderstorm-Setup.exe", "platform": "windows", "architecture": "x86_64", "cpu": "avx2", "flavor": "opensim", "installer": "nsis"}]}

    def prepare(self, **kwargs):
        source = self.root / "input.json"
        source.write_text(json.dumps(self.data), encoding="utf-8")
        return release.prepare(source, self.root / "out", **kwargs)

    def test_hashes_installer_and_embeds_exact_release(self):
        tag, data, paths = self.prepare()
        self.assertEqual(tag, "folderstorm-nightly-7.2.5.82125")
        self.assertEqual(len(data["assets"][0]["sha256"]), 64)
        self.assertEqual(paths[0].stat().st_size, data["assets"][0]["bytes"])
        body = (self.root / "out/release-notes.md").read_text()
        decoded = json.loads(body.split("<!-- folderstorm-release-v1\n")[1].split("\n-->")[0])
        self.assertEqual(decoded, data)

    def test_custom_host_and_immutable_published_identity(self):
        _, data, _ = self.prepare(download_base="https://downloads.example.test/updates")
        self.assertTrue(data["assets"][0]["url"].startswith("https://downloads.example.test/updates/"))
        prior = self.root / "out/folderstorm-releases.json"
        self.prepare(download_base="https://downloads.example.test/updates", previous=prior)
        (self.root / "notes.md").write_text("Changed published notes")
        with self.assertRaises(ValueError):
            self.prepare(download_base="https://downloads.example.test/updates", previous=prior)

    def test_rejects_duplicate_variants_and_unsafe_names(self):
        self.data["assets"].append(dict(self.data["assets"][0]))
        with self.assertRaises(ValueError): self.prepare()
        self.data["assets"] = self.data["assets"][:1]
        self.data["assets"][0]["cpu"] = "native"
        with self.assertRaises(ValueError): self.prepare()

    def test_version_and_host_validation(self):
        for value in ("7.2.5", "07.2.5.1", "7.2.5.18446744073709551616", "7.2.5.1;cmd"):
            with self.assertRaises(ValueError): release.version(value)
        for value in ("http://example.test/u", "https://user:pass@example.test/u", "https://example.test/u?token=x", "https://example.test\\evil/u"):
            with self.assertRaises(ValueError): release.https_base(value)

    def test_rejects_embedded_metadata_and_excessive_notes(self):
        for text in ("<!-- folderstorm-release-v1\n{}\n-->", "a"*32769):
            (self.root / "notes.md").write_text(text)
            with self.assertRaises(ValueError): self.prepare()


if __name__ == "__main__": unittest.main()
