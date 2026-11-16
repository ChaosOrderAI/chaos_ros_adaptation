#!/usr/bin/env python3
"""Fit view-independent 3D Gaussians for level-of-detail rendering.

Each 8 m chunk of the measured cloud keeps its original points for close views
and gains one anisotropic Gaussian per occupied voxel for distant views. A
Gaussian is the mean, colour and covariance of the measured points in its
voxel; nothing is invented and no photograph is used. The source buffers are
not modified.
"""
import argparse
import json
import time
from pathlib import Path

import numpy as np


def fit_gaussians(xyz, rgb, voxel):
    """One Gaussian per occupied voxel: mean, colour and regularised covariance."""
    cell = np.floor(xyz / voxel).astype(np.int64)
    cell -= cell.min(axis=0)
    span = cell.max(axis=0) + 1
    key = (cell[:, 0] * span[1] + cell[:, 1]) * span[2] + cell[:, 2]
    order = np.argsort(key, kind='stable')
    key, xyz, rgb = key[order], xyz[order], rgb[order]
    cuts = np.flatnonzero(np.diff(key)) + 1
    starts = np.r_[0, cuts]
    counts = np.diff(np.r_[starts, len(xyz)]).astype(np.float64)
    sum_xyz = np.add.reduceat(xyz, starts)
    products = np.column_stack((xyz[:, 0] * xyz[:, 0], xyz[:, 0] * xyz[:, 1], xyz[:, 0] * xyz[:, 2],
                                xyz[:, 1] * xyz[:, 1], xyz[:, 1] * xyz[:, 2], xyz[:, 2] * xyz[:, 2]))
    sum_products = np.add.reduceat(products, starts)
    sum_rgb = np.add.reduceat(rgb.astype(np.float64), starts)
    mean = sum_xyz / counts[:, None]
    cov = np.empty((len(starts), 3, 3), np.float64)
    upper = sum_products / counts[:, None] - np.column_stack((
        mean[:, 0] * mean[:, 0], mean[:, 0] * mean[:, 1], mean[:, 0] * mean[:, 2],
        mean[:, 1] * mean[:, 1], mean[:, 1] * mean[:, 2], mean[:, 2] * mean[:, 2]))
    cov[:, 0, 0], cov[:, 0, 1], cov[:, 0, 2] = upper[:, 0], upper[:, 1], upper[:, 2]
    cov[:, 1, 0], cov[:, 1, 1], cov[:, 1, 2] = upper[:, 1], upper[:, 3], upper[:, 4]
    cov[:, 2, 0], cov[:, 2, 1], cov[:, 2, 2] = upper[:, 2], upper[:, 4], upper[:, 5]
    eigenvalues, eigenvectors = np.linalg.eigh(cov)
    # A voxel-sized footprint covers the points that were merged, while the
    # lower bound keeps a flat wall from collapsing to a zero-thickness line.
    eigenvalues = np.clip(eigenvalues, (0.25 * voxel) ** 2, (1.25 * voxel) ** 2)
    cov = np.einsum('nij,nj,nkj->nik', eigenvectors, eigenvalues, eigenvectors)
    record = np.dtype([('xyz', '<f4', (3,)), ('rgb', 'u1', (4,)), ('cov', '<f4', (6,))])
    out = np.empty(len(starts), record)
    out['xyz'] = mean
    colour = np.clip(np.rint(sum_rgb / counts[:, None]), 0, 255).astype(np.uint8)
    out['rgb'][:, :3] = colour
    out['rgb'][:, 3] = 255
    out['cov'][:, 0] = cov[:, 0, 0]
    out['cov'][:, 1] = cov[:, 0, 1]
    out['cov'][:, 2] = cov[:, 0, 2]
    out['cov'][:, 3] = cov[:, 1, 1]
    out['cov'][:, 4] = cov[:, 1, 2]
    out['cov'][:, 5] = cov[:, 2, 2]
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir', type=Path, default=Path('generated/pcd0_go2'))
    parser.add_argument('--chunk', type=float, default=8., help='Chunk width in metres')
    parser.add_argument('--voxel', type=float, default=0.16, help='Gaussian voxel size in metres')
    parser.add_argument('--if-needed', action='store_true')
    args = parser.parse_args()
    if not np.isfinite(args.chunk) or args.chunk <= 0 or not np.isfinite(args.voxel) or args.voxel <= 0:
        parser.error('--chunk and --voxel must be positive and finite')
    scene = args.scene_dir.resolve()
    cloud_path = scene / 'cloud_gpu.npy'
    cloud = np.load(cloud_path, mmap_mode='r', allow_pickle=False)
    normals_path, radii_path = scene / 'cloud_gpu_normals.npy', scene / 'cloud_gpu_radii.npy'
    signature = {
        'format_version': 1, 'source': str(cloud_path),
        'source_size_bytes': cloud_path.stat().st_size, 'source_mtime_ns': cloud_path.stat().st_mtime_ns,
        'points': int(cloud.shape[0]), 'chunk_m': args.chunk, 'voxel_m': args.voxel,
    }
    manifest_path = scene / 'gaussians.json'
    if args.if_needed and manifest_path.is_file() and (scene / 'gaussians.npy').is_file():
        try:
            cached = json.loads(manifest_path.read_text())
            if all(cached.get(key) == value for key, value in signature.items()):
                print(f'Reusing 3D Gaussians: {cached["gaussians"]:,}', flush=True)
                return
        except (OSError, ValueError, KeyError):
            pass
    if not normals_path.is_file() or not radii_path.is_file():
        raise SystemExit('3D Gaussians need the normal and radius buffers; run prepare_pcd_normals.py')
    count = signature['points']
    started = time.monotonic()
    keys = np.empty(count, np.int64)
    for begin in range(0, count, 2_000_000):
        end = min(count, begin + 2_000_000)
        cell = np.floor(cloud['xyz'][begin:end, :2] / args.chunk).astype(np.int64)
        keys[begin:end] = (cell[:, 0] + 100_000) * 1_000_000 + (cell[:, 1] + 100_000)
        print(f'Keyed {end:,}/{count:,}', flush=True)
    order = np.argsort(keys)
    sorted_keys = keys[order]
    del keys
    edges = np.r_[0, np.flatnonzero(np.diff(sorted_keys)) + 1, count]
    del sorted_keys
    index_path = scene / 'cloud_chunk_index.npy'
    np.save(index_path, order.astype(np.uint32), allow_pickle=False)
    chunk_dtype = np.dtype([
        ('center', '<f4', (3,)), ('radius', '<f4'), ('point_offset', '<u4'), ('point_count', '<u4'),
        ('gaussian_offset', '<u4'), ('gaussian_count', '<u4')])
    chunks = np.empty(len(edges) - 1, chunk_dtype)
    gaussians = []
    written = 0
    for index, (begin, end) in enumerate(zip(edges[:-1], edges[1:])):
        chosen = order[begin:end]
        xyz = np.asarray(cloud['xyz'][chosen], np.float64)
        low, high = xyz.min(axis=0), xyz.max(axis=0)
        center = (low + high) / 2
        chunks[index]['center'] = center
        chunks[index]['radius'] = 0.5 * np.linalg.norm(high - low) + args.voxel
        chunks[index]['point_offset'] = begin
        chunks[index]['point_count'] = end - begin
        fitted = fit_gaussians(xyz, np.asarray(cloud['rgb'][chosen, :3]), args.voxel)
        chunks[index]['gaussian_offset'] = written
        chunks[index]['gaussian_count'] = len(fitted)
        written += len(fitted)
        gaussians.append(fitted)
        if (index + 1) % 25 == 0 or index + 1 == len(chunks):
            print(f'Fitted {index + 1:,}/{len(chunks):,} chunks, {written:,} Gaussians', flush=True)
    del order
    gaussian_path = scene / 'gaussians.npy'
    chunk_path = scene / 'gaussian_chunks.npy'
    np.save(gaussian_path, np.concatenate(gaussians), allow_pickle=False)
    np.save(chunk_path, chunks, allow_pickle=False)
    result = {**signature, 'chunks': int(len(chunks)), 'gaussians': int(written),
              'seconds': time.monotonic() - started,
              'sampling': 'one covariance Gaussian per occupied voxel inside each spatial chunk'}
    manifest_path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
