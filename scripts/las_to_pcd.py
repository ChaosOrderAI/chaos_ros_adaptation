#!/usr/bin/env python3
"""Convert an uncompressed LAS point cloud to the binary PCD the scene tools read.

The source is never modified and never loaded whole: points are first grouped
into spatial tiles on disk, then each tile is thinned and written. Thinning
keeps the first measured point of every occupied voxel, in source order; no
point is averaged or invented. Voxels are computed from the integer LAS
coordinates, so the result does not depend on floating-point rounding.

The output uses local coordinates, `source - origin`, because a float32 PCD
cannot hold projected (UTM) coordinates at millimetre resolution. The origin is
recorded in the PCD VIEWPOINT and in the JSON report next to the output.
"""
import argparse
import json
import shutil
import struct
import tempfile
import time
from pathlib import Path

import numpy as np

# Byte offset of the 16-bit RGB triple in each LAS point format that has one.
_RGB_OFFSET = {2: 20, 3: 28, 5: 28, 7: 30, 8: 30, 10: 30}
_SURVEY_VOXELS_M = (.01, .015, .02, .025, .03, .04, .05, .075, .1)


def read_las_header(path):
    """Validate the public header block and return the layout of the point records."""
    size = path.stat().st_size
    with path.open('rb') as stream:
        block = stream.read(375)
    if len(block) < 227 or block[:4] != b'LASF':
        raise ValueError('Not a LAS file')
    major, minor = block[24], block[25]
    if major != 1 or minor not in (2, 3, 4):
        raise ValueError(f'Unsupported LAS version {major}.{minor}')
    offset, = struct.unpack_from('<I', block, 96)
    point_format, record_bytes = struct.unpack_from('<BH', block, 104)
    if point_format & 0xC0:
        raise ValueError('Compressed LAS (LAZ) is not supported; decompress it first')
    if point_format not in _RGB_OFFSET:
        raise ValueError(f'LAS point format {point_format} has no RGB colour')
    points, = struct.unpack_from('<I', block, 107)
    if minor == 4 and len(block) >= 255:
        points = struct.unpack_from('<Q', block, 247)[0] or points
    scale = struct.unpack_from('<3d', block, 131)
    las_offset = struct.unpack_from('<3d', block, 155)
    max_x, min_x, max_y, min_y, max_z, min_z = struct.unpack_from('<6d', block, 179)
    rgb_offset = _RGB_OFFSET[point_format]
    if record_bytes < rgb_offset + 6 or points <= 0:
        raise ValueError('Invalid LAS point record length or point count')
    if size < offset + points * record_bytes:
        raise ValueError('LAS file is shorter than its header declares')
    if not all(np.isfinite(value) and value > 0 for value in scale):
        raise ValueError('LAS scale factors must be positive')
    dtype = np.dtype({'names': ['xyz', 'rgb'], 'formats': [('<i4', (3,)), ('<u2', (3,))],
                      'offsets': [0, rgb_offset], 'itemsize': record_bytes})
    return {'version': f'{major}.{minor}', 'point_format': point_format,
            'record_bytes': record_bytes, 'payload_offset': offset, 'points': points,
            'scale': scale, 'offset': las_offset, 'dtype': dtype,
            'minimum': (min_x, min_y, min_z), 'maximum': (max_x, max_y, max_z)}


def write_tiles(path, header, tile_raw, directory, block_points):
    """Group the raw records by XY tile, keeping source order inside each tile."""
    cloud = np.memmap(path, mode='r', dtype=header['dtype'], offset=header['payload_offset'],
                      shape=(header['points'],))
    record = np.dtype([('xyz', '<i4', (3,)), ('rgb', '<u2', (3,))])
    streams = {}
    counts = {}
    low = np.full(3, np.iinfo(np.int64).max)
    high = np.full(3, np.iinfo(np.int64).min)
    colour_max = 0
    started = time.monotonic()
    try:
        for start in range(0, header['points'], block_points):
            block = np.array(cloud[start:start + block_points])
            xyz = block['xyz']
            low = np.minimum(low, xyz.min(axis=0))
            high = np.maximum(high, xyz.max(axis=0))
            colour_max = max(colour_max, int(block['rgb'].max()))
            # Floor division keeps tiles aligned with every voxel grid whose
            # size divides the tile size, including for negative coordinates.
            tiles = xyz[:, :2].astype(np.int64) // tile_raw
            keys = tiles[:, 0] * (1 << 32) + (tiles[:, 1] & 0xFFFFFFFF)
            order = np.argsort(keys, kind='stable')
            sorted_keys = keys[order]
            edges = np.flatnonzero(np.diff(sorted_keys)) + 1
            packed = np.empty(len(block), dtype=record)
            packed['xyz'] = xyz[order]
            packed['rgb'] = block['rgb'][order]
            for first, last in zip(np.r_[0, edges], np.r_[edges, len(block)]):
                key = int(sorted_keys[first])
                if key not in streams:
                    streams[key] = (directory / f'{len(streams):06d}.bin').open('wb')
                    counts[key] = 0
                packed[first:last].tofile(streams[key])
                counts[key] += last - first
            done = min(header['points'], start + block_points)
            print(f'Tiled {done:,}/{header["points"]:,} points '
                  f'({time.monotonic() - started:.0f} s)', flush=True)
    finally:
        names = {key: Path(stream.name) for key, stream in streams.items()}
        for stream in streams.values():
            stream.close()
        cloud._mmap.close()
    # Tiles are visited in a fixed spatial order, independent of discovery order.
    tiles = [(names[key], counts[key]) for key in sorted(names)]
    return tiles, record, low, high, colour_max


def voxel_first_points(xyz, voxel_raw):
    """Indices of the first point of every occupied voxel, in source order."""
    cells = xyz.astype(np.int64) // voxel_raw
    cells -= cells.min(axis=0)
    spans = cells.max(axis=0) + 1
    keys = (cells[:, 0] * spans[1] + cells[:, 1]) * spans[2] + cells[:, 2]
    _, first = np.unique(keys, return_index=True)
    first.sort()
    return first


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('las', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='Binary PCD to write')
    thinning = parser.add_mutually_exclusive_group()
    thinning.add_argument('--voxel', type=float, help='Voxel size in metres; 0 keeps every point')
    thinning.add_argument('--target-points', type=int,
                          help='Use the finest surveyed voxel that keeps at most this many points')
    parser.add_argument('--origin', type=float, nargs=3, metavar=('X', 'Y', 'Z'),
                        help='Source coordinate of the local origin; default: XY centre of the '
                             'header bounds and Z = 0')
    parser.add_argument('--tile-size', type=float, default=32., help='Work tile width in metres')
    parser.add_argument('--work-dir', type=Path, help='Tile directory parent; default: beside the output')
    parser.add_argument('--block-points', type=int, default=4_000_000)
    args = parser.parse_args()
    if args.voxel is None and args.target_points is None:
        parser.error('one of --voxel or --target-points is required')
    if args.voxel is not None and (not np.isfinite(args.voxel) or args.voxel < 0):
        parser.error('--voxel must be finite and non-negative')
    if args.target_points is not None and args.target_points <= 0:
        parser.error('--target-points must be positive')
    if not np.isfinite(args.tile_size) or args.tile_size <= 0 or args.block_points <= 0:
        parser.error('--tile-size and --block-points must be positive')

    source = args.las.expanduser().resolve()
    output = args.output.expanduser().resolve()
    header = read_las_header(source)
    scale = np.array(header['scale'])
    las_offset = np.array(header['offset'])
    if args.origin is None:
        origin = np.array([(header['minimum'][0] + header['maximum'][0]) / 2,
                           (header['minimum'][1] + header['maximum'][1]) / 2, 0.])
    else:
        origin = np.array(args.origin)
    if not np.isfinite(origin).all():
        parser.error('--origin must be finite')
    print(f'LAS {header["version"]} format {header["point_format"]}: {header["points"]:,} points, '
          f'scale {tuple(header["scale"])}', flush=True)

    def raw_size(metres):
        raw = np.rint(metres / scale).astype(np.int64)
        if np.any(raw < 1):
            raise ValueError(f'{metres} m is finer than the LAS coordinate resolution')
        return raw

    started = time.monotonic()
    output.parent.mkdir(parents=True, exist_ok=True)
    work_parent = (args.work_dir or output.parent).expanduser().resolve()
    work_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='las-tiles-', dir=work_parent) as directory:
        directory = Path(directory)
        tiles, record, low, high, colour_max = write_tiles(
            source, header, raw_size(args.tile_size)[:2], directory, args.block_points)
        if sum(count for _, count in tiles) != header['points']:
            raise RuntimeError('Tiling lost points')
        measured_low = low * scale + las_offset
        measured_high = high * scale + las_offset
        if max(np.abs(measured_low - origin).max(), np.abs(measured_high - origin).max()) > 4096:
            # float32 resolves 0.5 mm at 4096 m and coarser beyond.
            print('Warning: local coordinates exceed 4096 m; choose a closer --origin', flush=True)

        survey = {}
        if args.target_points is not None:
            candidates = [size for size in _SURVEY_VOXELS_M if np.all(raw_size(size) >= 1)]
            totals = dict.fromkeys(candidates, 0)
            for index, (tile, count) in enumerate(tiles):
                xyz = np.fromfile(tile, dtype=record)['xyz']
                for size in candidates:
                    totals[size] += len(voxel_first_points(xyz, raw_size(size)))
                print(f'Surveyed tile {index + 1}/{len(tiles)}', flush=True)
            survey = {f'{size:g}': total for size, total in totals.items()}
            for size, total in totals.items():
                print(f'  voxel {size * 100:g} cm keeps {total:,} points')
            if header['points'] <= args.target_points:
                voxel = 0.
            else:
                fitting = [size for size in candidates if totals[size] <= args.target_points]
                if not fitting:
                    raise SystemExit('No surveyed voxel size meets --target-points')
                voxel = min(fitting)
        else:
            voxel = args.voxel
        voxel_raw = raw_size(voxel) if voxel > 0 else None
        print(f'Writing with voxel {voxel:g} m', flush=True)

        # LAS stores 16-bit colour; many writers only fill the low byte.
        colour_shift = 8 if colour_max > 255 else 0
        point = np.dtype([('x', '<f4'), ('y', '<f4'), ('z', '<f4'), ('rgb', '<u4')])
        payload = directory / 'payload.bin'
        kept = 0
        local_low = np.full(3, np.inf)
        local_high = np.full(3, -np.inf)
        with payload.open('wb') as stream:
            for index, (tile, count) in enumerate(tiles):
                data = np.fromfile(tile, dtype=record)
                if voxel_raw is not None:
                    data = data[voxel_first_points(data['xyz'], voxel_raw)]
                local = (data['xyz'] * scale + las_offset) - origin
                colour = (data['rgb'] >> colour_shift).astype(np.uint32)
                block = np.empty(len(data), dtype=point)
                block['x'], block['y'], block['z'] = local[:, 0], local[:, 1], local[:, 2]
                block['rgb'] = (colour[:, 0] << 16) | (colour[:, 1] << 8) | colour[:, 2]
                block.tofile(stream)
                kept += len(block)
                local_low = np.minimum(local_low, local.min(axis=0))
                local_high = np.maximum(local_high, local.max(axis=0))
                tile.unlink()
                print(f'Wrote tile {index + 1}/{len(tiles)}: {kept:,} points', flush=True)
        if payload.stat().st_size != kept * point.itemsize:
            raise RuntimeError('PCD payload size does not match the written point count')
        pcd_header = (
            '# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\n'
            'FIELDS x y z rgb\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n'
            f'WIDTH {kept}\nHEIGHT 1\n'
            f'VIEWPOINT {origin[0]!r} {origin[1]!r} {origin[2]!r} 1 0 0 0\n'
            f'POINTS {kept}\nDATA binary\n').encode('ascii')
        temporary = output.with_name(output.name + '.partial')
        with temporary.open('wb') as target, payload.open('rb') as stream:
            target.write(pcd_header)
            shutil.copyfileobj(stream, target, 64 * 1024 * 1024)
        temporary.replace(output)

    info = source.stat()
    report = {
        'source': str(source), 'source_size_bytes': info.st_size, 'source_mtime_ns': info.st_mtime_ns,
        'las_version': header['version'], 'las_point_format': header['point_format'],
        'source_points': header['points'], 'points': kept, 'voxel_m': voxel,
        'sampling': ('all source points, grouped by work tile' if voxel == 0 else
                     'first source point of every occupied voxel, grouped by work tile'),
        'tile_size_m': args.tile_size, 'work_tiles': len(tiles),
        'origin_in_source': origin.tolist(), 'coordinate_transform': 'pcd_xyz = las_xyz - origin_in_source',
        'source_bounds': [measured_low.tolist(), measured_high.tolist()],
        'local_bounds': [local_low.tolist(), local_high.tolist()],
        'colour': '16-bit, high byte kept' if colour_shift else '8-bit values stored in 16-bit fields',
        'surveyed_voxel_points': survey, 'output': str(output),
        'output_size_bytes': output.stat().st_size, 'seconds': time.monotonic() - started}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
