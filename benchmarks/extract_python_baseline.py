#!/usr/bin/env python3
"""Extract the historical Python benchmark into a new scratch directory.

Reads Git objects without checking out files or executing setup.py. This is a
benchmark-only copy; none of the old business modules are installed again.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ref', default='172b78c')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]

def git(*arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments])

commit = git('rev-parse', '--verify', '--end-of-options', args.ref + '^{commit}').decode().strip()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
manifest = {'base_commit': commit, 'sha256': {}}
for package in ('chaos_assets', 'chaos_runtime'):
    prefix = f'src/{package}'
    paths = git('ls-tree', '-r', '--name-only', commit, '--', f'{prefix}/{package}').decode().splitlines()
    paths = [path for path in paths if path.endswith('.py')] + [f'{prefix}/setup.py']
    for path in paths:
        content = git('show', f'{commit}:{path}')
        destination = root / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)
        manifest['sha256'][path] = hashlib.sha256(content).hexdigest()
    setup = ast.parse((root / prefix / 'setup.py').read_text())
    # Only read the literal entry-point mapping. Never execute historical
    # installation hooks merely to reconstruct plugin-discovery metadata.
    mappings = [ast.literal_eval(keyword.value)
                for call in ast.walk(setup) if isinstance(call, ast.Call)
                for keyword in call.keywords if keyword.arg == 'entry_points']
    if len(mappings) != 1:
        raise RuntimeError(f'{package}: expected one literal entry_points mapping')
    metadata = root / 'metadata' / f'{package}-0.1.0.dist-info'
    metadata.mkdir(parents=True)
    metadata.joinpath('METADATA').write_text(f'Metadata-Version: 2.1\nName: {package}\nVersion: 0.1.0\n')
    metadata.joinpath('entry_points.txt').write_text('\n'.join(
        f'[{group}]\n' + '\n'.join(entries) + '\n' for group, entries in mappings[0].items()))
launcher = root / 'robot_runtime'
launcher.write_text('''#!/usr/bin/python3
"""Frozen Python runtime for mock benchmark comparisons only."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parent
sys.path[:0] = [str(root/'metadata'), str(root/'src/chaos_runtime'), str(root/'src/chaos_assets')]
from chaos_runtime.node import main
main()
''')
launcher.chmod(0o755)
root.joinpath('manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(launcher)
