#!/usr/bin/env python3
"""Explicit, pinned official model download; launch never invokes this tool."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
import urllib.request
from urllib.parse import quote

MAX_FILE_BYTES = 32 * 1024 * 1024
NAME_PATTERN = re.compile(r'[A-Za-z0-9_-]+')
REPO_PATTERN = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+')
SHA_PATTERN = re.compile(r'[0-9a-f]{40}')


def default_lock():
    source = Path(__file__).resolve().parents[1] / 'config/models.lock.json'
    if source.is_file():
        return source
    from ament_index_python.packages import get_package_share_directory
    return Path(get_package_share_directory('chaos_sim')) / 'config/models.lock.json'


def blob_sha(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def read_limited(response, size):
    chunks = []
    remaining = size + 1
    while remaining:
        block = response.read(min(1024 * 1024, remaining))
        if not block:
            break
        chunks.append(block)
        remaining -= len(block)
    return b''.join(chunks)


def destination_for(output, name, relative):
    if not isinstance(relative, str) or not NAME_PATTERN.fullmatch(name):
        raise ValueError(f'unsafe robot name or path: {name} {relative}')
    path = PurePosixPath(relative)
    if (path.is_absolute() or not path.parts or '..' in path.parts or '.' in path.parts or
            '\\' in relative or '\0' in relative):
        raise ValueError(f'unsafe manifest path: {relative}')
    root = output.resolve()
    destination = root / name / Path(*path.parts)
    if destination.is_symlink():
        raise ValueError(f'refusing to use a symlink: {destination}')
    try:
        destination.resolve().relative_to(root)
    except ValueError as error:
        raise ValueError(f'path escapes the output directory: {relative}') from error
    return destination


def fetch(lock, output, robots, verify_only=False):
    entries = json.loads(lock.read_text())
    if not isinstance(entries, list) or not entries:
        raise ValueError('lock must be a nonempty list')
    by_name = {}
    for entry in entries:
        name = entry['name']
        repository = entry['repository']
        commit = entry['commit']
        if (not isinstance(name, str) or name in by_name or not NAME_PATTERN.fullmatch(name)):
            raise ValueError(f'unsafe or duplicate robot name: {name}')
        if (not isinstance(repository, str) or not isinstance(commit, str) or
                not REPO_PATTERN.fullmatch(repository) or not SHA_PATTERN.fullmatch(commit)):
            raise ValueError(f'unsafe repository or commit for {name}')
        by_name[name] = entry
    robots = [part.strip() for part in robots]
    if not robots or len(set(robots)) != len(robots) or set(robots) - by_name.keys():
        raise ValueError('robots must be a unique comma-separated subset of ' + ','.join(by_name))
    verified = downloaded = 0
    for name in robots:
        entry = by_name[name]
        for item in entry['files']:
            size = item['size']
            digest = item['git_blob_sha']
            if (type(size) is not int or not 0 <= size <= MAX_FILE_BYTES or
                    not isinstance(digest, str) or not SHA_PATTERN.fullmatch(digest)):
                raise ValueError(f"invalid pinned size or hash: {item.get('path')}")
            relative = item['path']
            destination = destination_for(output, name, relative)
            if destination.is_file():
                with destination.open('rb') as stream:
                    data = stream.read(size + 1)
                if len(data) != size or blob_sha(data) != digest:
                    raise ValueError(f'cached file fails verification; remove explicitly to re-fetch: {destination}')
                verified += 1
                continue
            if verify_only:
                raise FileNotFoundError(destination)
            quoted = quote(PurePosixPath(relative).as_posix(), safe='/')
            url = (f"https://raw.githubusercontent.com/{quote(entry['repository'], safe='/')}/"
                   f"{entry['commit']}/{quoted}")
            print(f'download {name}/{relative}', flush=True)
            with urllib.request.urlopen(url, timeout=60) as response:
                announced = response.headers.get('Content-Length')
                if announced is not None and announced != str(size):
                    raise ValueError(f'upstream content length {announced} != pinned size {size}: {url}')
                data = read_limited(response, size)
            if len(data) != size or blob_sha(data) != digest:
                raise ValueError(f'upstream file fails pinned verification: {url}')
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as stream:
                    temporary = Path(stream.name)
                    stream.write(data)
                os.replace(temporary, destination)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
            downloaded += 1
    print(f'verified {verified}, downloaded {downloaded}; cache: {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('~/.cache/chaos/mujoco'))
    parser.add_argument('--robots', default='go2,t2')
    parser.add_argument('--lock', type=Path)
    parser.add_argument('--verify-only', action='store_true', help='check every required file without network access')
    args = parser.parse_args()
    fetch(args.lock or default_lock(), args.output.expanduser().resolve(), args.robots.split(','), args.verify_only)


if __name__ == '__main__':
    main()
