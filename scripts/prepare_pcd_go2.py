#!/usr/bin/env python3
"""Prepare a large binary PCD as a colored MuJoCo scene with a Go2 robot.

Offline tool; requires NumPy. Ground is a measured, gridded height proxy around
the selected level, obstacles are voxel boxes. The original PCD is read only.
"""
import argparse
import itertools
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import numpy as np


from pcd_reader import read_pcd


def voxel_indices(xyz, size):
    cells = np.floor(xyz / size).astype(np.int64)
    _, indices = np.unique(cells, axis=0, return_index=True)
    return indices


def _merge_boxes_along_axis(lower, upper, axis):
    """Join touching integer boxes only when their other bounds match exactly."""
    if not len(lower):
        return lower, upper
    other = [dimension for dimension in range(3) if dimension != axis]
    # Group identical cross-sections, then sort intervals along the merge axis.
    keys = [lower[:, axis]]
    for dimension in other:
        keys.extend((lower[:, dimension], upper[:, dimension]))
    order = np.lexsort(tuple(keys))
    lower, upper = lower[order], upper[order]
    connected = (np.all(lower[1:, other] == lower[:-1, other], axis=1)
                 & np.all(upper[1:, other] == upper[:-1, other], axis=1)
                 & (lower[1:, axis] == upper[:-1, axis]))
    starts = np.r_[0, np.flatnonzero(~connected) + 1]
    ends = np.r_[starts[1:] - 1, len(lower) - 1]
    return lower[starts], upper[ends]


def merge_collision_voxels(cells):
    """Return disjoint boxes with exactly the input voxel union.

    Bounds are integer voxel coordinates: lower inclusive, upper exclusive.
    Each pass joins full matching faces, so holes and external boundaries are
    preserved. Try all six axis orders and retain the fewest resulting boxes.
    This is a greedy partition, not a minimum-box solver.
    """
    cells = np.asarray(cells)
    if cells.ndim != 2 or cells.shape[1] != 3 or cells.dtype.kind not in 'iu':
        raise ValueError('Collision cells must be an integer array of shape (N, 3)')
    lower = np.unique(cells.astype(np.int64), axis=0)
    upper = lower + 1
    best_lower, best_upper = lower, upper
    best_order = 'xyz'
    counts = {}
    for axes in itertools.permutations(range(3)):
        merged_lower, merged_upper = lower, upper
        for axis in axes:
            merged_lower, merged_upper = _merge_boxes_along_axis(
                merged_lower, merged_upper, axis)
        name = ''.join('xyz'[axis] for axis in axes)
        counts[name] = len(merged_lower)
        if len(merged_lower) < len(best_lower):
            best_lower, best_upper, best_order = merged_lower, merged_upper, name
    return best_lower, best_upper, {
        'strategy': 'exact face-adjacent cuboids; fewest boxes of six axis orders',
        'axis_order': best_order, 'candidate_box_counts': counts,
    }


def write_ply(path, xyz, rgb):
    points = np.empty(len(xyz), dtype=[('x', '<f4'), ('y', '<f4'), ('z', '<f4'),
                                      ('red', 'u1'), ('green', 'u1'), ('blue', 'u1')])
    for i, name in enumerate(('x', 'y', 'z')):
        points[name] = xyz[:, i]
    for i, name in enumerate(('red', 'green', 'blue')):
        points[name] = rgb[:, i]
    with path.open('wb') as stream:
        stream.write(('ply\nformat binary_little_endian 1.0\nelement vertex '
                      f'{len(xyz)}\nproperty float x\nproperty float y\nproperty float z\n'
                      'property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n').encode())
        points.tofile(stream)


def colored_meshes(asset, world, xyz, rgb, output, voxel):
    # Group small octahedra by RGB palette: hundreds of draw geometries instead
    # of hundreds of thousands of independent MuJoCo sphere geometries.
    palette = rgb.astype(np.int32) // 32
    keys = palette[:, 0] * 64 + palette[:, 1] * 8 + palette[:, 2]
    offsets = np.array([[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0],
                        [0, 0, 1], [0, 0, -1]], dtype=np.float32) * (voxel * .72)
    triangles = np.array([[0, 2, 4], [2, 1, 4], [1, 3, 4], [3, 0, 4],
                          [2, 0, 5], [1, 2, 5], [3, 1, 5], [0, 3, 5]])
    mesh_dir = output / 'scene_meshes'
    mesh_dir.mkdir(exist_ok=True)
    for key in np.unique(keys):
        selected = keys == key
        points = xyz[selected]
        vertices = points[:, None, :] + offsets[None, :, :]
        # Preserve a thin colored surface for near-horizontal ground points.
        flat = np.abs(points[:, 2]) < .18
        vertices[flat, :, 2] = points[flat, None, 2] + offsets[None, :, 2] * .025
        faces = (triangles[None, :, :] + np.arange(len(points))[:, None, None] * 6 + 1)
        path = mesh_dir / f'color_{key:03d}.obj'
        with path.open('w') as stream:
            np.savetxt(stream, vertices.reshape(-1, 3), fmt='v %.5f %.5f %.5f')
            np.savetxt(stream, faces.reshape(-1, 3), fmt='f %d %d %d')
        name = f'cloud_color_{key}'
        ET.SubElement(asset, 'mesh', name=name, file=str(path), maxhullvert='4')
        color = np.mean(rgb[selected], axis=0) / 255
        ET.SubElement(world, 'geom', name=name, type='mesh', mesh=name,
                      rgba=' '.join(map(str, [*color, 1])), contype='0', conaffinity='0', group='2')
    return len(np.unique(keys))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pcd', type=Path)
    parser.add_argument('--model-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--spawn-xy', type=float, nargs=2, required=True)
    parser.add_argument('--visual-voxel', type=float, default=.25)
    parser.add_argument('--collision-voxel', type=float, default=.6)
    parser.add_argument('--gpu-only', action='store_true',
                        help='Skip legacy octahedron display meshes; use the full-point GPU viewer')
    parser.add_argument('--merge-collision-boxes', action='store_true',
                        help='Merge adjacent obstacle voxels into boxes with exactly the same occupied union')
    args = parser.parse_args()
    if not all(np.isfinite(v) and v > 0 for v in (args.visual_voxel, args.collision_voxel)):
        parser.error('Voxel sizes must be positive and finite')
    if not np.isfinite(args.spawn_xy).all():
        parser.error('Spawn coordinates must be finite')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cloud, header = read_pcd(args.pcd.resolve())
    spawn = np.asarray(args.spawn_xy)
    patches = []
    for start in range(0, len(cloud), 1_000_000):
        block = cloud[start:start + 1_000_000]
        near = ((block['x'] - spawn[0]) ** 2 + (block['y'] - spawn[1]) ** 2) < 1.0
        patches.append(block['z'][near])
    ground_samples = np.concatenate(patches)
    ground_samples = ground_samples[np.isfinite(ground_samples)]
    if len(ground_samples) < 100:
        raise ValueError('Spawn needs at least 100 ground points within 1 m')
    ground_z = float(np.median(ground_samples))
    roughness = float(np.quantile(ground_samples, .95) - np.quantile(ground_samples, .05))
    if roughness > .12:
        raise ValueError(f'Spawn patch is not flat/clear: vertical spread {roughness:.3f} m')
    origin = np.array([*spawn, ground_z])
    pieces, colors = [], []
    finite_count = 0
    for start in range(0, len(cloud), 1_000_000):
        block = cloud[start:start + 1_000_000]
        xyz = np.column_stack([block[name] for name in ('x', 'y', 'z')])
        finite = np.isfinite(xyz).all(axis=1)
        finite_count += int(finite.sum())
        xyz = (xyz[finite] - origin).astype(np.float32)
        packed = block['rgb'][finite]
        if packed.dtype.kind == 'f':
            if packed.dtype.itemsize != 4:
                raise ValueError('Packed floating rgb must be float32')
            packed = packed.view('<u4')
        keep = voxel_indices(xyz, args.visual_voxel)
        pieces.append(xyz[keep])
        colors.append(np.column_stack([(packed[keep] >> shift) & 255 for shift in (16, 8, 0)]).astype('u1'))
        print(f'Processed {min(start + len(block), len(cloud)):,}/{len(cloud):,} points', flush=True)
    xyz, rgb = np.concatenate(pieces), np.concatenate(colors)
    del pieces, colors
    keep = voxel_indices(xyz, args.visual_voxel)
    xyz, rgb = xyz[keep], rgb[keep]
    write_ply(output / 'scene_local.ply', xyz, rgb)

    robot_file = args.model_root.resolve() / 'go2/unitree_robots/go2/go2.xml'
    tree = ET.parse(robot_file)
    root = tree.getroot()
    root.set('model', 'Go2 in imported ' + args.pcd.name)
    root.find('compiler').set('meshdir', str(robot_file.parent / 'assets'))
    asset, world = root.find('asset'), root.find('worldbody')
    ET.SubElement(root, 'statistic', center='0 0 0.3', extent='3')
    visual = ET.SubElement(root, 'visual')
    ET.SubElement(visual, 'headlight', ambient='.5 .5 .5', diffuse='.6 .6 .6', specular='0 0 0')
    ET.SubElement(visual, 'map', znear='.005', zfar='200')
    ET.SubElement(visual, 'global', offwidth='1600', offheight='1000')
    ET.SubElement(asset, 'texture', name='sky', type='skybox', builtin='gradient',
                  rgb1='.35 .48 .62', rgb2='.85 .89 .93', width='512', height='3072')
    ET.SubElement(world, 'light', pos='0 0 10', dir='0 0 -1', directional='true', castshadow='false')
    # The GPU viewer draws the cloud separately and only needs robot geometry.
    # Save before adding static collision bodies; joint/free-base layout is equal.
    tree.write(output / 'go2_viewer.xml', encoding='utf-8', xml_declaration=True)

    # Supported ground cells only; holes in the scan stay holes. Use medians of
    # measured surface elevations rather than inserting an infinite plane.
    ground = xyz[np.abs(xyz[:, 2]) < .18]
    cell = args.collision_voxel
    cells = np.floor(ground[:, :2] / cell).astype(np.int32)
    unique, inverse = np.unique(cells, axis=0, return_inverse=True)
    order = np.argsort(inverse)
    groups = np.split(order, np.flatnonzero(np.diff(inverse[order])) + 1)
    for i, indices in enumerate(groups):
        cx, cy = (unique[i] + .5) * cell
        height = float(np.median(ground[indices, 2]))
        ET.SubElement(world, 'geom', name=f'ground_{i}', type='box',
                      pos=f'{cx} {cy} {height - .10}', size=f'{cell/2} {cell/2} .10',
                      contype='1', conaffinity='1', condim='3', friction='.8 .02 .01',
                      rgba='0 0 0 0', group='3')
    obstacles = xyz[np.abs(xyz[:, 2]) >= .18]
    obstacle_cells = np.unique(np.floor(obstacles / cell).astype(np.int32), axis=0)
    if args.merge_collision_boxes:
        obstacle_lower, obstacle_upper, merge_info = merge_collision_voxels(obstacle_cells)
        obstacle_centers = (obstacle_lower + obstacle_upper) * (cell / 2)
        obstacle_sizes = (obstacle_upper - obstacle_lower) * (cell / 2)
        print(f'Merged {len(obstacle_cells):,} occupied obstacle voxels into '
              f'{len(obstacle_lower):,} exact boxes ({merge_info["axis_order"]})', flush=True)
    else:
        obstacle_centers = (obstacle_cells + .5) * cell
        obstacle_sizes = np.full((len(obstacle_cells), 3), cell / 2)
        merge_info = {'strategy': 'one box per occupied obstacle voxel'}
    for i, (center, size) in enumerate(zip(obstacle_centers, obstacle_sizes)):
        ET.SubElement(world, 'geom', name=f'obstacle_{i}', type='box',
                      pos=' '.join(map(str, center)), size=' '.join(map(str, size)),
                      contype='1', conaffinity='1', condim='3', friction='.8 .02 .01',
                      rgba='0 0 0 0', group='3')
    # Save physics without cloud meshes so the headless process need not parse
    # visual resources. Robot visual assets remain governed by discard_visual.
    tree.write(output / 'go2_physics.xml', encoding='utf-8', xml_declaration=True)
    mesh_count = (0 if args.gpu_only else
                  colored_meshes(asset, world, xyz, rgb, output, args.visual_voxel))
    tree.write(output / 'go2_scene.xml', encoding='utf-8', xml_declaration=True)
    world_config = [{'name': 'go2', 'namespace': '/sim/go2',
                     'model_path': str(output / 'go2_physics.xml'), 'kp': 40., 'kd': 2.}]
    params = {'chaos_simulator': {'ros__parameters': {
        'worlds_json': json.dumps(world_config), 'discard_visual': True,
        'publish_rate_hz': 50., 'command_timeout_s': .5}}}
    # JSON is a YAML subset, accepted as a ROS parameter file.
    (output / 'simulator.yaml').write_text(json.dumps(params, indent=2) + '\n')
    metadata = {'source': str(args.pcd.resolve()), 'source_points': len(cloud),
                'finite_points': finite_count, 'visual_points': len(xyz),
                'proxy_points': len(xyz),
                'visual_points_note': 'Legacy proxy sample count; complete GPU count is in cloud_gpu.json',
                'visual_meshes': mesh_count, 'visual_voxel_m': args.visual_voxel,
                'legacy_display_meshes': not args.gpu_only,
                'viewer_model': str(output / 'go2_viewer.xml'),
                'collision_voxel_m': cell, 'ground_boxes': len(unique),
                'ground_occupied_cells': len(unique),
                'obstacle_occupied_voxels': len(obstacle_cells),
                'obstacle_boxes': len(obstacle_centers),
                'collision_box_merging': merge_info,
                'ground_box_merging': 'none; preserve each measured median height exactly',
                'local_origin_in_pcd': origin.tolist(),
                'coordinate_transform': 'local_xyz = pcd_xyz - local_origin_in_pcd',
                'pcd_viewpoint': header.get('VIEWPOINT'), 'viewpoint_applied': False,
                'spawn_ground_90_percent_spread_m': roughness,
                'local_bounds': [xyz.min(axis=0).tolist(), xyz.max(axis=0).tolist()],
                'collision_method': 'Observed ground median-height tiles within +/-0.18 m; '
                                    f'{cell:g} m occupied obstacle grid outside that band. '
                                    'Box grouping preserves its exact occupied union; scan holes remain holes.',
                'controller': 'Go2 home posture and joint PD; no locomotion policy'}
    (output / 'scene_metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    main()
