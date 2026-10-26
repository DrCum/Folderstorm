"""Prepare reproducible native proof packages; does not launch or select a viewer."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import uuid
import xml.etree.ElementTree as ET
import zlib

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--base', type=Path, required=True)
parser.add_argument('--dotnet', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.resolve()
base = args.base.resolve()
viewer = base / 'indra/newview' if (base / 'indra/newview/skins').is_dir() else base
output = args.output.resolve()
if output.exists():
    parser.error('Choose a new output directory; completed proof sets are preserved')
output.mkdir(parents=True)
cli = source / 'tools/theme-editor/Folderstorm.Theme.Cli/bin/Release/net10.0/Folderstorm.Theme.Cli.dll'


def command(*parts):
    result = subprocess.run([args.dotnet, str(cli), *map(str, parts), '--base', str(base)], text=True, encoding='utf-8', capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{parts[0]} failed: {result.stderr.strip()}")
    return json.loads(result.stdout)


catalog = command('catalog')
textures = {texture['name']: texture for texture in catalog['textures']}
baseline_path = output / 'projects/noop'
command('create', baseline_path, 'Gate A baseline')
baseline = json.loads((baseline_path / 'project.fsthemeproj').read_text(encoding='utf-8'))


def png(width, height, center, asymmetric=False):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)
        for x in range(width):
            color = center
            if asymmetric:
                if x < 3 and y < 3:
                    color = (255, 35, 70, 255)
                elif x >= width - 6 and y < 3:
                    color = (40, 220, 255, 255)
                elif x < 3 and y >= height - 7:
                    color = (90, 255, 100, 255)
                elif x >= width - 6 and y >= height - 7:
                    color = (255, 190, 30, 255)
            scanlines.extend(color)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(scanlines), 9)) + chunk(b'IEND', b'')


def asset(directory, project, payload, kind, data, original=None):
    extension = Path(payload).suffix
    original = data if original is None else original
    relative = 'assets/' + hashlib.sha256(data).hexdigest() + extension
    original_relative = 'assets/' + hashlib.sha256(original).hexdigest() + extension
    for name, content in [(relative, data), (original_relative, original)]:
        path = directory / name
        path.parent.mkdir(exist_ok=True, parents=True)
        path.write_bytes(content)
    project['edits']['assets'] = [item for item in project['edits']['assets'] if item['payload_path'] != payload]
    project['edits']['assets'].append({'payload_path': payload, 'asset_path': relative, 'original_path': original_relative, 'kind': kind})


def write_xml(node):
    return ET.tostring(node, encoding='utf-8', xml_declaration=True) + b'\n'


def inherited_bytes(relative):
    candidates = [viewer / layer / relative for layer in catalog['layers']]
    return next(path.read_bytes() for path in reversed(candidates) if path.is_file())


def button_assets(directory, project, asymmetric):
    registrations = ET.Element('textures', version='101')
    states = [('PushButton_Off', (40, 145, 95, 255)), ('PushButton_Over', (80, 220, 145, 255)), ('PushButton_Press', (235, 130, 45, 255))]
    for name, color in states if not asymmetric else states[:1]:
        texture = textures[name]
        payload = 'viewer/textures/' + texture['attributes']['file_name']
        asset(directory, project, payload, 'image', png(32, 24, color, asymmetric), (viewer / texture['source']).read_bytes())
        attributes = dict(texture['attributes'])
        attributes.update({'scale.left': '3' if asymmetric else '4', 'scale.top': '21' if asymmetric else '20', 'scale.right': '26' if asymmetric else '28', 'scale.bottom': '7' if asymmetric else '4'})
        ET.SubElement(registrations, 'texture', attributes)
    asset(directory, project, 'viewer/textures/textures.xml', 'textures', write_xml(registrations))
    project['edits']['colors']['ButtonImageColor'] = '1 1 1 1'
    if not asymmetric:
        # Texture names alone do not change the inherited widget's state bindings.
        original = inherited_bytes('xui/en/widgets/button.xml')
        button = ET.fromstring(original)
        button.set('image_hover_unselected', 'PushButton_Over')
        button.set('image_pressed', 'PushButton_Press')
        asset(directory, project, 'viewer/xui/en/widgets/button.xml', 'xui', write_xml(button), original)


def floater_asset(directory, project):
    original = inherited_bytes('xui/en/widgets/floater.xml')
    floater = ET.fromstring(original)
    floater.set('bg_opaque_color', 'ThemeAccent')
    floater.set('bg_alpha_color', 'ThemeFloaterAlphaTint')
    floater.set('bg_opaque_image_overlay', 'ThemeAccent')
    floater.set('bg_alpha_image_overlay', 'ThemeFloaterAlphaTint')
    project['edits']['colors']['ThemeFloaterAlphaTint'] = '0.2 0.8 0.5 0.66'
    asset(directory, project, 'viewer/xui/en/widgets/floater.xml', 'xui', write_xml(floater), original)


def font_assets(directory, project):
    filename = 'GateACustomMono.ttf'
    asset(directory, project, 'viewer/fonts/' + filename, 'font', (viewer / 'fonts/CascadiaCode-Light.ttf').read_bytes())
    original = (viewer / 'fonts/fonts.xml').read_bytes()
    configuration = ET.fromstring(original)
    for font in configuration.findall('font'):
        if font.get('name') in ('default', 'SansSerif'):
            first = font.find('file')
            if first is not None:
                first.attrib.clear()
                first.text = filename
    asset(directory, project, 'viewer/fonts/fonts.xml', 'font_config', write_xml(configuration), original)
    asset(directory, project, 'licenses/CascadiaCodeLICENSE.txt', 'license', (viewer / 'fonts/CascadiaCodeLICENSE.txt').read_bytes())


results = []
for name in ['noop', 'accent', 'button-states', 'asymmetric-slice', 'floater-tint', 'embedded-font', 'combined']:
    directory = output / 'projects' / name
    directory.mkdir(parents=True, exist_ok=True)
    project = copy.deepcopy(baseline)
    project['project_id'] = str(uuid.uuid4())
    project['theme_id'] = str(uuid.uuid4())
    project['display_name'] = 'Gate A ' + name
    if name in ('accent', 'combined'):
        project['edits']['colors']['ThemeAccent'] = '0.2 0.8 0.5 1'
    if name in ('button-states', 'combined'):
        button_assets(directory, project, False)
    if name == 'asymmetric-slice':
        button_assets(directory, project, True)
    if name in ('floater-tint', 'combined'):
        floater_asset(directory, project)
    if name in ('embedded-font', 'combined'):
        font_assets(directory, project)
    (directory / 'project.fsthemeproj').write_text(json.dumps(project, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    package = output / (name + '.fstheme')
    exported = command('export', directory, package)
    published = command('publish', package, output / 'library')
    results.append({'name': name, 'project': str(directory), 'package': str(package), 'revision': exported['revision'],
                    'library_revision': published['library_revision'], 'native_visual_validation': 'pending', 'expected': {
                        'accent': 'mint aliases and preserved alpha variants', 'button-states': 'dark green normal, bright green hover, orange press',
                        'asymmetric-slice': 'red/cyan top corners and green/amber bottom corners; 3/6 left/right and 3/7 top/bottom margins',
                        'floater-tint': 'complete inherited floater template with green alpha tint', 'embedded-font': 'package-local GateACustomMono.ttf with built-in/OS fallbacks',
                        'combined': 'all supported overrides together', 'noop': 'identical effective appearance to unmodified AnsaStorm Violet'
                    }[name]})
    print('PREPARED', name, exported['revision'], flush=True)
(output / 'proof-cases.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8', newline='\n')
print('No viewer was launched or selected. Native visual validation remains pending.')
