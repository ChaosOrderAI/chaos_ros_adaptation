#!/usr/bin/env python3
"""Clone one Go2 into several floating roots that share the scene geoms.

The collision map is not copied. Robot 0 keeps the spawn pose and is named
``go2``; ``go2_2`` onward stand behind it on the path, one metre apart.
Joints, sites, motors and sensors are prefixed so each robot can be addressed
on its own. Mesh files and default classes stay shared.
"""
import argparse
import copy
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def robot_names(count):
    return ['go2'] + [f'go2_{index}' for index in range(2, count + 1)]


def local_names(body):
    return {elem.get('name') for elem in body.iter() if elem.get('name')}


def is_floating(body):
    return any(elem.tag == 'freejoint' or (elem.tag == 'joint' and elem.get('type') == 'free')
               for elem in body.iter())


def shift(body, distance):
    pos = body.get('pos', '0 0 0').split()
    if len(pos) != 3:
        raise ValueError('robot body pos must be x y z')
    pos[0] = f'{float(pos[0]) + distance:.8g}'
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


def expand_key(key, count, spacing):
    qpos = key.get('qpos', '').split()
    if len(qpos) < 7:
        raise ValueError('home keyframe has no floating pose')
    free, hinges = qpos[:7], qpos[7:]
    origin = float(free[0])
    pieces = []
    for index in range(count):
        pieces.append(f'{origin - index * spacing:.8g}')
        pieces.extend(free[1:])
        pieces.extend(hinges)
    key.set('qpos', ' '.join(pieces))
    for attr in ('ctrl', 'qvel'):
        values = key.get(attr)
        if not values:
            continue
        parts = values.split()
        key.set(attr, ' '.join(parts * count))


def compose(source, destination, count, spacing):
    tree = ET.parse(source)
    root = tree.getroot()
    world = root.find('worldbody')
    if world is None:
        raise ValueError(f'{source}: missing worldbody')
    templates = [body for body in list(world) if body.tag == 'body' and is_floating(body)]
    if len(templates) != 1:
        raise ValueError(f'{source}: expected one floating robot, found {len(templates)}')
    template = templates[0]
    names = local_names(template)
    if 'go2' in names:
        raise ValueError(f'{source}: robot already uses the fleet body name')
    index = list(world).index(template)
    world.remove(template)
    fleet = robot_names(count)
    for robot_index, robot in enumerate(fleet):
        body = copy.deepcopy(template)
        shift(body, -robot_index * spacing)
        rewrite(body, robot + '/', names, False)
        body.set('name', robot)
        world.insert(index + robot_index, body)
    for section_name in ('actuator', 'sensor', 'tendon', 'equality', 'contact'):
        section = root.find(section_name)
        if section is None:
            continue
        owned = [child for child in list(section) if references_robot(child, names)]
        for child in owned:
            section.remove(child)
        for robot in fleet:
            for child in owned:
                clone = copy.deepcopy(child)
                rewrite(clone, robot + '/', names, True)
                section.append(clone)
    keys = root.findall('keyframe/key')
    home = [key for key in keys if key.get('name') == 'home']
    if len(home) != 1:
        raise ValueError(f'{source}: expected one home keyframe')
    for key in keys:
        expand_key(key, count, spacing)
    destination.parent.mkdir(parents=True, exist_ok=True)
    tree.write(destination, encoding='utf-8', xml_declaration=True)
    return fleet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir', type=Path, required=True)
    parser.add_argument('--count', type=int, required=True)
    parser.add_argument('--spacing', type=float, default=1.0)
    args = parser.parse_args()
    if not 2 <= args.count <= 8 or not 0.5 <= args.spacing <= 5:
        parser.error('--count must be 2..8 and --spacing 0.5..5 metres')
    scene = args.scene_dir.expanduser().resolve()
    physics = scene / 'go2_physics.xml'
    if not physics.is_file():
        parser.error(f'missing {physics}')
    names = compose(physics, scene / 'go2_physics_multi.xml', args.count, args.spacing)
    viewer = scene / 'go2_viewer.xml'
    if viewer.is_file():
        compose(viewer, scene / 'go2_viewer_multi.xml', args.count, args.spacing)
    manifest = {
        # The robot sharing the entry name keeps /sim/go2; the others extend it.
        'robots': [{'name': names[0], 'namespace': '/sim/' + names[0]}] +
                  [{'name': name, 'namespace': '/sim/' + names[0] + '/' + name}
                   for name in names[1:]],
        'spacing_m': args.spacing,
    }
    (scene / 'robots.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Shared scene: {args.count} robots ({", ".join(names)}), spacing {args.spacing:g} m',
          flush=True)


if __name__ == '__main__':
    main()
