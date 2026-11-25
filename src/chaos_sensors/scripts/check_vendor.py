#!/usr/bin/env python3
"""Verify every vendored / apt sensor driver against vendor/manifest.yaml.

Exit code 0 when everything matches, 1 on a mismatch. A pinned-import driver
whose source has not been fetched is reported as a warning, not a failure:
the source is intentionally not committed.
"""
import json
import pathlib
import subprocess
import sys

import yaml

PACKAGE_ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = PACKAGE_ROOT / 'vendor' / 'manifest.yaml'


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def git_root():
    result = run(['git', '-C', str(PACKAGE_ROOT), 'rev-parse', '--show-toplevel'])
    return pathlib.Path(result.stdout.strip()) if result.returncode == 0 else None


def check_in_tree(name, spec, root, failures):
    path = PACKAGE_ROOT / spec['path']
    if not path.is_dir():
        failures.append(f'{name}: missing tree at {spec["path"]}')
        return
    provenance = path / 'VENDORED.json'
    if not provenance.is_file():
        failures.append(f'{name}: missing {provenance.relative_to(PACKAGE_ROOT)}')
    else:
        data = json.loads(provenance.read_text())
        if data.get('ref') != spec['ref']:
            failures.append(
                f'{name}: VENDORED.json pins {data.get("ref")}, manifest pins {spec["ref"]}')
    status = run(['git', '-C', str(root), 'status', '--porcelain', '--',
                  str(path.relative_to(root))])
    if status.returncode != 0:
        failures.append(f'{name}: cannot read git status for {spec["path"]}')
    elif status.stdout.strip():
        failures.append(
            f'{name}: uncommitted changes under {spec["path"]}; record them as patches '
            f'in vendor/patches/{name}/ and list them in the manifest')


def check_apt(name, spec, failures):
    dpkg_name = spec.get('dpkg', spec['package'])
    result = run(['dpkg-query', '-W', '-f=${Version}', dpkg_name])
    if result.returncode != 0:
        failures.append(f'{name}: apt package {dpkg_name} is not installed')
    elif not result.stdout.startswith(spec['version']):
        failures.append(
            f'{name}: installed {result.stdout}, manifest pins {spec["version"]}')


def check_pinned_import(name, spec, failures, warnings):
    path = PACKAGE_ROOT / spec['path']
    if not (path / '.git').exists():
        warnings.append(
            f'{name}: source not imported at {spec["path"]} '
            f'(run update_vendor.py {name})')
        return
    head = run(['git', '-C', str(path), 'rev-parse', 'HEAD']).stdout.strip()
    if head != spec['ref']:
        failures.append(f'{name}: HEAD {head} != manifest {spec["ref"]}')


def main():
    manifest = yaml.safe_load(MANIFEST.read_text())
    drivers = manifest.get('drivers', {})
    root = git_root()
    failures, warnings = [], []
    for name, spec in drivers.items():
        mode = spec.get('mode')
        if mode == 'in-tree':
            if root is None:
                failures.append(f'{name}: not inside a git checkout')
            else:
                check_in_tree(name, spec, root, failures)
        elif mode == 'apt':
            check_apt(name, spec, failures)
        elif mode == 'pinned-import':
            check_pinned_import(name, spec, failures, warnings)
        else:
            failures.append(f'{name}: unknown mode {mode!r}')

    for warning in warnings:
        print(f'WARN  {warning}')
    if failures:
        print('vendor check FAILED')
        for failure in failures:
            print(f'FAIL  {failure}')
        return 1
    print(f'vendor check OK: {len(drivers)} drivers')
    return 0


if __name__ == '__main__':
    sys.exit(main())
