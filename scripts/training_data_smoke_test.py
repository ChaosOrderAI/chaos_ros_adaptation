#!/usr/bin/env python3
"""Mock runtime -> redacted control trace -> native bag -> offline dataset.

No hardware, broker or system clock changes. Explicitly unverified mock time
tests the exporter opt-in; this does not validate NTP or a learned controller.
"""

import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_share_directory
import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile
from std_msgs.msg import String
import yaml

from chaos_interfaces.msg import ControlTrace
from chaos_sdk import Robot


def stamp_ns(stamp):
    return stamp.sec * 1_000_000_000 + stamp.nanosec


def run():
    os.environ['ROS_DOMAIN_ID'] = '193'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    with tempfile.TemporaryDirectory(prefix='chaos-training-smoke-') as directory:
        root = Path(directory)
        share = Path(get_package_share_directory('chaos_assets'))
        data = yaml.safe_load((share / 'config/data_collection.demo.yaml').read_text())
        data['hosts'] = {'smoke': {'ros_domain_id': 193}}
        asset = data['assets'][0]
        asset.update(host='smoke', mode='mock')
        asset['sensors'] = {'camera': {
            'kind': 'camera', 'driver': {'mode': 'mock'}, 'streams': {'color': {
                'type': 'sensor_msgs/msg/Image', 'topic': 'image_raw', 'rate_hz': 20,
                'frame_id': 'demo_001/camera_optical_frame',
            }},
        }}
        asset['data_collection'].update(output_dir=str(root / 'recordings'))
        path = root / 'assets.yaml'
        path.write_text(yaml.safe_dump(data))
        logfile = (root / 'launch.log').open('w+')
        process = subprocess.Popen(
            ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
             f'inventory:={path}', 'host_id:=smoke'],
            env=dict(os.environ, ROS_LOG_DIR=str(root / 'ros-log')),
            stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
        node = None
        try:
            rclpy.init()
            node = Node('chaos_training_smoke')
            statuses, traces = [], []
            subscriptions = [
                node.create_subscription(String, '/robots/demo_001/data_collection/status',
                    lambda msg: statuses.append(json.loads(msg.data)),
                    QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)),
                node.create_subscription(ControlTrace, '/robots/demo_001/control_trace', traces.append, 100),
            ]
            robot = Robot(node, 'demo_001')

            def wait(predicate, timeout=25):
                deadline = time.monotonic() + timeout
                while not predicate():
                    assert process.poll() is None, f'launch exited: {process.returncode}'
                    assert time.monotonic() < deadline, 'timed out waiting for mock collection'
                    rclpy.spin_once(node, timeout_sec=.02)

            def result(future):
                wait(future.done)
                return future.result()

            wait(lambda: statuses and statuses[-1]['state'] == 'recording' and robot.wait_ready(.05))
            wait(lambda: node.count_subscribers('/robots/demo_001/control_trace') >= 2)
            result(robot.describe())
            assert result(robot.acquire('training_smoke', duration_s=10)).success
            token = robot.lease_token
            begin = time.monotonic()
            next_send = begin
            while time.monotonic() - begin < 2.5:
                if time.monotonic() >= next_send:
                    robot.velocity(x=.12, yaw=.03, valid_for_s=.3)
                    next_send = time.monotonic() + .08
                rclpy.spin_once(node, timeout_sec=.02)
            assert result(robot.stop()).success
            wait(lambda: any(row.command == 'motion.stop' and row.stage == 'issued' for row in traces))
            started = time.monotonic()
            wait(lambda: time.monotonic() - started > .35)
            end_ns = time.time_ns()
            issued = [row for row in traces if row.command == 'motion.velocity' and row.stage == 'issued']
            assert len(issued) >= 15 and all(row.velocity_valid for row in issued)
            assert all(row.valid_for_s > 0 for row in issued)
            assert all(not hasattr(row, 'lease_token') for row in traces)
            assert any(row.stage == 'invalidated' for row in traces)
            os.kill(process.pid, signal.SIGINT)
            assert process.wait(timeout=45) == 0
            session = next((root / 'recordings/demo_001').iterdir())
            manifest = json.loads((session / 'manifest.json').read_text())
            assert manifest['status'] == 'completed'
            print('PASS: native bag captures redacted issued velocity and explicit stop invalidation')

            config = {
                'schema_version': 1, 'fps': 10, 'allow_unverified_time': False,
                'features': {
                    'observation.state': {
                        'kind': 'vector', 'topic': '/robots/demo_001/state',
                        'type': 'chaos_interfaces/msg/RobotState',
                        'fields': ['pose.pose.position.x', 'pose.pose.position.y'], 'units': ['m', 'm'],
                        'stamp_field': 'pose.header.stamp', 'frame_field': 'pose.header.frame_id',
                        'frame': 'demo_001/map', 'clock_domain': 'system_utc',
                        'validity_fields': ['connected', 'pose_valid'], 'max_age_ms': 200,
                    },
                    'observation.images.front': {
                        'kind': 'image', 'units': 'intensity',
                        'topic': '/robots/demo_001/sensors/camera/image_raw',
                        'type': 'sensor_msgs/msg/Image', 'stamp_field': 'header.stamp',
                        'frame_field': 'header.frame_id', 'frame': 'demo_001/camera_optical_frame',
                        'clock_domain': 'system_utc', 'max_age_ms': 200,
                    },
                },
                'action': {
                    'topic': '/robots/demo_001/control_trace', 'space': 'base_velocity_2d',
                    'clock_domain': 'system_utc', 'frame': 'demo_001/base_link',
                    'fields': ['velocity.linear.x', 'velocity.angular.z'],
                    'units': ['m/s', 'rad/s'], 'max_age_ms': 200,
                },
                'episodes': [{
                    'id': 'demo_forward', 'session': str(session), 'segment': 1,
                    'start_utc_ns': stamp_ns(issued[0].header.stamp), 'end_utc_ns': end_ns,
                    'task': 'Synthetic forward and yaw velocity demonstration', 'outcome': 'unknown',
                }],
            }
            profile = root / 'export.yaml'
            profile.write_text(yaml.safe_dump(config))
            output = root / 'dataset'

            def export():
                return subprocess.run(['ros2', 'run', 'chaos_data', 'data_export', str(profile),
                    '--output', str(output)], text=True, capture_output=True, timeout=30)

            rejected = export()
            assert rejected.returncode != 0 and not output.exists(), rejected.stdout + rejected.stderr
            config['allow_unverified_time'] = True
            profile.write_text(yaml.safe_dump(config))
            converted = export()
            assert converted.returncode == 0, converted.stdout + converted.stderr
            rows = [json.loads(line) for line in (output / 'episodes/demo_forward/steps.jsonl').read_text().splitlines()]
            assert sum(row['valid'] for row in rows) >= 10
            assert any(not row['valid'] for row in rows), 'stop tail must not keep a stale action'
            images = list((output / 'episodes/demo_forward/images').rglob('*.npy'))
            assert images
            pixels = np.load(images[0], allow_pickle=False)
            assert pixels.shape == (48, 64, 3) and pixels.dtype == np.uint8
            assert token not in (output / 'dataset.json').read_text()
            assert token not in (output / 'episodes/demo_forward/steps.jsonl').read_text()
            assert export().returncode != 0, 'existing dataset must never be overwritten'
            assert subscriptions
            print('PASS: strict time rejection, explicit mock-only override, offline vectors/images, stop gaps and no overwrite')
        except BaseException:
            logfile.flush()
            logfile.seek(0)
            print(logfile.read())
            raise
        finally:
            if process.poll() is None:
                os.kill(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            if node is not None:
                node.destroy_node()
            if rclpy.ok():
                rclpy.shutdown()
            logfile.close()


if __name__ == '__main__':
    run()
