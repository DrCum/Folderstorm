"""Replay shared manifest and mutated package boundaries through both validators.

Pass the proof-cases.json from the Gate A sample generator; use a fresh output
directory to preserve each test run's package bytes and result receipt.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--base', type=Path, help='Installed/retained base used to export the proof packages; defaults to source')
parser.add_argument('--dotnet', required=True)
parser.add_argument('--proof-cases', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.resolve()
base = (args.base or args.source).resolve()
native_base = base / 'indra/newview' if (base / 'indra/newview/skins').is_dir() else base
output = args.output.resolve()
if output.exists():
    parser.error('Use a new evidence directory')
output.mkdir(parents=True)
cli = source / 'tools/theme-editor/Folderstorm.Theme.Cli/bin/Release/net10.0/Folderstorm.Theme.Cli.dll'
native = source / 'tools/theme-editor/artifacts/native/Release/theme_validate.exe'
valid = json.loads((source / 'tools/theme-editor/contracts/fixtures/valid.json').read_text(encoding='utf-8'))
results = []


def run(path, mode, expected):
    entry = {'case': path.name, 'mode': mode, 'expected': expected}
    for name, command in [('managed', [args.dotnet, str(cli), 'manifest' if mode == 'manifest' else 'validate', str(path)]),
                          ('native', [str(native), mode, str(path)])]:
        if mode == 'revision':
            command += ['--base', str(base)] if name == 'managed' else [str(native_base)]
        process = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=180)
        if not process.returncode:
            result = 'valid'
        elif name == 'managed':
            try:
                result = json.loads(process.stderr)['error']
            except (ValueError, KeyError):
                result = 'unhandled: ' + process.stderr[:300]
        else:
            result = process.stderr.strip().split(':', 1)[0]
        entry[name] = result
    entry['passed'] = entry['managed'] == entry['native'] == expected
    results.append(entry)
    print(json.dumps(entry), flush=True)


for depth in [20, 40, 60, 65]:
    manifest = copy.deepcopy(valid)
    nested = 1
    for unused in range(depth):
        nested = {'x': nested}
    manifest['extensions'] = {'org.folderstorm.fixture': nested}
    path = output / f'depth-{depth}.json'
    path.write_text(json.dumps(manifest), encoding='utf-8')
    run(path, 'manifest', 'valid' if depth <= 60 else 'json')
manifest = copy.deepcopy(valid)
manifest['font_dependencies'] = [None]
path = output / 'null-font-entry.json'
path.write_text(json.dumps(manifest), encoding='utf-8')
run(path, 'manifest', 'schema')
manifest = copy.deepcopy(valid)
manifest['format_version'] = 2
manifest['files'][0]['unknown_structural_field'] = True
path = output / 'nested-shape-before-version.json'
path.write_text(json.dumps(manifest), encoding='utf-8')
run(path, 'manifest', 'schema')

proofs = json.loads(args.proof_cases.read_text(encoding='utf-8'))
combined = Path(next(proof['library_revision'] for proof in proofs if proof['name'] == 'combined'))
mutations = [
    ('nonfinite-font-size', 'viewer/fonts/fonts.xml', 'font_size', 'size', 'NaN', 'font'),
    ('nonfinite-xui-width', 'viewer/xui/en/widgets/floater.xml', None, 'width', 'NaN', 'xui_value'),
    ('unknown-xui-color', 'viewer/xui/en/widgets/floater.xml', None, 'bg_alpha_color', 'MissingThemeColor', 'reference'),
    ('unknown-xui-image-overlay', 'viewer/xui/en/widgets/floater.xml', None, 'bg_alpha_image_overlay', 'MissingOverlayColor', 'reference'),
    ('valid-xui-image-overlay', 'viewer/xui/en/widgets/floater.xml', None, 'bg_alpha_image_overlay', 'ThemeAccent', 'valid'),
    ('valid-pressed-image', 'viewer/xui/en/widgets/button.xml', None, 'image_pressed', 'PushButton_Press', 'valid'),
    ('unknown-pressed-image', 'viewer/xui/en/widgets/button.xml', None, 'image_pressed', 'MissingPressedImage', 'reference'),
    ('valid-pressed-selected-image', 'viewer/xui/en/widgets/button.xml', None, 'image_pressed_selected', 'PushButton_Selected', 'valid'),
    ('unknown-pressed-selected-image', 'viewer/xui/en/widgets/button.xml', None, 'image_pressed_selected', 'MissingPressedSelectedImage', 'reference'),
    ('reversed-nine-slice', 'viewer/textures/textures.xml', 'texture', 'scale.left', '31', 'texture'),
]
for name, relative, element, attribute, value, expected in mutations:
    directory = output / name
    shutil.copytree(combined, directory)
    path = directory / relative
    tree = ET.parse(path)
    target = tree.getroot() if element is None else tree.getroot().find(element)
    target.set(attribute, value)
    tree.write(path, encoding='utf-8', xml_declaration=True)
    manifest_path = directory / 'theme.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    payload = next(file for file in manifest['files'] if file['path'] == relative)
    payload['size'] = path.stat().st_size
    payload['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    run(directory, 'revision', expected)

(output / 'findings.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
print(f"Boundary evidence saved; {sum(not item['passed'] for item in results)} findings need implementation work. No native visual acceptance is claimed.")
