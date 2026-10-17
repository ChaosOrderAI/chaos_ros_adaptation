#!/usr/bin/env python3
"""Exercise native recording and injected chrony states with mock robots only.

The temporary chronyc fixture tests admission behavior, not actual NTP precision.
No system clock, daemon, real device or external service is modified or accessed.
"""

import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from ament_index_python.packages import get_package_share_directory
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile
from std_msgs.msg import String
import yaml

from chaos_data.audit import audit_bag
from chaos_interfaces.msg import RobotState


def run():
    os.environ['ROS_DOMAIN_ID'] = '192'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    with tempfile.TemporaryDirectory(prefix='chaos-data-smoke-') as directory:
        root = Path(directory)
        share = Path(get_package_share_directory('chaos_assets'))
        data = yaml.safe_load((share / 'config/data_collection.demo.yaml').read_text())
        data['hosts'] = {'smoke': {'ros_domain_id': 192}}
        asset = data['assets'][0]
        asset.update(host='smoke', mode='mock')
        asset['sensors'] = {
            'camera': {'kind': 'camera', 'driver': {'mode': 'mock'}, 'streams': {
                'color': {'type': 'sensor_msgs/msg/Image', 'topic': 'color/image_raw'},
                'info': {'type': 'sensor_msgs/msg/CameraInfo', 'topic': 'color/camera_info'},
            }},
            'lidar': {'kind': 'lidar', 'driver': {'mode': 'mock'}, 'streams': {
                'points': {'type': 'sensor_msgs/msg/PointCloud2', 'topic': 'points'},
            }},
        }
        asset['data_collection'].update({
            'output_dir': str(root / 'recordings'), 'max_bagfile_duration_s': 2,
            'time_sync': {'require_sync': True, 'poll_interval_s': .2,
                          'probe_timeout_s': .15, 'stable_samples': 3},
        })
        path = root / 'assets.yaml'
        path.write_text(yaml.safe_dump(data))
        state_file = root / 'clock-state'
        state_file.write_text('unsynced')
        bin_dir = root / 'bin'
        bin_dir.mkdir()
        chronyc = bin_dir / 'chronyc'
        chronyc.write_text('''#!/usr/bin/python3
import os
from pathlib import Path
import sys
import time
assert sys.argv[1:] == ['-c', '-n', 'tracking']
if Path(os.environ['CHAOS_TEST_CHRONY_STATE']).read_text() == 'synced':
    print(f'C0000201,192.0.2.1,2,{time.time() - .1},.0001,.0001,.0001,0,0,0,.001,.0001,16,Normal')
else:
    print('00000000,,0,0,0,0,0,0,0,0,0,0,0,Not synchronised')
''')
        chronyc.chmod(0o700)
        environment = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'],
                           CHAOS_TEST_CHRONY_STATE=str(state_file), ROS_LOG_DIR=str(root / 'ros-log'))
        logfile = (root / 'launch.log').open('w+')
        process = subprocess.Popen(
            ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
             f'inventory:={path}', 'host_id:=smoke'],
            env=environment, stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
        node = None
        try:
            rclpy.init()
            node = Node('chaos_data_smoke')
            observed = {'status': None, 'states': 0}

            def status(message):
                observed['status'] = json.loads(message.data)

            def state(message):
                if message.ready:
                    observed['states'] += 1

            subscriptions = [
                node.create_subscription(String, '/robots/demo_001/data_collection/status', status,
                    QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)),
                node.create_subscription(RobotState, '/robots/demo_001/state', state, 10),
            ]

            def wait(predicate, timeout=35):
                deadline = time.monotonic() + timeout
                while not predicate():
                    assert process.poll() is None, f'launch exited: {process.returncode}'
                    assert time.monotonic() < deadline, f'timed out: {observed}'
                    rclpy.spin_once(node, timeout_sec=.03)

            def recording(count):
                value = observed['status'] or {}
                return value.get('state') == 'recording' and value.get('segment_count') == count

            wait(lambda: observed['status'] and observed['status']['clock_samples'] >= 2 and observed['states'] > 0)
            assert observed['status']['state'] == 'waiting_for_clock'
            assert observed['status']['segment_count'] == 0
            assert not list((root / 'recordings').rglob('*.mcap'))
            print('PASS: unsynchronized host does not start recording; robot runtime stays ready')
            state_file.write_text('synced')
            wait(lambda: recording(1))
            start = time.monotonic()
            wait(lambda: time.monotonic() - start > 3.5)
            state_file.write_text('unsynced')
            wait(lambda: observed['status']['state'] == 'waiting_for_clock' and observed['status']['segment_count'] == 1)
            session = next((root / 'recordings/demo_001').iterdir())
            manifest = json.loads((session / 'manifest.json').read_text())
            assert manifest['segments'][0]['status'] == 'closed'
            assert manifest['segments'][0]['suspect_interval'] is not None
            print('PASS: clock loss flushes the segment and records the potentially affected interval')
            state_file.write_text('synced')
            wait(lambda: recording(2))
            start = time.monotonic()
            wait(lambda: time.monotonic() - start > 3.5)
            os.killpg(process.pid, signal.SIGINT)
            assert process.wait(timeout=22) == 0
            manifest = json.loads((session / 'manifest.json').read_text())
            assert manifest['status'] == 'completed', manifest
            assert len(manifest['segments']) == 2
            for segment in manifest['segments']:
                assert segment['status'] == 'closed', segment
                bag = session / segment['relative_path']
                assert len(list(bag.glob('*.mcap'))) >= 2, 'duration splitting was not exercised'
                report = audit_bag(bag, index=root / f'index-{segment["index"]}.jsonl')
                topics = report['topics']
                for topic in ('/tf_static', '/robots/demo_001/robot_description',
                              '/robots/demo_001/sensors/description', '/robots/demo_001/state',
                              '/robots/demo_001/data_collection/status'):
                    assert topics[topic]['messages'] > 0, topic
                imu = topics['/robots/demo_001/sensors/body_imu/data']
                assert imu['valid_source_stamps'] > 0
                assert imu['invalid_source_stamps'] == imu['source_reversals'] == 0
                for stream in ('camera/color/image_raw', 'camera/color/camera_info', 'lidar/points'):
                    assert topics['/robots/demo_001/sensors/' + stream]['valid_source_stamps'] > 0
                assert report['complete'] and report['modified_messages'] == 0
            print('PASS: split MCAP bags contain retained TF/model, state, camera/lidar/IMU source stamps and indexes')
            events = [json.loads(line) for line in (session / 'time_sync.jsonl').read_text().splitlines()]
            assert any(event.get('state') == 'waiting_for_clock' for event in events)
            assert events[-1]['event'] == 'session_end'
            assert subscriptions
            print('PASS: graceful shutdown finalizes metadata and clock evidence; fixture does not verify real NTP')
        except BaseException:
            logfile.flush()
            logfile.seek(0)
            print(logfile.read())
            raise
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=22)
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
