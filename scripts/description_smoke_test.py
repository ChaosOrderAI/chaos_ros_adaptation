#!/usr/bin/env python3
"""Check URDF publication and multi-robot TF with synthetic robots on localhost.

Only this test publishes synthetic joint positions; normal bringup never creates
joint states. No vendor driver, real robot, USB device or broker is accessed.
"""

import math
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_share_directory
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, qos_profile_sensor_data
from sensor_msgs.msg import Imu, JointState
from std_msgs.msg import String
from tf2_msgs.msg import TFMessage
import yaml

from chaos_interfaces.msg import RobotState


def run():
    os.environ['ROS_DOMAIN_ID'] = '191'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    with tempfile.TemporaryDirectory(prefix='chaos-description-smoke-') as directory:
        root = Path(directory)
        share = Path(get_package_share_directory('chaos_assets'))
        data = yaml.safe_load((share / 'config/description.demo.yaml').read_text())
        data['hosts'] = {'smoke': {'ros_domain_id': 191}}
        for asset in data['assets']:
            asset['host'], asset['mode'] = 'smoke', 'mock'
        path = root / 'assets.yaml'
        path.write_text(yaml.safe_dump(data))
        os.environ['ROS_LOG_DIR'] = str(root / 'ros-log')
        logfile = (root / 'launch.log').open('w+')
        process = subprocess.Popen(
            ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
             f'inventory:={path}', 'host_id:=smoke'],
            stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
        node = None
        try:
            rclpy.init()
            node = Node('chaos_description_smoke')
            fixed, moving, descriptions, states, imu = {}, {}, {}, {}, {}
            subscriptions, publishers = [], {}

            def save_transforms(message, target):
                for transform in message.transforms:
                    target[transform.child_frame_id] = transform

            subscriptions.append(node.create_subscription(
                TFMessage, '/tf_static', lambda msg: save_transforms(msg, fixed),
                QoSProfile(depth=100, durability=DurabilityPolicy.TRANSIENT_LOCAL)))
            subscriptions.append(node.create_subscription(
                TFMessage, '/tf', lambda msg: save_transforms(msg, moving), 100))
            for identity in ('demo_001', 'demo_002'):
                subscriptions.append(node.create_subscription(
                    RobotState, f'/robots/{identity}/state',
                    lambda msg, identity=identity: states.__setitem__(identity, msg), 10))
                subscriptions.append(node.create_subscription(
                    Imu, f'/robots/{identity}/sensors/body_imu/data',
                    lambda msg, identity=identity: imu.__setitem__(identity, msg),
                    qos_profile_sensor_data))

            def wait(predicate, timeout=20):
                deadline = time.monotonic() + timeout
                while not predicate():
                    if process.poll() is not None:
                        raise AssertionError(f'launch exited early: {process.returncode}')
                    if time.monotonic() >= deadline:
                        raise AssertionError('timed out waiting for model/TF data')
                    rclpy.spin_once(node, timeout_sec=0.03)

            wait(lambda: len(states) == 2 and len(imu) == 2 and all(
                identity + '/body_imu_link' in fixed for identity in states))
            for identity in states:
                transform = fixed[identity + '/body_imu_link']
                assert transform.header.frame_id == identity + '/base_link'
                assert abs(transform.transform.translation.x - 0.1) < 1e-6
                assert imu[identity].header.frame_id == transform.child_frame_id
                assert states[identity].ready
            assert not moving, 'bringup must not invent movable joint positions'
            names = node.get_node_names_and_namespaces()
            assert sum(name == 'robot_state_publisher' for name, _ in names) == 2
            assert not any(name.endswith('_static_tf') for name, _ in names)
            print('PASS: two namespaced models, unique prefixed static TF, URDF-owned IMU mounts')

            # Subscribe after static transforms arrived, proving late discovery.
            for identity in ('demo_001', 'demo_002'):
                subscriptions.append(node.create_subscription(
                    String, f'/robots/{identity}/robot_description',
                    lambda msg, identity=identity: descriptions.__setitem__(identity, msg.data),
                    QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)))
            wait(lambda: len(descriptions) == 2)
            for text in descriptions.values():
                assert '<robot' in text and 'name="base_link"' in text
                assert 'left_wheel_joint' in text
            print('PASS: late subscribers receive robot_description; source joint/link names preserved')

            positions = {'demo_001': 0.6, 'demo_002': -0.4}
            for identity in positions:
                publishers[identity] = node.create_publisher(
                    JointState, f'/robots/{identity}/joint_states', 10)
            wait(lambda: all(pub.get_subscription_count() > 0 for pub in publishers.values()))
            deadline = time.monotonic() + 10
            while not all(identity + '/left_wheel_link' in moving for identity in positions):
                assert process.poll() is None
                assert time.monotonic() < deadline, 'joint state TF was not published'
                for identity, position in positions.items():
                    message = JointState()
                    message.header.stamp = node.get_clock().now().to_msg()
                    message.name = ['left_wheel_joint', 'right_wheel_joint']
                    message.position = [position, position]
                    publishers[identity].publish(message)
                rclpy.spin_once(node, timeout_sec=0.05)
            for identity, position in positions.items():
                transform = moving[identity + '/left_wheel_link']
                assert transform.header.frame_id == identity + '/base_link'
                assert math.isclose(transform.transform.rotation.y, math.sin(position / 2), abs_tol=1e-6)
            print('PASS: independent joint_states produce the correct dynamic TF for each robot')
        except Exception:
            logfile.flush()
            logfile.seek(0)
            print(logfile.read())
            raise
        finally:
            if node is not None:
                node.destroy_node()
            if rclpy.ok():
                rclpy.shutdown()
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            logfile.close()


if __name__ == '__main__':
    run()
