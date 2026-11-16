#!/usr/bin/env python3
"""Build a compact GPU point buffer from the source PCD without changing physics."""
import argparse
import json
from pathlib import Path
import tempfile
import time

import numpy as np

from pcd_reader import read_pcd, read_pcd_header


def voxel_keys(xyz, size):
    cells = np.floor(xyz / size).astype(np.int64)
    if np.any(cells < -(1 << 20)) or np.any(cells >= (1 << 20)):
        raise ValueError('Point coordinates exceed the supported voxel grid')
    cells += 1 << 20
    return ((cells[:, 0].astype(np.uint64) << np.uint64(42)) |
            (cells[:, 1].astype(np.uint64) << np.uint64(21)) |
            cells[:, 2].astype(np.uint64))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir', type=Path, default=Path('generated/pcd0_go2'))
    parser.add_argument('--voxel', type=float, default=0.,
                        help='Visual voxel spacing in metres; 0 keeps every finite source point (default)')
    parser.add_argument('--if-needed', action='store_true', help='Reuse a matching complete GPU buffer')
    args = parser.parse_args()
    if not np.isfinite(args.voxel) or args.voxel < 0:
        parser.error('--voxel must be nonnegative and finite')
    scene = args.scene_dir.resolve()
    metadata = json.loads((scene / 'scene_metadata.json').read_text())
    source = Path(metadata['source'])
    origin = np.asarray(metadata['local_origin_in_pcd'])
    header = read_pcd_header(source)
    if not {'x', 'y', 'z', 'rgb'} <= set(header['FIELDS']):
        raise ValueError('GPU rendering requires x, y, z and packed rgb fields')
    dtype = np.dtype([('xyz', '<f4', (3,)), ('rgb', 'u1', (4,))])
    target = scene / 'cloud_gpu.npy'
    manifest = scene / 'cloud_gpu.json'
    signature = {'format_version': 1, 'source': str(source),
                 'source_size_bytes': source.stat().st_size,
                 'source_mtime_ns': source.stat().st_mtime_ns,
                 'source_points': int(header['POINTS'][0]), 'voxel_m': args.voxel,
                 'local_origin_in_pcd': origin.tolist()}
    if args.if_needed and target.is_file() and manifest.is_file():
        try:
            cached = json.loads(manifest.read_text())
            buffer = np.load(target, mmap_mode='r', allow_pickle=False)
            if (all(cached.get(key) == value for key, value in signature.items())
                    and buffer.dtype == dtype and buffer.shape == (cached['points'],)
                    and buffer.nbytes == cached['buffer_bytes']):
                print(f'Reusing GPU cloud: {len(buffer):,} points', flush=True)
                return
        except (OSError, ValueError, KeyError):
            pass
    cloud, _ = read_pcd(source)
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='.cloud-gpu-', dir=scene) as directory:
        temporary_dir = Path(directory)
        temporary_path = temporary_dir / 'cloud_gpu.npy'
        destination = None
        try:
            blocks = []
            cursor = 0
            if not args.voxel:
                # Count before allocating so invalid coordinates are excluded without
                # concatenating a second full copy of a multi-million-point cloud.
                count = 0
                for start in range(0, len(cloud), 1_000_000):
                    block = cloud[start:start + 1_000_000]
                    count += int(np.logical_and.reduce([
                        np.isfinite(block[name]) for name in ('x', 'y', 'z')]).sum())
                if not count:
                    raise ValueError('PCD contains no finite points')
                destination = np.lib.format.open_memmap(
                    temporary_path, mode='w+', dtype=dtype, shape=(count,))
            for start in range(0, len(cloud), 1_000_000):
                block = cloud[start:start + 1_000_000]
                xyz = np.column_stack([block[name] for name in ('x', 'y', 'z')])
                valid = np.isfinite(xyz).all(axis=1)
                xyz = (xyz[valid] - origin).astype(np.float32)
                packed = block['rgb'][valid]
                if packed.dtype.kind == 'f':
                    if packed.dtype.itemsize != 4:
                        raise ValueError('Packed floating rgb must be float32')
                    packed = packed.view('<u4')
                if args.voxel:
                    _, keep = np.unique(voxel_keys(xyz, args.voxel), return_index=True)
                    xyz, packed = xyz[keep], packed[keep]
                points = np.empty(len(xyz), dtype=dtype)
                points['xyz'] = xyz
                for channel, shift in enumerate((16, 8, 0)):
                    points['rgb'][:, channel] = (packed >> shift) & 255
                points['rgb'][:, 3] = 255
                if destination is None:
                    blocks.append(points)
                else:
                    destination[cursor:cursor + len(points)] = points
                    cursor += len(points)
                print(f'Read {min(start + len(block), len(cloud)):,}/{len(cloud):,} points', flush=True)
            if destination is None:
                points = np.concatenate(blocks)
                del blocks
                _, keep = np.unique(voxel_keys(points['xyz'], args.voxel), return_index=True)
                points = points[keep]
                np.save(temporary_path, points, allow_pickle=False)
            else:
                if cursor != count:
                    raise ValueError('Finite point count changed during conversion')
                destination.flush()
                points = destination
            result = {
                **signature,
                'points': len(points), 'voxel_m': args.voxel,
                'point_radius_m': args.voxel * .85 if args.voxel else .0085,
                'sampling': 'voxel' if args.voxel else 'all finite source points, in original order',
                'local_origin_in_pcd': origin.tolist(), 'buffer_bytes': points.nbytes,
                'buffer_layout': 'xyz: little-endian float32[3]; rgb: uint8[4]; stride: 16 bytes',
                'preparation_seconds': time.monotonic() - started,
                'physics_model': str(scene / 'go2_physics.xml'),
            }
            temporary_manifest = temporary_dir / 'cloud_gpu.json'
            temporary_manifest.write_text(json.dumps(result, indent=2) + '\n')
            # Publish only after both files are complete. An existing viewer keeps its
            # mapping, and conversion/serialization errors leave both old files intact.
            previous_target = temporary_dir / 'previous-cloud.npy'
            if target.exists():
                previous_target.hardlink_to(target)
            temporary_path.replace(target)
            try:
                temporary_manifest.replace(manifest)
            except BaseException:
                # A failed second rename must not leave new points with old metadata.
                if previous_target.exists():
                    previous_target.replace(target)
                else:
                    target.unlink()
                raise
        finally:
            if destination is not None:
                destination._mmap.close()
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
