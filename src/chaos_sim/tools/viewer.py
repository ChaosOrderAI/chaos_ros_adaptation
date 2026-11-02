#!/usr/bin/env python3
"""Render ROS simulation observations; never step physics or publish controls."""
import argparse
import math
from pathlib import Path
import sys
import threading
import time

import mujoco
import mujoco.viewer
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from nav_msgs.msg import Odometry
from sensor_msgs.msg import JointState

MODELS = {'go2': 'go2/unitree_robots/go2/scene.xml',
          't2': 't2/robots/T2/T2_31dof/T2_31dof.xml'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--robot', choices=MODELS, default='go2')
    parser.add_argument('--model-root', type=Path, default=Path('~/.cache/chaos/mujoco'))
    parser.add_argument('--namespace', help='default: /sim/<robot>')
    args, ros_args = parser.parse_known_args()
    # This process compiles one model; release the unused mesh compilation cache.
    mujoco.mj_setCacheCapacity(mujoco.mj_getCache(), 0)
    model = mujoco.MjModel.from_xml_path(str(args.model_root.expanduser() / MODELS[args.robot]))
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
    rclpy.init(args=ros_args)
    node = Node('chaos_mujoco_viewer_' + args.robot)
    ns = args.namespace or '/sim/' + args.robot
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
        if message.header.frame_id != args.robot + '/odom' or message.child_frame_id != args.robot + '/base_link':
            raise ValueError('odom frames do not match selected robot')
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
