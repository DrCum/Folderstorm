"""Regenerate shared manifest fixtures without a .NET/native dependency."""
import copy
import json
from pathlib import Path

root = Path(__file__).with_name("fixtures")
root.mkdir(exist_ok=True)
base = {
    "format_version": 1,
    "theme_id": "cd2e719d-3242-41e8-9eef-1cfd448f498d",
    "package_version": "1.0.0",
    "display_name": "Violet test",
    "base_skin": "ansastorm",
    "base_scheme": "folderstorm",
    "adapter_id": "ansastorm-classic",
    "adapter_version": 1,
    "required_capabilities": ["viewer.overlay.v1"],
    "base_fingerprint": "0" * 64,
    "files": [{"path": "base-dependencies.json", "size": 0, "sha256": "0" * 64, "kind": "dependencies"}],
    "font_dependencies": [],
}
cases = []


def case(name, expected="valid", change=None, raw=None):
    value = copy.deepcopy(base)
    if change:
        change(value)
    content = raw if raw is not None else json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    (root / (name + ".json")).write_text(content, encoding="utf-8", newline="\n")
    cases.append({"name": name, "file": name + ".json", "expected": expected})


def field(name, value):
    return lambda document: document.update({name: value})


case("valid")
case("unicode-name", change=field("display_name", "Violet 日本語 🦊"))
case("metadata-version-hyphen", change=field("package_version", "1.0.0+build-alpha"))
case("version-prerelease", change=field("package_version", "2.1.0-beta.12+private"))
case("empty-base-scheme", change=field("base_scheme", ""))
case("unknown-format", "format_version", field("format_version", 2))
case("format-string", "schema", field("format_version", "1"))
case("uppercase-identity", "identity", field("theme_id", base["theme_id"].upper()))
case("numeric-prerelease-zero", "identity", field("package_version", "1.0.0-beta.01"))
case("new-capability", "capability", field("required_capabilities", ["viewer.unavailable.v2"]))
case("duplicate-capability", "capability", field("required_capabilities", ["viewer.overlay.v1"] * 2))
case("unsupported-adapter", "base", field("adapter_version", 2))
case("absolute-scheme", "path", field("base_scheme", "C:/escape"))
case("null-display", "schema", field("display_name", None))
case("oversized-display", "identity", field("display_name", "x" * 129))
case("control-display", "identity", field("display_name", "bad\nname"))
case("c1-control-display", "identity", field("display_name", "bad\u0085name"))
case("unicode-author", change=field("author", "狐" * 1024))
case("unknown-structural-field", "schema", field("run_script", "ignored"))
case("invalid-hash", "schema", field("base_fingerprint", "A" * 64))
case("duplicate-key", "duplicate_key", raw=json.dumps(base)[:-1] + ',"format_version":1}')
case("escaped-duplicate-key", "duplicate_key", raw=json.dumps(base)[:-1] + ',"format_vers\\u0069on":1}')
case("malformed-json", "json", raw="{broken")
for name, path in [("traversal", "../escape.txt"), ("drive", "C:/escape.txt"), ("stream", "licenses/a:stream.txt"),
                   ("device", "licenses/CON.txt"), ("superscript-device", "licenses/COM¹.txt"),
                   ("trailing-space", "licenses/name.txt "), ("backslash", "licenses\\name.txt")]:
    case(name, "path", lambda document, path=path: document["files"][0].update(path=path))
case("unlisted-kind", "kind", lambda document: document["files"][0].update(path="scripts/run.exe", kind="license"))
case("negative-size", "size", lambda document: document["files"][0].update(size=-1))


def collision(document):
    for path in ["viewer/textures/widgets/A.png", "viewer/textures/widgets/a.png"]:
        document["files"].append({"path": path, "size": 0, "sha256": "0" * 64, "kind": "image"})


case("case-collision", "collision", collision)
for depth in [20, 40, 60, 65]:
    nested = 1
    for unused in range(depth):
        nested = {"x": nested}
    case(f"metadata-depth-{depth}", "valid" if depth <= 60 else "json", field("extensions", {"org.folderstorm.fixture": nested}))
case("null-font-entry", "schema", field("font_dependencies", [None]))
case("missing-payload-size", "schema", lambda document: document["files"][0].pop("size"))
case("shape-before-version", "schema", lambda document: (document.update(format_version=2), document["files"][0].update(unknown=True)))
(root / "cases.json").write_text(json.dumps(cases, indent=2) + "\n", encoding="utf-8", newline="\n")
print(f"Wrote {len(cases)} shared manifest fixtures")
