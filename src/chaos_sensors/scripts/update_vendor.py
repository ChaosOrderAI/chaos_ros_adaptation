#!/usr/bin/env python3
"""Move an in-tree vendored driver to a new upstream ref, re-applying patches.

    update_vendor.py --check
    update_vendor.py livox_ros_driver2 --ref <commit>

Only ``mode: in-tree`` drivers can be updated automatically. apt drivers are
upgraded through the system package manager and pinned in the manifest;
pinned-import drivers are re-imported with ``vcs import`` inside their own
workspace (see vendor/orbbec/README.md).
"""
import argparse
import io
import json
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

import yaml

PACKAGE_ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = PACKAGE_ROOT / 'vendor' / 'manifest.yaml'
PATCH_ROOT = PACKAGE_ROOT / 'vendor' / 'patches'


def github_archive(upstream, ref):
    url = upstream.removesuffix('.git').rstrip('/')
    if not url.startswith('https://github.com/'):
        raise SystemExit(f'only GitHub upstreams are supported, got {upstream}')
    owner_repo = url[len('https://github.com/'):]
    return f'https://codeload.github.com/{owner_repo}/tar.gz/{ref}'


def fetch_archive(url):
    print(f'fetching {url}')
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()
    return tarfile.open(fileobj=io.BytesIO(data), mode='r:gz')


def replace_tree(archive, destination):
    temp = pathlib.Path(tempfile.mkdtemp(prefix='chaos-vendor-'))
    archive.extractall(temp)
    entries = list(temp.iterdir())
    if len(entries) != 1 or not entries[0].is_dir():
        raise SystemExit(f'unexpected archive layout in {temp}')
    source = entries[0]
    backup = destination.with_name(destination.name + '.prev')
    if backup.exists():
        shutil.rmtree(backup)
    if destination.exists():
        destination.rename(backup)
    shutil.copytree(source, destination)
    shutil.rmtree(temp)
    print(f'replaced {destination} (previous tree kept in {backup})')


def apply_patches(name, destination, patches):
    if not patches:
        return True
    directory = PATCH_ROOT / name
    for patch in patches:
        path = directory / patch
        if not path.is_file():
            raise SystemExit(f'missing patch {path}')
        result = subprocess.run(['patch', '-p1', '-d', str(destination), '-i', str(path)])
        if result.returncode != 0:
            print(f'patch {path} FAILED; resolve manually in {destination}')
            return False
    return True


def record_provenance(destination, name, spec, ref):
    (destination / 'VENDORED.json').write_text(json.dumps({
        'name': name,
        'upstream': spec['upstream'],
        'ref': ref,
        'license': spec.get('license', ''),
        'patches': spec.get('patches', []),
    }, indent=2) + '\n')


def update_manifest_ref(name, ref):
    lines = MANIFEST.read_text().splitlines()
    in_driver = False
    for index, line in enumerate(lines):
        if line.startswith('  ') and not line.startswith('    ') and line.rstrip().endswith(':'):
            in_driver = line.strip() == f'{name}:'
        elif in_driver and line.strip().startswith('ref:'):
            lines[index] = f'    ref: {ref}'
            MANIFEST.write_text('\n'.join(lines) + '\n')
            return True
    return False


def do_update(name, ref):
    manifest = yaml.safe_load(MANIFEST.read_text())
    spec = manifest['drivers'].get(name)
    if spec is None:
        raise SystemExit(f'unknown driver {name}')
    if spec.get('mode') != 'in-tree':
        raise SystemExit(
            f'{name} is mode {spec.get("mode")}; update it through the package manager '
            f'or its own workspace, then pin the new version in the manifest')
    destination = PACKAGE_ROOT / spec['path']
    if not destination.is_dir():
        raise SystemExit(f'missing tree at {spec["path"]}')
    archive = fetch_archive(github_archive(spec['upstream'], ref))
    replace_tree(archive, destination)
    ok = apply_patches(name, destination, spec.get('patches', []))
    record_provenance(destination, name, spec, ref)
    update_manifest_ref(name, ref)
    print('next steps:')
    print(f'  colcon build --symlink-install --packages-select {name}')
    print('  run the sensor smoke tests, then git add -A and review the diff')
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('name', nargs='?')
    parser.add_argument('--ref')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check or not args.name:
        return subprocess.call([sys.executable, str(PACKAGE_ROOT / 'scripts' / 'check_vendor.py')])
    if not args.ref:
        raise SystemExit('--ref is required to update a driver')
    return do_update(args.name, args.ref)


if __name__ == '__main__':
    sys.exit(main())
