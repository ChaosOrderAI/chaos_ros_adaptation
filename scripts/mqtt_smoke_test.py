#!/usr/bin/env python3
"""Exercise the MQTT facade over local ROS, with one mock robot and no broker.

Run after sourcing the built workspace. Uses domain 188 and localhost discovery,
starts only its own mock runtime/gateway, and always terminates those processes.
"""

import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import uuid

from ament_index_python.packages import get_package_share_directory
import rclpy
from rclpy.node import Node
from std_msgs.msg import String

from chaos_assets.inventory import load_inventory
from chaos_interfaces.msg import RobotState
from chaos_interfaces.srv import GetDescription


def run():
    os.environ['ROS_DOMAIN_ID'] = '188'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    share = Path(get_package_share_directory('chaos_assets'))
    asset = load_inventory(share / 'config/demo.yaml', mode='mock').select('dev', ['drone_001'])[0]
    assert asset['driver']['plugin'] == 'mock'
    prefix = asset['namespace']
    processes = []
    node = None
    with tempfile.TemporaryDirectory(prefix='chaos-mqtt-smoke-', dir='/tmp') as folder:
        root = Path(folder)
        os.environ['ROS_LOG_DIR'] = str(root / 'ros-log')
        with (root / 'processes.log').open('w+') as logfile:
            try:
                common = ['--ros-args', '-r', '__ns:=' + prefix,
                          '-p', 'asset_json:=' + json.dumps(json.dumps(asset))]
                for package, executable, extra in (
                    ('chaos_runtime', 'robot_runtime', []),
                    ('chaos_mqtt', 'gateway', ['-p', 'action_timeout_s:=1.0']),
                ):
                    processes.append(subprocess.Popen(
                        ['ros2', 'run', package, executable, *common, *extra],
                        stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True))
                rclpy.init()
                node = Node('chaos_mqtt_smoke_client')
                replies, events, states = {}, [], []

                def receive_reply(message):
                    data = json.loads(message.data)
                    replies.setdefault(data['request_id'], []).append(data)

                node.create_subscription(String, prefix + '/mqtt/command/result', receive_reply, 32)
                node.create_subscription(String, prefix + '/mqtt/task_events',
                                         lambda message: events.append(json.loads(message.data)), 32)
                node.create_subscription(String, prefix + '/mqtt/state',
                                         lambda message: states.append(json.loads(message.data)), 32)
                publisher = node.create_publisher(String, prefix + '/mqtt/command', 32)
                state_pub = node.create_publisher(RobotState, prefix + '/state', 10)
                describe_client = node.create_client(GetDescription, prefix + '/description')

                def wait(predicate, timeout=10):
                    deadline = time.monotonic() + timeout
                    while not predicate():
                        for process in processes:
                            if process.poll() is not None:
                                raise AssertionError(f'child process exited: {process.returncode}')
                        if time.monotonic() >= deadline:
                            raise AssertionError('timed out waiting for MQTT facade ROS response')
                        rclpy.spin_once(node, timeout_sec=0.02)

                def command(name, *, request_id=None, age_ms=0, ttl_ms=1000, **fields):
                    request_id = request_id or uuid.uuid4().hex
                    value = {'bridge': {'source': 'mqtt', 'request_id': request_id,
                                        'timestamp_ms': time.time_ns() // 1_000_000 - age_ms},
                             'payload': {'command': name, 'ttl_ms': ttl_ms, **fields}}
                    previous = len(replies.get(request_id, []))
                    publisher.publish(String(data=json.dumps(value)))
                    wait(lambda: len(replies.get(request_id, [])) > previous)
                    reply = replies[request_id][-1]
                    assert reply['source'] == 'mqtt' and reply['command'] == name
                    return request_id, reply['response']

                def terminal(request_id):
                    wait(lambda: any(item['request_id'] == request_id and item['terminal'] for item in events))
                    return next(item for item in events if item['request_id'] == request_id and item['terminal'])

                wait(lambda: publisher.get_subscription_count() > 0
                     and describe_client.service_is_ready() and bool(states))
                _, description = command('system.describe')
                assert description['ok'] and description['data']['asset_id'] == asset['id'], description
                acquire_id, acquired = command('control.acquire', owner='mqtt_smoke', duration_s=15.0)
                assert acquired['ok'], acquired
                token = acquired['data']['token']
                _, repeated = command('control.acquire', request_id=acquire_id,
                                      owner='mqtt_smoke', duration_s=15.0)
                assert repeated == acquired, (repeated, acquired)
                print('PASS: description, lease service, duplicate cached response')

                _, moved = command('motion.velocity', lease_token=token, valid_for_s=0.3,
                                   velocity={'linear': {'x': 0.1}})
                assert moved['ok'] and moved['terminal'], moved
                _, stale = command('motion.velocity', age_ms=2000, ttl_ms=1000,
                                   lease_token=token, valid_for_s=0.3, velocity={})
                assert not stale['ok'] and stale['code'] == 'EXPIRED', stale
                _, bad = command('motion.velocity', lease_token=token, valid_for_s=0.3,
                                 velocity={}, topic='/hardware/cmd_vel')
                assert not bad['ok'] and bad['code'] == 'INVALID_ARGUMENT', bad
                _, stopped = command('motion.stop', lease_token=token)
                assert stopped['ok'], stopped
                print('PASS: velocity runtime acknowledgment, TTL rejection, fixed command fields, stop')

                _, malformed = command('navigation.to_pose', lease_token=token,
                    target={'frame_id': asset['frames']['map'], 'position': {'x': 0.0},
                            'orientation': {'x': 1e308, 'w': 1.0}})
                assert not malformed['ok'] and malformed['code'] == 'INVALID_ARGUMENT', malformed
                print('PASS: extreme finite quaternion is rejected without terminating the gateway')

                for name, fields in (
                    ('navigation.to_pose', {'target': {'frame_id': asset['frames']['map'],
                                                       'position': {'x': 0.1}}}),
                    ('flight.takeoff', {'altitude_m': 0.2}),
                    ('flight.land', {'altitude_m': 0.0}),
                ):
                    request_id, accepted = command(name, lease_token=token, **fields)
                    assert accepted['ok'] and accepted['phase'] == 'accepted' and not accepted['terminal'], accepted
                    result = terminal(request_id)
                    assert result['ok'] and result['execution_state'] == 'known', result
                    assert result['goal_id'] == accepted['goal_id'], result
                assert any(item['phase'] == 'feedback' for item in events)
                print('PASS: navigation/takeoff/landing accepted, goal correlation, feedback, terminal results')

                request_id, accepted = command('navigation.to_pose', lease_token=token,
                    target={'frame_id': asset['frames']['map'], 'position': {'x': 10.0}})
                assert accepted['ok'], accepted
                result = terminal(request_id)
                assert result['code'] == 'RESPONSE_TIMEOUT' and result['execution_state'] == 'unknown', result
                _, stopped = command('motion.stop', lease_token=token)
                assert stopped['ok'], stopped
                print('PASS: timeout reports unknown execution, explicit stop remains available')

                wait(lambda: state_pub.get_subscription_count() > 0)
                unknown = RobotState()
                unknown.asset_id = 'synthetic_unknown'
                unknown.battery_percentage = float('nan')
                state_pub.publish(unknown)
                wait(lambda: any(state.get('asset_id') == 'synthetic_unknown' for state in states))
                telemetry = next(state for state in states if state.get('asset_id') == 'synthetic_unknown')
                assert telemetry['battery_percentage'] is None, telemetry
                _, released = command('control.release', owner='mqtt_smoke', token=token)
                assert released['ok'], released
                print('PASS: unknown telemetry serializes to JSON null; gateway remains live')
            except Exception:
                logfile.flush()
                logfile.seek(0)
                print(logfile.read())
                raise
            finally:
                for process in processes:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGINT)
                for process in processes:
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=2)
                if node:
                    node.destroy_node()
                if rclpy.ok():
                    rclpy.shutdown()


if __name__ == '__main__':
    run()
