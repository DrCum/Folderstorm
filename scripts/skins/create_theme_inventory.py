#!/usr/bin/env python3
"""Write a v2 classic-theme source catalog and byte fingerprints to a new file.

Developer tooling only. No Pillow or .NET dependency. The viewer's resolver is
authoritative for locale, CURRENT_SKIN, fonts and personal overrides.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path)
    parser.add_argument("output", type=Path, help="New output path; existing evidence is never overwritten")
    arguments = parser.parse_args()
    root = arguments.repository.resolve()
    output = arguments.output.resolve()
    if output.exists():
        parser.error("Output already exists; choose a new evidence file")
    viewer = root / "indra/newview"
    skins = viewer / "skins"
    hashes = {}

    def digest(path):
        if path not in hashes:
            hashes[path] = hashlib.sha256(path.read_bytes()).hexdigest()
        return hashes[path]

    def fields(map_node):
        nodes = list(map_node)
        return {nodes[i].text: nodes[i + 1] for i in range(0, len(nodes), 2)}

    catalog = []
    skin_entry = next(fields(node) for node in ET.parse(skins / "skins.xml").getroot().find("array").findall("map") if fields(node)["folder"].text == "ansastorm")
    schemes = [{key: value.text or "" for key, value in fields(node).items()} for node in skin_entry["themes"].findall("map")]
    for scheme in schemes:
        relative_roots = ["skins/default", "skins/ansastorm"]
        if scheme["folder"]:
            relative_roots.append("skins/ansastorm/themes/" + scheme["folder"])
        layers = [viewer / name for name in relative_roots]
        colors, textures, logical = {}, {}, set()
        for relative_root, layer in zip(relative_roots, layers):
            color_file = layer / "colors.xml"
            if color_file.is_file():
                for node in ET.parse(color_file).getroot():
                    if node.tag == "color" and node.get("name"):
                        colors[node.get("name")] = {"attributes": dict(node.attrib), "source": relative_root + "/colors.xml"}
            texture_file = layer / "textures/textures.xml"
            if texture_file.is_file():
                for node in ET.parse(texture_file).getroot():
                    if node.get("name"):
                        textures[node.get("name")] = {"attributes": dict(node.attrib), "source": relative_root + "/textures/textures.xml"}
            for path in layer.rglob("*"):
                if path.is_file():
                    relative = path.relative_to(layer).as_posix()
                    if relative == "colors.xml" or relative.startswith(("textures/", "xui/en/")):
                        logical.add(relative)
        dependencies = []

        def dependency(relative, logical_id, index):
            path = viewer / relative
            present = path.is_file()
            dependencies.append({"path": relative, "present": present, "size": path.stat().st_size if present else 0,
                                 "sha256": digest(path) if present else "", "logical_id": logical_id, "layer": index})

        for name in sorted(logical):
            for index, relative_root in enumerate(relative_roots):
                dependency(relative_root + "/" + name, name, index)
        for path in sorted((viewer / "fonts").rglob("*")):
            if path.is_file():
                dependency(path.relative_to(viewer).as_posix(), "fonts/" + path.name, len(layers))
        for texture in textures.values():
            filename = texture["attributes"].get("file_name", "")
            found = [(layer / "textures" / filename) for layer in layers if filename and (layer / "textures" / filename).is_file()]
            if found:
                texture["resolved_source"] = found[-1].relative_to(viewer).as_posix()
                texture["sha256"] = digest(found[-1])
                with found[-1].open("rb") as stream:
                    header = stream.read(24)
                if header[:8] == b"\x89PNG\r\n\x1a\n" and len(header) == 24:
                    texture["pixel_size"] = list(struct.unpack(">II", header[16:24]))
                else:
                    texture["dimensions_require_native_decoder"] = True
            else:
                texture["resolution_requires_native_check"] = True
        catalog.append({"scheme": scheme, "roots": relative_roots, "colors": colors, "textures": textures,
                        "base_dependencies": {"format_version": 1, "base_skin": "ansastorm", "base_scheme": scheme["folder"],
                                              "roots": relative_roots, "files": sorted(dependencies, key=lambda item: item["path"])}})
    source_names = ["scripts/skins/generate_folderstorm_themes.py", "indra/llfilesystem/lldir.cpp", "indra/llui/lluicolortable.cpp",
                    "indra/llui/lluictrlfactory.cpp", "indra/llrender/llfontgl.cpp", "indra/llrender/llfontregistry.cpp",
                    "indra/newview/llappviewer.cpp", "indra/newview/llviewertexturelist.cpp", "indra/newview/viewer_manifest.py",
                    "indra/newview/fsthemepackage.cpp", "indra/newview/fsthemeviewer.cpp", "tools/session-host/theme.h"]
    inventory = {"inventory_version": 2, "source_commit": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
                 "source_had_uncommitted_changes": bool(subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"])),
                 "base_skin": "ansastorm", "starting_scheme": "folderstorm", "locale": "en",
                 "resolution_note": "Static developer catalog; native resource resolution and visual checks remain required.",
                 "schemes": catalog, "source_sha256": {name: digest(root / name) for name in source_names if (root / name).is_file()}}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(inventory, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({"output": str(output), "inventory_version": 2, "schemes": len(catalog), "hashed_files": len(hashes)}))


if __name__ == "__main__":
    main()
