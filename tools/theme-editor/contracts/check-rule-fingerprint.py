"""Check the production CMake fingerprint against an independent SHA-256 reader."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cmake', required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('Use a new evidence directory')
args.output.mkdir(parents=True)
rules = (args.source / 'tools/theme-editor/contracts/xui-edit-rules.json').read_bytes().replace(b'\r\n', b'\n')
expected = hashlib.sha256(rules).hexdigest()
helper = (args.source / 'tools/theme-editor/native/ThemeRules.cmake').resolve().as_posix()
script = [f'include("{helper}")']
for name, data in [('lf', rules), ('crlf', rules.replace(b'\n', b'\r\n'))]:
    path = (args.output / f'rules-{name}.json').resolve()
    path.write_bytes(data)
    script += [f'folderstorm_theme_rules("{path.as_posix()}")',
               f'if(NOT THEME_RULES_SHA256 STREQUAL "{expected}")',
               f'  message(FATAL_ERROR "{name} fingerprint differs")', 'endif()',
               f'file(WRITE "{(args.output / (name + ".sha256")).resolve().as_posix()}" "${{THEME_RULES_SHA256}}")']
check = args.output / 'check.cmake'
check.write_text('\n'.join(script) + '\n', encoding='utf-8')
process = subprocess.run([args.cmake, '-P', str(check)], capture_output=True, encoding='utf-8', timeout=30)
(args.output / 'cmake.log').write_text(process.stdout + process.stderr, encoding='utf-8')
result = {'exit_code': process.returncode, 'expected': expected,
          'lf': (args.output / 'lf.sha256').read_text() if process.returncode == 0 else None,
          'crlf': (args.output / 'crlf.sha256').read_text() if process.returncode == 0 else None}
(args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result))
raise SystemExit(process.returncode)
