#!/usr/bin/env python3
"""Clone one Go2 into several floating roots that share the scene geoms.

The collision map is not copied. Each clone is offset in the worldframe by a
layout (``line`` along -X, ``grid`` rows along -Y, ``ring`` around the spawn)
while keeping its own spawn orientation. Joints, sites, motors and sensors are
prefixed ``<body>/`` so each robot can be addressed on its own. Mesh files and
default classes stay shared.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def robot_names(count, base):
    if count == 1:
        return [base]
    return [base] + [f'{base}_{index}' for index in range(2, count + 1)]


def local_names(body):
    return {elem.get('name') for elem in body.iter() if elem.get('name')}


def is_floating(body):
    return any(elem.tag == 'freejoint' or (elem.tag == 'joint' and elem.get('type') == 'free')
               for elem in body.iter())


def offsets(layout, count, spacing):
    """World-frame (x, y) offsets for robots 1..count-1; robot 0 stays put."""
    out = []
    for index in range(1, count):
        if layout == 'line':
            out.append((-index * spacing, 0.0))
        elif layout == 'ring':
            angle = 2 * math.pi * index / count
            out.append((spacing * (math.cos(angle) - 1.0), spacing * math.sin(angle)))
        else:  # grid
            columns = 3
            row, column = divmod(index - 1, columns)
            out.append((-(column + 1) * spacing, -row * spacing))
    return out


def shift(body, offset):
    pos = body.get('pos', '0 0 0').split()
    if len(pos) != 3:
        raise ValueError('robot body pos must be x y z')
    pos[0] = f'{float(pos[0]) + offset[0]:.8g}'
    pos[1] = f'{float(pos[1]) + offset[1]:.8g}'
    body.set('pos', ' '.join(pos))


# Attributes that reference a named element of the same subtree (not asset
# attributes like mesh/material/class which stay shared).
REFERENCE_ATTRS = {'joint', 'site', 'objname', 'body', 'target', 'anchor',
                   'body1', 'body2', 'slidesite', 'cranksite', 'tendon'}


def rewrite(elem, prefix, names, rename_self):
    for attr, value in list(elem.attrib.items()):
        if attr == 'name' and rename_self:
            elem.set(attr, prefix + value)
        elif attr in REFERENCE_ATTRS and value in names:
            elem.set(attr, prefix + value)
    for child in list(elem):
        rewrite(child, prefix, names, True)


def references_robot(elem, names):
    # An element belongs to the template robot when it references one of the
    # robot's own names through a reference attribute (joint=, site=, objname=)
    # or declares one itself. Matching by bare attribute value would steal
    # scene elements that happen to share a name.
    for node in elem.iter():
        for attr, value in node.attrib.items():
            if value in names and (attr == 'name' or attr in REFERENCE_ATTRS):
                return True
    return False


def expand_key(key, deltas):
    qpos = key.get('qpos', '').split()
    if len(qpos) < 7:
        raise ValueError('home keyframe has no floating pose')
    free, hinges = qpos[:7], qpos[7:]
    origin = (float(free[0]), float(free[1]))
    pieces = []
    for dx, dy in [(0.0, 0.0)] + deltas:
        pieces.append(f'{origin[0] + dx:.8g}')
        pieces.append(f'{origin[1] + dy:.8g}')
        pieces.extend(free[2:])
        pieces.extend(hinges)
    key.set('qpos', ' '.join(pieces))
    for attr in ('ctrl', 'qvel'):
        values = key.get(attr)
        if not values:
            continue
        parts = values.split()
        key.set(attr, ' '.join(parts * (len(deltas) + 1)))


def compose(source, destination, names, deltas, base):
    tree = ET.parse(source)
    root = tree.getroot()
    world = root.find('worldbody')
    if world is None:
        raise ValueError(f'{source}: missing worldbody')
    templates = [body for body in list(world) if body.tag == 'body' and is_floating(body)]
    if len(templates) != 1:
        raise ValueError(f'{source}: expected one floating robot, found {len(templates)}')
    template = templates[0]
    local = local_names(template)
    if base in local:
        raise ValueError(f'{source}: robot already uses the fleet body name {base!r}')
    index = list(world).index(template)
    world.remove(template)
    for robot_index, robot in enumerate(names):
        body = copy.deepcopy(template)
        if robot_index:
            shift(body, deltas[robot_index - 1])
        rewrite(body, robot + '/', local, False)
        body.set('name', robot)
        world.insert(index + robot_index, body)
    for section_name in ('actuator', 'sensor', 'tendon', 'equality', 'contact'):
        section = root.find(section_name)
        if section is None:
            continue
        owned = [child for child in list(section) if references_robot(child, local)]
        for child in owned:
            section.remove(child)
        for robot in names:
            for child in owned:
                clone = copy.deepcopy(child)
                rewrite(clone, robot + '/', local, True)
                section.append(clone)
    keys = root.findall('keyframe/key')
    home = [key for key in keys if key.get('name') == 'home']
    if len(home) != 1:
        raise ValueError(f'{source}: expected one home keyframe')
    for key in keys:
        expand_key(key, deltas)
    destination.parent.mkdir(parents=True, exist_ok=True)
    tree.write(destination, encoding='utf-8', xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir', type=Path, required=True)
    parser.add_argument('--count', type=int, required=True)
    parser.add_argument('--manifest', type=Path,
                        help='write the manifest here instead of <scene-dir>/robots.json')
    parser.add_argument('--base-name', default='go2',
                        help='fleet prefix; the first robot keeps it, the rest add _2, _3 ...')
    parser.add_argument('--layout', choices=('line', 'ring', 'grid'), default='line',
                        help='line: single file along -X; ring: circle around spawn; grid: rows of 3')
    parser.add_argument('--spacing', type=float, default=1.0)
    parser.add_argument('--namespace-style', choices=('nested', 'flat'), default='nested',
                        help='nested: /sim/go2 + /sim/go2/go2_2 ... ; flat: /sim/go2 + /sim/go2_2 ...')
    parser.add_argument('--if-needed', action='store_true',
                        help='skip when the multi XML and manifest already cover this request')
    args = parser.parse_args()
    if not 2 <= args.count <= 8 or not 0.5 <= args.spacing <= 5:
        parser.error('--count must be 2..8 and --spacing 0.5..5 metres')
    scene = args.scene_dir.expanduser().resolve()
    physics = scene / 'go2_physics.xml'
    if not physics.is_file():
        parser.error(f'missing {physics}')
    manifest_path = (args.manifest.expanduser().resolve() if args.manifest
                     else scene / 'robots.json')
    # A custom --manifest only relocates the manifest; the model outputs keep
    # their canonical _multi names so the run script finds them.
    physics_out = scene / 'go2_physics_multi.xml'
    viewer_out = scene / 'go2_viewer_multi.xml'
    request = {'count': args.count, 'base_name': args.base_name, 'layout': args.layout,
               'spacing_m': args.spacing, 'namespace_style': args.namespace_style}
    if args.if_needed and manifest_path.is_file() and physics_out.is_file():
        try:
            manifest = json.loads(manifest_path.read_text())
            if manifest.get('request') == request and len(manifest.get('robots', [])) == args.count:
                print(f'Shared scene is current: {args.count} robots', flush=True)
                return
        except (OSError, ValueError, TypeError):
            pass
    names = robot_names(args.count, args.base_name)
    deltas = offsets(args.layout, args.count, args.spacing)
    compose(physics, physics_out, names, deltas, args.base_name)
    viewer = scene / 'go2_viewer.xml'
    if viewer.is_file():
        compose(viewer, viewer_out, names, deltas, args.base_name)
    robot_namespaces = {}
    robots = []
    for index, name in enumerate(names):
        if args.namespace_style == 'flat' or index == 0:
            namespace = '/sim/' + name
        else:
            namespace = '/sim/' + names[0] + '/' + name
        robots.append({'name': name, 'namespace': namespace})
        robot_namespaces[name] = namespace
    manifest_path.write_text(json.dumps({
        'robots': robots, 'robot_namespaces': robot_namespaces, 'request': request},
        indent=2) + '\n')
    print(f'Shared scene: {args.count} robots ({", ".join(names)}), '
          f'{args.layout} spacing {args.spacing:g} m, namespaces {args.namespace_style}', flush=True)


if __name__ == '__main__':
    main()
