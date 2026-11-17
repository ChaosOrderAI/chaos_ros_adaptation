#!/usr/bin/env python3
"""Convert a large coloured binary PLY triangle mesh into a local-frame PCD.

Offline tool; requires NumPy. The scene pipeline consumes coloured points, so
the mesh surface is written as its own vertices plus area-proportional samples
inside each triangle. Sample colours are barycentric blends of the triangle's
vertex colours, i.e. what a vertex-coloured mesh renderer would show. Nothing
outside the mesh surface is invented. The source is memory-mapped, read only.

Projected coordinates do not fit float32 at millimetre precision, so an origin
is subtracted first (default: XY centre of the vertex bounds, Z unchanged). It
is recorded in the PCD VIEWPOINT and in the JSON report next to the output.
"""
import argparse
import json
from pathlib import Path

import numpy as np

_PLY_TYPES = {'char': 'i1', 'uchar': 'u1', 'short': '<i2', 'ushort': '<u2',
              'int': '<i4', 'uint': '<u4', 'float': '<f4', 'double': '<f8',
              'int8': 'i1', 'uint8': 'u1', 'int16': '<i2', 'uint16': '<u2',
              'int32': '<i4', 'uint32': '<u4', 'float32': '<f4', 'float64': '<f8'}


def read_ply_layout(path):
    """Return (vertex dtype, vertex count, face dtype, face count, payload offset)."""
    elements = []
    with path.open('rb') as stream:
        if stream.readline().strip() != b'ply':
            raise ValueError('Not a PLY file')
        while True:
            line = stream.readline()
            if not line:
                raise ValueError('PLY header is not terminated')
            words = line.decode('ascii').split()
            if not words or words[0] == 'comment':
                continue
            if words[0] == 'format' and words[1] != 'binary_little_endian':
                raise ValueError('Only binary_little_endian PLY is supported')
            if words[0] == 'element':
                elements.append((words[1], int(words[2]), []))
            elif words[0] == 'property':
                elements[-1][2].append(words[1:])
            elif words[0] == 'end_header':
                break
        offset = stream.tell()
    if [name for name, _, _ in elements[:2]] != ['vertex', 'face']:
        raise ValueError('Expected vertex and face elements, in that order')
    (_, vertices, vertex_props), (_, faces, face_props) = elements[:2]
    if any(prop[0] == 'list' for prop in vertex_props):
        raise ValueError('List properties on vertices are not supported')
    vertex_dtype = np.dtype([(name, _PLY_TYPES[kind]) for kind, name in vertex_props])
    missing = {'x', 'y', 'z', 'red', 'green', 'blue'} - set(vertex_dtype.names)
    if missing:
        raise ValueError(f'Vertices lack {sorted(missing)}')
    if len(face_props) != 1 or face_props[0][0] != 'list':
        raise ValueError('Faces must hold exactly one vertex index list')
    face_dtype = np.dtype([('count', _PLY_TYPES[face_props[0][1]]),
                           ('index', _PLY_TYPES[face_props[0][2]], (3,))])
    expected = offset + vertices * vertex_dtype.itemsize + faces * face_dtype.itemsize
    if path.stat().st_size != expected:
        raise ValueError('File size does not match a pure triangle mesh')
    return vertex_dtype, vertices, face_dtype, faces, offset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ply', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--density', type=float, default=150.,
                        help='Surface samples per square metre in addition to the vertices; 0 writes vertices only')
    parser.add_argument('--origin', type=float, nargs=3,
                        help='Source coordinates of the local origin (default: XY centre of the bounds, Z 0)')
    parser.add_argument('--block-faces', type=int, default=1_000_000)
    args = parser.parse_args()
    if not (np.isfinite(args.density) and args.density >= 0):
        parser.error('Density must be finite and not negative')
    source = args.ply.resolve()
    vertex_dtype, vertex_count, face_dtype, face_count, offset = read_ply_layout(source)
    vertices = np.memmap(source, dtype=vertex_dtype, mode='r', offset=offset, shape=(vertex_count,))
    faces = np.memmap(source, dtype=face_dtype, mode='r',
                      offset=offset + vertex_count * vertex_dtype.itemsize, shape=(face_count,))

    lower, upper = np.full(3, np.inf), np.full(3, -np.inf)
    for start in range(0, vertex_count, 2_000_000):
        block = vertices[start:start + 2_000_000]
        xyz = np.column_stack([block[name] for name in 'xyz'])
        if not np.isfinite(xyz).all():
            raise ValueError('Mesh has non-finite vertices')
        lower, upper = np.minimum(lower, xyz.min(axis=0)), np.maximum(upper, xyz.max(axis=0))
    origin = (np.asarray(args.origin) if args.origin else
              np.array([(lower[0] + upper[0]) / 2, (lower[1] + upper[1]) / 2, 0.]))

    def vertex_block(index):
        block = vertices[index]
        xyz = np.column_stack([block[name] for name in 'xyz']) - origin
        rgb = np.column_stack([block[name] for name in ('red', 'green', 'blue')])
        return xyz, rgb.astype(np.float32)

    def face_block(start):
        block = faces[start:start + args.block_faces]
        if (block['count'] != 3).any():
            raise ValueError('Only triangles are supported')
        index = block['index'].astype(np.int64)
        if index.max() >= vertex_count:
            raise ValueError('Face index is outside the vertex array')
        # One gather for all corners keeps the memory-mapped reads sorted.
        unique, inverse = np.unique(index, return_inverse=True)
        xyz, rgb = vertex_block(unique)
        inverse = inverse.reshape(index.shape)
        return xyz[inverse], rgb[inverse]

    # Pass 1: decide the sample count of every triangle so the header is exact.
    rng = np.random.default_rng(0)
    counts = np.zeros(face_count, dtype=np.uint16)
    area_total = 0.
    for start in range(0, face_count, args.block_faces):
        xyz, _ = face_block(start)
        area = .5 * np.linalg.norm(np.cross(xyz[:, 1] - xyz[:, 0], xyz[:, 2] - xyz[:, 0]), axis=1)
        area_total += float(area.sum())
        # Stochastic rounding keeps the expected density on tiny triangles.
        wanted = np.floor(area * args.density + rng.random(len(area)))
        counts[start:start + len(area)] = np.minimum(wanted, np.iinfo(np.uint16).max)
        print(f'Measured {min(start + len(area), face_count):,}/{face_count:,} faces', flush=True)
    sampled = int(counts.sum(dtype=np.int64))
    total = vertex_count + sampled

    point = np.dtype([('x', '<f4'), ('y', '<f4'), ('z', '<f4'), ('rgb', '<u4')])

    def pack(xyz, rgb):
        block = np.empty(len(xyz), dtype=point)
        for i, name in enumerate('xyz'):
            block[name] = xyz[:, i]
        colour = np.clip(np.rint(rgb), 0, 255).astype(np.uint32)
        block['rgb'] = (colour[:, 0] << 16) | (colour[:, 1] << 8) | colour[:, 2]
        return block

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_name(output.name + '.partial')
    with partial.open('wb') as stream:
        stream.write((
            '# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\n'
            'FIELDS x y z rgb\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n'
            f'WIDTH {total}\nHEIGHT 1\n'
            f'VIEWPOINT {origin[0]!r} {origin[1]!r} {origin[2]!r} 1 0 0 0\n'
            f'POINTS {total}\nDATA binary\n').encode('ascii'))
        for start in range(0, vertex_count, 2_000_000):
            pack(*vertex_block(slice(start, start + 2_000_000))).tofile(stream)
        for start in range(0, face_count, args.block_faces):
            repeat = counts[start:start + args.block_faces]
            if repeat.any():
                xyz, rgb = face_block(start)
                owner = np.repeat(np.arange(len(repeat)), repeat)
                # Uniform barycentric weights: fold the unit square onto the triangle.
                u, v = rng.random(len(owner)), rng.random(len(owner))
                fold = u + v > 1
                u[fold], v[fold] = 1 - u[fold], 1 - v[fold]
                weights = np.column_stack([1 - u - v, u, v])
                pack(np.einsum('nk,nkd->nd', weights, xyz[owner]),
                     np.einsum('nk,nkd->nd', weights, rgb[owner])).tofile(stream)
            print(f'Sampled {min(start + len(repeat), face_count):,}/{face_count:,} faces', flush=True)
    partial.replace(output)
    stat = source.stat()
    report = {'source': str(source), 'source_size_bytes': stat.st_size,
              'source_mtime_ns': stat.st_mtime_ns,
              'source_vertices': vertex_count, 'source_faces': face_count,
              'surface_area_m2': area_total, 'points': total,
              'vertex_points': vertex_count, 'surface_sample_points': sampled,
              'surface_samples_per_m2': args.density,
              'sampling': 'all mesh vertices, then area-proportional uniform samples per triangle '
                          'with barycentric vertex colours (seed 0)',
              'origin_in_source': origin.tolist(),
              'coordinate_transform': 'pcd_xyz = mesh_xyz - origin_in_source',
              'source_bounds': [lower.tolist(), upper.tolist()],
              'local_bounds': [(lower - origin).tolist(), (upper - origin).tolist()]}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
