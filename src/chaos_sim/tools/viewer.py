#!/usr/bin/env python3
"""Render ROS simulation observations; never step physics or publish controls."""
import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time

import mujoco
import mujoco.viewer
from ament_index_python.packages import get_package_prefix
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from nav_msgs.msg import Odometry
from sensor_msgs.msg import JointState

MODELS = {'go2': 'go2/unitree_robots/go2/scene.xml',
          't2': 't2/robots/T2/T2_31dof/T2_31dof.xml'}


SCENE_DEFAULTS = {'voxel': 0.1, 'radius': 0.03, 'max_geoms': 20000, 'collision': True}


def simulator_scene_options(node, instance_name):
    """Match a running simulator when the viewer flags were left unset."""
    from rcl_interfaces.srv import GetParameters
    client = node.create_client(GetParameters, '/chaos_simulator/get_parameters')
    if not client.wait_for_service(timeout_sec=0.3):
        return None
    request = GetParameters.Request()
    request.names = ['worlds_json']
    future = client.call_async(request)
    rclpy.spin_until_future_complete(node, future, timeout_sec=0.5)
    result = future.result()
    if result is None or not result.values or not result.values[0].string_value:
        return None
    try:
        worlds = json.loads(result.values[0].string_value)
    except json.JSONDecodeError:
        return None
    for world in worlds:
        if world.get('name') == instance_name:
            return {
                'voxel': float(world.get('scene_voxel_size', SCENE_DEFAULTS['voxel'])),
                'radius': float(world.get('scene_point_radius', SCENE_DEFAULTS['radius'])),
                'max_geoms': int(world.get('scene_max_geoms', SCENE_DEFAULTS['max_geoms'])),
                'collision': bool(world.get('scene_collision', SCENE_DEFAULTS['collision'])),
            }
    return None


def resolve_scene_options(node, args, instance_name):
    options = dict(SCENE_DEFAULTS)
    inherited = None
    if args.scene is not None and None in (
            args.scene_voxel_size, args.scene_point_radius, args.scene_max_geoms, args.scene_collision):
        inherited = simulator_scene_options(node, instance_name)
        if inherited:
            options.update(inherited)
            print('Scene options inherited from chaos_simulator:', inherited)
    if args.scene_voxel_size is not None:
        options['voxel'] = args.scene_voxel_size
    if args.scene_point_radius is not None:
        options['radius'] = args.scene_point_radius
    if args.scene_max_geoms is not None:
        options['max_geoms'] = args.scene_max_geoms
    if args.scene_collision is not None:
        options['collision'] = args.scene_collision
    if options['max_geoms'] <= 0:
        raise ValueError('scene_max_geoms must be positive')
    return options


def load_model(args, options):
    robot_model = args.model_root.expanduser() / MODELS[args.robot]
    if args.scene is None:
        return mujoco.MjModel.from_xml_path(str(robot_model))
    executable = Path(get_package_prefix('chaos_sim')) / 'lib/chaos_sim/scene_import'
    with tempfile.TemporaryDirectory(prefix='chaos-scene-viewer-') as directory:
        output = Path(directory) / 'composed.xml'
        command = [str(executable), str(robot_model), str(args.scene.expanduser().resolve()), str(output),
                   '--voxel-size', str(options['voxel']),
                   '--point-radius', str(options['radius']),
                   '--max-geoms', str(options['max_geoms'])]
        if not options['collision']:
            command.append('--no-collision')
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError('scene import failed: ' + result.stderr.strip())
        print('Scene imported: ' + result.stdout.strip())
        return mujoco.MjModel.from_xml_path(str(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--robot', choices=MODELS, default='go2')
    parser.add_argument('--name', help='simulator instance name; default: value of --robot')
    parser.add_argument('--model-root', type=Path, default=Path('~/.cache/chaos/mujoco'))
    parser.add_argument('--namespace', help='default: /sim/<name>')
    parser.add_argument('--scene', type=Path, help='PLY/PCD point cloud or 3D Gaussian PLY scene')
    parser.add_argument('--scene-voxel-size', type=float,
                        help='default: running simulator, else 0.1')
    parser.add_argument('--scene-point-radius', type=float,
                        help='default: running simulator, else 0.03')
    parser.add_argument('--scene-max-geoms', type=int,
                        help='default: running simulator, else 20000')
    parser.add_argument('--scene-collision', action=argparse.BooleanOptionalAction, default=None,
                        help='default: running simulator, else true')
    args, ros_args = parser.parse_known_args()
    instance_name = args.name or args.robot
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', instance_name):
        parser.error('--name must be a valid ROS name')
    rclpy.init(args=ros_args)
    node = Node('chaos_mujoco_viewer_' + instance_name)
    try:
        options = resolve_scene_options(node, args, instance_name)
    except ValueError as error:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
        parser.error(str(error))
    # This process compiles one model; release the unused mesh compilation cache.
    mujoco.mj_setCacheCapacity(mujoco.mj_getCache(), 0)
    model = load_model(args, options)
    data = mujoco.MjData(model)
    free_joints = [i for i in range(model.njnt) if int(model.jnt_type[i]) == int(mujoco.mjtJoint.mjJNT_FREE)]
    if len(free_joints) != 1:
        raise ValueError('viewer requires exactly one floating base')
    root_qpos = int(model.jnt_qposadr[free_joints[0]])
    addresses = {}
    for actuator in range(model.nu):
        joint = int(model.actuator_trnid[actuator, 0])
        name = mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_JOINT, joint)
        if not name or int(model.jnt_type[joint]) not in (int(mujoco.mjtJoint.mjJNT_HINGE), int(mujoco.mjtJoint.mjJNT_SLIDE)):
            raise ValueError('viewer requires named scalar motor joints')
        addresses[name] = int(model.jnt_qposadr[joint])
    ns = args.namespace or '/sim/' + instance_name
    latest = {'joints': None, 'odom': None, 'mapped': False}
    expected_names = set(addresses)
    mapping = []

    def joints(message):
        if not latest['mapped']:
            if len(message.name) != len(set(message.name)) or set(message.name) != expected_names:
                raise ValueError('joint_states names do not match the selected visual model')
            mapping.extend(addresses[name] for name in message.name)
            latest['mapped'] = True
        elif message.name != latest['joints'].name:
            raise ValueError('joint_states ordering changed')
        if len(message.position) != len(mapping) or not all(math.isfinite(x) for x in message.position):
            raise ValueError('joint_states contains invalid positions')
        latest['joints'] = message

    def odom(message):
        if message.header.frame_id != instance_name + '/odom' or message.child_frame_id != instance_name + '/base_link':
            raise ValueError('odom frames do not match selected instance')
        latest['odom'] = message

    subscriptions = [node.create_subscription(JointState, ns + '/joint_states', joints, qos_profile_sensor_data),
                     node.create_subscription(Odometry, ns + '/odom', odom, qos_profile_sensor_data)]
    viewer_threads = []
    try:
        deadline = time.monotonic() + 15
        while rclpy.ok() and (latest['joints'] is None or latest['odom'] is None):
            if time.monotonic() > deadline:
                raise TimeoutError(f'no complete state on {ns}; source ROS and start the simulator in the same domain')
            rclpy.spin_once(node, timeout_sec=0.1)
        if not rclpy.ok():
            return 0
        # ROS initialization is complete; this call alone starts new Python threads.
        previous_threads = set(threading.enumerate())
        with mujoco.viewer.launch_passive(model, data) as viewer:
            viewer_threads = [thread for thread in threading.enumerate() if thread not in previous_threads]
            while rclpy.ok() and viewer.is_running():
                # Callbacks and qpos updates run on this one Python thread.
                rclpy.spin_once(node, timeout_sec=0.01)
                with viewer.lock():
                    for address, value in zip(mapping, latest['joints'].position):
                        data.qpos[address] = value
                    pose = latest['odom'].pose.pose
                    data.qpos[root_qpos:root_qpos + 3] = (pose.position.x, pose.position.y, pose.position.z)
                    data.qpos[root_qpos + 3:root_qpos + 7] = (pose.orientation.w, pose.orientation.x,
                                                           pose.orientation.y, pose.orientation.z)
                    mujoco.mj_forward(model, data)
                viewer.sync()
    except KeyboardInterrupt:
        pass
    finally:
        # launch_passive closes asynchronously; finish its renderer before GLFW's atexit cleanup.
        for thread in viewer_threads:
            thread.join(timeout=5)
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
        if any(thread.is_alive() for thread in viewer_threads):
            raise RuntimeError('MuJoCo viewer renderer did not stop within 5 seconds')
    return 0


if __name__ == '__main__':
    sys.exit(main())
