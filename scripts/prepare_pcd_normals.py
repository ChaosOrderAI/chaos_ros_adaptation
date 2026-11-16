#!/usr/bin/env python3
"""Estimate local surface normals for GPU point splats (NumPy and SciPy)."""
import argparse
from collections import OrderedDict
from dataclasses import dataclass
import json
import mmap
from pathlib import Path
import tempfile
import time

import numpy as np
from scipy.spatial import cKDTree


@dataclass
class Tile:
    path: Path
    count: int
    lower: np.ndarray
    upper: np.ndarray
    size: float
    depth: int = 0


def release_pages(array, *, flush=False):
    """Keep mapped input/output pages out of the working set between batches."""
    if flush:
        array.flush()
    mapping = getattr(array, '_mmap', None)
    if mapping is not None and hasattr(mapping, 'madvise'):
        mapping.madvise(mmap.MADV_DONTNEED)


class SpatialIndex:
    """Disk-backed spatial buckets with bounded, exact local KD trees.

    Every record retains its original array index. Partitioning changes neither
    the number of displayed points nor the set of nearest-neighbor candidates.
    """

    def __init__(self, directory, point_count, tile_size, max_tile_points, batch_size):
        self.directory = Path(directory)
        index_type = '<u4' if point_count <= np.iinfo(np.uint32).max else '<u8'
        self.dtype = np.dtype([('xyz', '<f4', (3,)), ('index', index_type)])
        self.tile_size = tile_size
        self.max_tile_points = max_tile_points
        self.batch_size = batch_size
        self.next_file = 0

    def new_path(self):
        path = self.directory / f'tile_{self.next_file:08d}.bin'
        self.next_file += 1
        return path

    def records(self, path):
        with path.open('rb') as stream:
            while True:
                records = np.fromfile(stream, dtype=self.dtype, count=self.batch_size)
                if not len(records):
                    return
                yield records

    def partition(self, batches, size, depth=0):
        buckets = {}
        for records in batches:
            cells = np.floor(records['xyz'].astype(np.float64) / size)
            if not np.isfinite(cells).all() or np.any(np.abs(cells) >= 2**62):
                raise ValueError('Point coordinates exceed the spatial grid range')
            cells = cells.astype(np.int64)
            order = np.lexsort((cells[:, 2], cells[:, 1], cells[:, 0]))
            cells = cells[order]
            records = records[order]
            starts = np.r_[0, np.flatnonzero(np.any(cells[1:] != cells[:-1], axis=1)) + 1]
            ends = np.r_[starts[1:], len(records)]
            for start, end in zip(starts, ends):
                key = tuple(cells[start])
                group = records[start:end]
                lower, upper = group['xyz'].min(axis=0), group['xyz'].max(axis=0)
                tile = buckets.get(key)
                if tile is None:
                    tile = Tile(self.new_path(), 0, lower, upper, size, depth)
                    buckets[key] = tile
                with tile.path.open('ab') as stream:
                    group.tofile(stream)
                tile.count += len(group)
                tile.lower = np.minimum(tile.lower, lower)
                tile.upper = np.maximum(tile.upper, upper)
        return list(buckets.values())

    def build(self, points):
        def source_batches():
            for start in range(0, len(points), self.batch_size):
                xyz = points['xyz'][start:start + self.batch_size]
                if not np.isfinite(xyz).all():
                    raise ValueError('GPU cloud contains non-finite coordinates')
                records = np.empty(len(xyz), dtype=self.dtype)
                records['xyz'] = xyz
                records['index'] = np.arange(start, start + len(xyz), dtype=self.dtype['index'])
                yield records
                release_pages(points)
        pending = self.partition(source_batches(), self.tile_size)
        tiles = []
        while pending:
            tile = pending.pop()
            if tile.count <= self.max_tile_points:
                tiles.append(tile)
                continue
            if tile.depth >= 32 or np.array_equal(tile.lower, tile.upper):
                # Coincident points cannot be split spatially. Small overlapping
                # buckets still participate in the exact nearest-neighbor merge.
                with tile.path.open('rb') as stream:
                    while True:
                        records = np.fromfile(stream, dtype=self.dtype, count=self.max_tile_points)
                        if not len(records):
                            break
                        path = self.new_path()
                        records.tofile(path)
                        tiles.append(Tile(path, len(records), records['xyz'].min(axis=0),
                                          records['xyz'].max(axis=0), tile.size, tile.depth))
            else:
                pending.extend(self.partition(self.records(tile.path), tile.size / 2, tile.depth + 1))
            tile.path.unlink()
        return tiles


def box_distance_squared(points, lower, upper):
    delta = np.maximum(np.maximum(lower - points, points - upper), 0.)
    return np.einsum('...i,...i->...', delta, delta)


def estimate_tiled(points, normals, radii, directory, voxel, tile_size,
                   max_tile_points, batch_size, workers):
    """Compute globally exact kNN, loading bounded spatial trees on demand.

    A neighboring tile is queried only for points whose current kth distance
    reaches its bounding box. The search ends only when the remaining boxes
    cannot contain a closer neighbor: this is an adaptive halo, including for
    sparse or isolated points, rather than an approximate fixed-radius crop.
    """
    index = SpatialIndex(directory, len(points), tile_size, max_tile_points,
                         min(max_tile_points, 200_000))
    tiles = index.build(points)
    print(f'Spatial index: {len(tiles):,} tiles, at most {max_tile_points:,} points per tree', flush=True)
    lower = np.asarray([tile.lower for tile in tiles], dtype=np.float64)
    upper = np.asarray([tile.upper for tile in tiles], dtype=np.float64)
    neighbors_count = min(12, len(points))
    tree_cache = OrderedDict()
    completed = 0
    last_report = 0
    for tile_id, tile in enumerate(tiles):
        records = np.fromfile(tile.path, dtype=index.dtype)
        query = records['xyz'].astype(np.float64)
        best_distances = np.full((len(query), neighbors_count), np.inf)
        best_points = np.zeros((len(query), neighbors_count, 3), dtype=np.float32)
        box_delta = np.maximum(np.maximum(lower - tile.upper, tile.lower - upper), 0.)
        box_distances = np.einsum('ij,ij->i', box_delta, box_delta)
        order = np.argsort(box_distances)
        order = np.r_[tile_id, order[order != tile_id]]
        for candidate_id in order:
            if candidate_id != tile_id and box_distances[candidate_id] > np.max(best_distances[:, -1])**2:
                break
            distance_to_box = box_distance_squared(query, lower[candidate_id], upper[candidate_id])
            eligible = np.flatnonzero(distance_to_box <= best_distances[:, -1]**2)
            if not len(eligible):
                continue
            if candidate_id in tree_cache:
                tree = tree_cache.pop(candidate_id)
            else:
                candidate = np.fromfile(tiles[candidate_id].path, dtype=index.dtype)
                tree = cKDTree(candidate['xyz'].astype(np.float64))
                del candidate
            tree_cache[candidate_id] = tree
            while len(tree_cache) > 3:
                tree_cache.popitem(last=False)
            count = min(neighbors_count, tree.n)
            for start in range(0, len(eligible), batch_size):
                rows = eligible[start:start + batch_size]
                distances, nearest = tree.query(query[rows], k=count, workers=workers)
                if count == 1:
                    distances, nearest = distances[:, None], nearest[:, None]
                if candidate_id == tile_id and count == neighbors_count:
                    best_distances[rows] = distances
                    best_points[rows] = tree.data[nearest]
                else:
                    merged_distances = np.concatenate((best_distances[rows], distances), axis=1)
                    merged_points = np.concatenate((best_points[rows], tree.data[nearest]), axis=1)
                    choice = np.argsort(merged_distances, axis=1, kind='stable')[:, :neighbors_count]
                    best_distances[rows] = np.take_along_axis(merged_distances, choice, axis=1)
                    best_points[rows] = np.take_along_axis(merged_points, choice[:, :, None], axis=1)
        if not np.isfinite(best_distances).all():
            raise ValueError('Spatial search did not find enough neighbors')
        for start in range(0, len(query), batch_size):
            end = start + batch_size
            delta = best_points[start:end].astype(np.float64)
            delta -= delta.mean(axis=1, keepdims=True)
            covariance = np.einsum('nki,nkj->nij', delta, delta)
            _, vectors = np.linalg.eigh(covariance)
            normal = vectors[:, :, 0]
            normal[normal[:, 2] < 0] *= -1
            if not np.isfinite(normal).all() or not np.allclose(np.linalg.norm(normal, axis=1), 1, atol=1e-5):
                raise ValueError('Invalid estimated normals')
            rows = records['index'][start:end]
            normals[rows] = normal
            radii[rows] = np.clip(.8 * best_distances[start:end, min(6, neighbors_count - 1)],
                                  max(.003, voxel * .85), max(.12, voxel * .85))
        completed += len(query)
        release_pages(normals, flush=True)
        release_pages(radii, flush=True)
        if completed - last_report >= 500_000 or tile_id + 1 == len(tiles):
            print(f'Normals {completed:,}/{len(points):,} ({tile_id + 1}/{len(tiles)} tiles)', flush=True)
            last_report = completed
    return {'estimation_method': 'exact 12NN with disk-backed spatial tiles and adaptive halo',
            'tile_size_m': tile_size, 'max_tile_points': max_tile_points, 'tiles': len(tiles)}


def positive_int(value):
    result = int(value)
    if result < 1:
        raise argparse.ArgumentTypeError('must be a positive integer')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir', type=Path, default=Path('generated/pcd0_go2'))
    parser.add_argument('--if-needed', action='store_true')
    parser.add_argument('--tile-size', type=float, default=8., help='Initial spatial tile width in metres')
    parser.add_argument('--max-tile-points', type=positive_int, default=250_000,
                        help='Maximum points per local KD tree; dense tiles are subdivided')
    parser.add_argument('--batch-size', type=positive_int, default=8192, help='Neighbor query/PCA batch size')
    parser.add_argument('--workers', type=positive_int, default=4, help='Local KD-tree query threads')
    args = parser.parse_args()
    if not np.isfinite(args.tile_size) or args.tile_size <= 0:
        parser.error('--tile-size must be positive and finite')
    scene = args.scene_dir.resolve()
    cloud_path = scene / 'cloud_gpu.npy'
    target = scene / 'cloud_gpu_normals.npy'
    radius_target = scene / 'cloud_gpu_radii.npy'
    manifest = scene / 'cloud_gpu_normals.json'
    points = np.load(cloud_path, mmap_mode='r', allow_pickle=False)
    if (points.ndim != 1 or points.dtype.names is None or 'xyz' not in points.dtype.names
            or points['xyz'].shape != (len(points), 3) or points['xyz'].dtype != np.dtype('<f4')):
        raise ValueError('Expected a one-dimensional GPU cloud with float32 xyz[3] records')
    voxel = float(json.loads((scene / 'cloud_gpu.json').read_text())['voxel_m'])
    if not np.isfinite(voxel) or voxel < 0:
        raise ValueError('Cloud voxel size must be finite and nonnegative')
    # The output contract and exact 12NN/PCA definition are unchanged, so valid
    # version-2 caches remain reusable regardless of the working-set settings.
    signature = {'format_version': 2, 'cloud_size_bytes': cloud_path.stat().st_size,
                 'cloud_mtime_ns': cloud_path.stat().st_mtime_ns, 'points': len(points),
                 'neighbors': 12, 'voxel_m': voxel}
    if args.if_needed and target.is_file() and radius_target.is_file() and manifest.is_file():
        try:
            cached = json.loads(manifest.read_text())
            normals = np.load(target, mmap_mode='r', allow_pickle=False)
            radii = np.load(radius_target, mmap_mode='r', allow_pickle=False)
            if (all(cached.get(key) == value for key, value in signature.items())
                    and normals.shape == (len(points), 3) and normals.dtype == np.dtype('<f4')
                    and radii.shape == (len(points),) and radii.dtype == np.dtype('<f4')):
                print(f'Reusing normals: {len(normals):,} points')
                return
        except (OSError, ValueError):
            pass
    started = time.monotonic()
    if len(points) < 3:
        raise ValueError('At least three points are required for surface normals')
    with tempfile.TemporaryDirectory(dir=scene, prefix='.normals-') as directory:
        temporary_normals = Path(directory) / 'normals.npy'
        temporary_radii = Path(directory) / 'radii.npy'
        normals = np.lib.format.open_memmap(temporary_normals, mode='w+', dtype='<f4', shape=(len(points), 3))
        radii = np.lib.format.open_memmap(temporary_radii, mode='w+', dtype='<f4', shape=(len(points),))
        details = estimate_tiled(points, normals, radii, directory, voxel, args.tile_size,
                                 args.max_tile_points, args.batch_size, args.workers)
        result = {**signature, **details, 'preparation_seconds': time.monotonic() - started,
                  'normal_bytes': normals.nbytes, 'radii_bytes': radii.nbytes,
                  'radius_quantiles_m': np.quantile(radii, [0, .5, .9, .99, 1]).tolist()}
        del normals, radii
        # Publish complete arrays; running viewers keep their old file mappings.
        temporary_normals.replace(target)
        temporary_radii.replace(radius_target)
    with tempfile.NamedTemporaryFile(dir=scene, suffix='.json', mode='w', delete=False) as temporary:
        temporary_path = Path(temporary.name)
        temporary.write(json.dumps(result, indent=2) + '\n')
    temporary_path.replace(manifest)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
