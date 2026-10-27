"""Replay XUI presence and UTF-8 boundaries through the production validators.

The seed must be a valid exported revision containing floater_test_widgets.xml.
Each case retains its exact bytes and a result receipt in a fresh directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--revision', type=Path, required=True)
parser.add_argument('--base', type=Path, required=True)
parser.add_argument('--dotnet', required=True)
parser.add_argument('--cli', type=Path, required=True)
parser.add_argument('--native', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('Use a new evidence directory')
args.output.mkdir(parents=True)
relative = 'viewer/xui/en/floater_test_widgets.xml'


def attribute(name, value):
    def change(root):
        button = root.find("button[@name='test_button']")
        if value is None:
            button.attrib.pop(name)
        else:
            button.set(name, value)
    return change


def text(value):
    def change(root):
        root.find('text').text = value
    return change


cases = [
    ('seed', lambda root: None, 'valid'),
    ('remove-geometry', attribute('width', None), 'valid'),
    ('empty-label', attribute('label', ''), 'valid'),
    ('empty-protected-attribute', attribute('commit_callback', ''), 'xui_attribute'),
    ('remove-protected-name', attribute('name', None), 'xui_attribute'),
    ('attribute-ascii-4096', attribute('label', 'A' * 4096), 'valid'),
    ('attribute-ascii-4097', attribute('label', 'A' * 4097), 'xui_value'),
    ('attribute-cjk-4095', attribute('label', '中' * 1365), 'valid'),
    ('attribute-cjk-4098', attribute('label', '中' * 1366), 'xui_value'),
    ('text-ascii-4096', text('A' * 4096), 'valid'),
    ('text-ascii-4097', text('A' * 4097), 'xui_value'),
    ('text-cjk-4095', text('中' * 1365), 'valid'),
    ('text-cjk-4098', text('中' * 1366), 'xui_value'),
    ('empty-text', text(''), 'valid'),
    ('protected-content', lambda root: setattr(root, 'text', 'Changed protected content'), 'xui_structure'),
]
results = []
for name, mutate, expected in cases:
    directory = args.output / name
    shutil.copytree(args.revision, directory)
    path = directory / relative
    tree = ET.parse(path)
    mutate(tree.getroot())
    tree.write(path, encoding='utf-8', xml_declaration=True)
    manifest_path = directory / 'theme.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    payload = next(item for item in manifest['files'] if item['path'] == relative)
    payload.update(size=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    entry = {'case': name, 'expected': expected, 'payload_sha256': payload['sha256']}
    commands = {
        'managed': [args.dotnet, str(args.cli), 'validate', str(directory), '--base', str(args.base)],
        'native': [str(args.native), 'revision', str(directory), str(args.base)],
    }
    for validator, command in commands.items():
        process = subprocess.run(command, capture_output=True, encoding='utf-8', timeout=90)
        (directory.parent / f'{name}-{validator}.log').write_text(process.stdout + process.stderr, encoding='utf-8')
        if process.returncode == 0:
            result = 'valid'
        elif validator == 'managed':
            try:
                result = json.loads(process.stderr)['error']
            except (ValueError, KeyError):
                result = 'unhandled'
        else:
            result = process.stderr.strip().split(':', 1)[0]
        entry[validator] = result
    entry['passed'] = entry['managed'] == entry['native'] == expected
    results.append(entry)
    print(json.dumps(entry), flush=True)
(args.output / 'findings.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
failures = sum(not result['passed'] for result in results)
print(f'{len(results) - failures}/{len(results)} passed. Validator parity only; no native appearance acceptance.')
sys.exit(1 if failures else 0)
