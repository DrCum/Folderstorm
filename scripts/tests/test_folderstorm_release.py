import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import hashlib
import zipfile
from unittest import mock

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
        self.assertNotIn("notes",decoded)
        self.assertEqual(decoded,{key:value for key,value in data.items() if key!="notes"})
        self.assertEqual(body.split("\n\n<!-- folderstorm-release-v1\n")[0],data["notes"])

    def test_generated_files_use_lf_even_with_windows_text_defaults(self):
        (self.root / "notes.md").write_bytes(b"First line\r\nSecond line\r\nThird line\n")
        native_open = Path.open

        def windows_open(path, mode="r", buffering=-1, encoding=None, errors=None, newline=None):
            if "b" not in mode and "w" in mode and newline is None:
                newline = "\r\n"
            return native_open(path, mode, buffering, encoding, errors, newline)

        # Linux read_text() would hide this Windows-only output bug. Simulate
        # native newline translation, then inspect actual bytes in every output.
        with mock.patch.object(Path, "open", windows_open):
            _, data, _ = self.prepare()
        self.assertEqual(data["notes"], "First line\nSecond line\nThird line\n")
        for name in ("release-notes.md", "folderstorm-release.json", "folderstorm-releases.json"):
            raw = (self.root / "out" / name).read_bytes()
            self.assertNotIn(b"\r", raw, name)
            self.assertTrue(raw.endswith(b"\n"), name)
        body = (self.root / "out/release-notes.md").read_bytes()
        self.assertIn(b"<!-- folderstorm-release-v1\n", body)
        self.assertIn(b"\n-->\n", body)

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

    def test_verified_matching_velopack_package(self):
        asset=self.data["assets"][0];asset["installer"]="velopack";asset["velopack_feed"]="releases.win.json"
        identity="Folderstorm.opensim.nightly.x86_64.avx2"
        package=self.root / (identity+"-7.2.5-82125-full.nupkg")
        with zipfile.ZipFile(package,"w") as archive:
            archive.writestr("lib/app/folderstorm-bundle.json",json.dumps({"schema":1,"protocol":16,"version":self.data["version"],"commit":self.data["commit"],"app_id":identity,"installer":"velopack"}))
            archive.writestr("lib/app/folderstorm-session-viewer.txt","Folderstorm.exe\n")
            for name in ("Folderstorm.exe","folderstorm-session-host.exe","fs-mcp.exe","folderstorm-update.exe","migrate-settings.exe","fs-mcp-launcher-maintenance.exe"):
                archive.writestr("lib/app/"+name,b"fixture")
        raw=package.read_bytes()
        catalog={"Assets":[{"PackageId":identity,"Version":"7.2.5-82125","Type":"Full","FileName":package.name,"Size":len(raw),"SHA1":hashlib.sha1(raw).hexdigest(),"SHA256":hashlib.sha256(raw).hexdigest()}]}
        feed=self.root / asset["velopack_feed"];feed.write_text(json.dumps(catalog))
        _,data,paths=self.prepare()
        self.assertEqual(package.name,data["assets"][0]["update"]["file"])
        self.assertIn(package,paths)
        package.write_bytes(raw+b"changed")
        with self.assertRaises(ValueError):self.prepare()
        package.write_bytes(raw);catalog["Assets"][0]["Version"]="7.2.5-82126";feed.write_text(json.dumps(catalog))
        with self.assertRaises(ValueError):self.prepare()
        catalog["Assets"][0]["Version"]="7.2.5-82125"
        for missing in ("lib/app/Folderstorm.exe","lib/app/folderstorm-session-host.exe"):
            package.write_bytes(raw)
            with zipfile.ZipFile(package) as archive:
                entries={item.filename:archive.read(item) for item in archive.infolist() if item.filename!=missing}
            with zipfile.ZipFile(package,"w") as archive:
                for filename,contents in entries.items():archive.writestr(filename,contents)
            changed=package.read_bytes();catalog["Assets"][0].update(Size=len(changed),SHA1=hashlib.sha1(changed).hexdigest(),SHA256=hashlib.sha256(changed).hexdigest())
            feed.write_text(json.dumps(catalog))
            with self.assertRaises(ValueError):self.prepare()

    def test_full_history_with_maximum_changelogs_fits_bounded_pages(self):
        (self.root / "notes.md").write_text("x"*release.MAX_NOTES)
        native,legacy=[],[]
        prior=None
        for number in range(32):
            self.data["version"]=f"7.2.5.{number+1}"
            _,data,_=self.prepare(previous=prior)
            prior=self.root/"out/folderstorm-releases.json"
            body=(self.root/"out/release-notes.md").read_text()
            base={"draft":False,"prerelease":True,"html_url":data["url"],"assets":[]}
            native.append({**base,"body":body})
            old_body=data["notes"]+"\n\n<!-- folderstorm-release-v1\n"+json.dumps(data,separators=(",",":"))+"\n-->\n"
            legacy.append({**base,"body":old_body})
        self.assertEqual(32,len(json.loads(prior.read_text())["releases"]))
        self.assertLessEqual(prior.stat().st_size,release.MAX_FEED_BYTES)
        # The reviewer reproduction: the old, publisher-accepted 32-row response
        # exceeds the cap. Pages work for both existing and new release bodies.
        self.assertGreater(len(json.dumps(legacy).encode()),release.MAX_FEED_BYTES)
        for history in (legacy,native):
            for offset in range(0,32,8):
                self.assertLessEqual(len(json.dumps(history[offset:offset+8]).encode()),release.MAX_FEED_BYTES)
        for entry in native:
            metadata=json.loads(entry["body"].split("<!-- folderstorm-release-v1\n")[1].split("\n-->")[0])
            self.assertNotIn("notes",metadata)

    def test_oversized_static_history_is_refused_before_writing(self):
        (self.root/"notes.md").write_text('"'*release.MAX_NOTES)
        prior=None
        for number in range(31):
            self.data["version"]=f"7.2.5.{number+1}"
            self.prepare(previous=prior)
            prior=self.root/"out/folderstorm-releases.json"
        previous=prior.read_bytes()
        self.data["version"]="7.2.5.32"
        with self.assertRaisesRegex(ValueError,"static feed exceeds"):
            self.prepare(previous=prior)
        self.assertEqual(prior.read_bytes(),previous)


if __name__ == "__main__": unittest.main()
