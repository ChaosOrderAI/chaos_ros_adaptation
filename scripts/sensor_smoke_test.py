#!/usr/bin/env python3
"""Exercise real ROS sensor interfaces on localhost using only synthetic devices.

The Gemini 335 declaration is forced to mock before launch. No vendor binary,
camera, USB device, broker, or real robot control is accessed.
"""

import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_share_directory
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, qos_profile_sensor_data
from std_msgs.msg import String
from tf2_msgs.msg import TFMessage
import yaml

from chaos_assets import load_inventory
from chaos_interfaces.msg import RobotState

# The message-type catalog of the sensor contract. It is written out here
# instead of imported from the removed Python sensor package: the mapping is
# part of the sensor contract, not of a runtime implementation.
from sensor_msgs.msg import CameraInfo, Image, Imu, LaserScan, PointCloud2

MESSAGE_TYPES = {
    f'sensor_msgs/msg/{message.__name__}': message
    for message in (Image, CameraInfo, PointCloud2, LaserScan, Imu)
}


def run():
    os.environ['ROS_DOMAIN_ID'] = '190'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    with tempfile.TemporaryDirectory(prefix='chaos-sensors-smoke-') as directory:
        root = Path(directory)
        share = Path(get_package_share_directory('chaos_assets'))
        data = yaml.safe_load((share / 'config/orbbec_gemini335.example.yaml').read_text())
        demo = yaml.safe_load((share / 'config/sensors.demo.yaml').read_text())
        data['hosts'] = {'smoke': {'ros_domain_id': 190}}
        asset = data['assets'][0]
        asset['host'], asset['mode'] = 'smoke', 'mock'
        for name in ('lidar', 'body_imu'):
            asset['sensors'][name] = demo['assets'][0]['sensors'][name]
        for sensor in asset['sensors'].values():
            for stream in sensor['streams'].values():
                stream['rate_hz'] = 20.0
        # A synthetic fixture, explicitly not a real mounting calibration.
        asset['sensors']['body_imu']['transform'] = {
            'xyz': [0.1, 0.0, 0.2], 'rpy': [0.0, 0.0, 0.0]}
        data['assets'].append({
            'id': 'drone_001', 'host': 'smoke', 'mode': 'mock',
            'model_file': 'package://chaos_assets/models/dji_m3td/v1/model.yaml'})
        path = root / 'assets.yaml'
        path.write_text(yaml.safe_dump(data))
        resolved = load_inventory(path).assets[0]
        assert all(s['driver']['mode'] == 'mock' for s in resolved['sensors'].values())
        os.environ['ROS_LOG_DIR'] = str(root / 'ros-log')
        logfile = (root / 'launch.log').open('w+')
        process = subprocess.Popen(
            ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
             f'inventory:={path}', 'host_id:=smoke'],
            stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
        node = None
        try:
            rclpy.init()
            node = Node('sensor_smoke_client')
            received, diagnostics, descriptions, states, transforms = {}, [], [], {}, []
            subscriptions = []
            for sensor in resolved['sensors'].values():
                for stream in sensor['streams'].values():
                    topic = stream['topic']
                    subscriptions.append(node.create_subscription(
                        MESSAGE_TYPES[stream['type']], topic,
                        lambda message, topic=topic: received.__setitem__(topic, message),
                        qos_profile_sensor_data))
            subscriptions.append(node.create_subscription(
                DiagnosticArray, '/robots/vehicle_001/sensors/diagnostics',
                lambda message: diagnostics.append(message), 10))
            subscriptions.append(node.create_subscription(
                TFMessage, '/tf_static', lambda message: transforms.extend(message.transforms),
                QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)))
            for identity in ('vehicle_001', 'drone_001'):
                subscriptions.append(node.create_subscription(
                    RobotState, f'/robots/{identity}/state',
                    lambda message, identity=identity: states.__setitem__(identity, message), 10))

            def wait(predicate, timeout=20):
                deadline = time.monotonic() + timeout
                while not predicate():
                    if process.poll() is not None:
                        raise AssertionError(f'launch exited early: {process.returncode}')
                    if time.monotonic() >= deadline:
                        raise AssertionError('timed out waiting for sensor data')
                    rclpy.spin_once(node, timeout_sec=0.03)

            count = sum(len(s['streams']) for s in resolved['sensors'].values())
            wait(lambda: len(received) == count and len(states) == 2)
            for sensor in resolved['sensors'].values():
                for stream in sensor['streams'].values():
                    message = received[stream['topic']]
                    assert message.header.frame_id == stream['frame_id']
                    assert message.header.stamp.sec > 0
                    if stream['type'].endswith('/Image'):
                        assert len(message.data) == message.height * message.step
                    elif stream['type'].endswith('/PointCloud2'):
                        assert len(message.data) == message.row_step * message.height
            assert all(state.ready for state in states.values())
            wait(lambda: any(t.child_frame_id == 'vehicle_001/body_imu_link' for t in transforms))
            mounting = next(t for t in transforms if t.child_frame_id == 'vehicle_001/body_imu_link')
            assert mounting.header.frame_id == 'vehicle_001/base_link'
            assert abs(mounting.transform.translation.x - 0.1) < 1e-6
            print('PASS: Gemini 335 mock RGB/depth/CameraInfo/IMU, lidar and static TF; isolated robot states')

            # Subscribe after all devices are running: directory must be latched.
            subscriptions.append(node.create_subscription(
                String, '/robots/vehicle_001/sensors/description',
                lambda message: descriptions.append(json.loads(message.data)),
                QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)))
            wait(lambda: bool(descriptions))
            catalog = descriptions[-1]
            camera = catalog['sensors']['front_camera']
            assert camera['driver'] == {'mode': 'mock'}
            assert camera['streams']['color_image']['monitor'] is False
            assert 'serial_number' not in json.dumps(catalog)
            monitored = sum(stream['monitor'] for s in resolved['sensors'].values()
                            for stream in s['streams'].values())
            wait(lambda: bool(diagnostics) and len(diagnostics[-1].status) == monitored
                 and all(status.level == DiagnosticStatus.OK for status in diagnostics[-1].status))
            assert not any(status.name.endswith('/color_image') for status in diagnostics[-1].status)
            names = node.get_node_names_and_namespaces()
            assert ('sensor_monitor', '/robots/drone_001') not in names
            print('PASS: late sensor discovery, private parameter exclusion, selective freshness diagnostics')

            # Kill only a child PID reported by this isolated test's own launch.
            logfile.flush()
            logfile.seek(0)
            log = logfile.read()
            match = re.search(r'\[mock_sensor-\d+\]: process started with pid \[(\d+)\]', log)
            assert match, 'mock publisher PID was not present in the launch log'
            child_pid = int(match.group(1))
            assert os.getpgid(child_pid) == process.pid, 'unexpected child process group'
            os.kill(child_pid, signal.SIGKILL)
            process.wait(timeout=15)
            assert process.returncode != 0, 'managed sensor failure must fail the host launch'
            print('PASS: managed sensor failure shuts down unified launch with a failure status')
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
