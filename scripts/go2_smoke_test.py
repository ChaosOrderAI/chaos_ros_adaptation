#!/usr/bin/env python3
"""Exercise the installed Go2 mock through unified launch and the Python SDK.

Requires a built, sourced chaos_ws. Uses localhost discovery in ROS domain 186,
an explicitly mock-only temporary inventory, and no MQTT or Unitree driver.
"""

import math
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from action_msgs.msg import GoalStatus
from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import PoseStamped
import rclpy
from rclpy.node import Node
import yaml

from chaos_assets import load_inventory
from chaos_interfaces.msg import CommandResult
from chaos_sdk import Robot


def run():
    domain = 186
    environment_keys = ('ROS_DOMAIN_ID', 'ROS_AUTOMATIC_DISCOVERY_RANGE',
                        'ROS_LOCALHOST_ONLY', 'ROS_LOG_DIR')
    previous_environment = {key: os.environ.get(key) for key in environment_keys}
    os.environ['ROS_DOMAIN_ID'] = str(domain)
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    try:
        with tempfile.TemporaryDirectory(prefix='chaos-go2-smoke-') as directory:
            root = Path(directory)
            share = Path(get_package_share_directory('chaos_assets'))
            source = share / 'config/go2.demo.yaml'
            installed = load_inventory(source, mode='mock')
            assert len(installed.assets) == 1, 'Go2 demo must contain exactly one robot'
            original = installed.assets[0]
            # Rebuild a minimal inventory, using the resolved absolute model path.
            # No deployment transport, credentials or instance driver overrides
            # are copied from the source configuration.
            inventory = root / 'inventory.yaml'
            inventory.write_text(yaml.safe_dump({
                'schema_version': 1,
                'hosts': {'smoke': {'ros_domain_id': domain}},
                'assets': [{
                    'id': original['id'], 'model_file': original['model_file'],
                    'host': 'smoke', 'mode': 'mock', 'frames': original['frames'],
                }],
            }), encoding='utf-8')
            asset = load_inventory(inventory, mode='mock').select('smoke')[0]
            assert asset['kind'] == 'quadruped'
            assert asset['mode'] == 'mock' and asset['driver'] == {
                'plugin': 'mock', 'options': {'kind': 'quadruped'}}
            assert asset['transport'] is None
            assert not asset['sensors'], 'This smoke test must not start sensor drivers'
            assert not asset['description'], 'This smoke test requires only the robot runtime'
            assert not asset['data_collection'], 'This smoke test must not start recorders'

            os.environ['ROS_LOG_DIR'] = str(root / 'ros-log')
            process = None
            node = None
            sdk = None
            with (root / 'launch.log').open('w+', encoding='utf-8') as logfile:
                try:
                    process = subprocess.Popen(
                        ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
                         f'inventory:={inventory}', 'host_id:=smoke', 'mode:=mock',
                         'enable_mqtt:=false', 'discovery_range:=LOCALHOST'],
                        stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
                    rclpy.init()
                    node = Node('chaos_go2_smoke_client')
                    sdk = Robot(node, asset['id'])
                    results = {}
                    result_subscription = node.create_subscription(
                        CommandResult, asset['namespace'] + '/command_results',
                        lambda message: results.__setitem__(message.request_id, message), 50)

                    def wait(predicate, label, timeout=10.0):
                        deadline = time.monotonic() + timeout
                        while not predicate():
                            if process.poll() is not None:
                                raise AssertionError(f'Launch exited early: {process.returncode}')
                            if time.monotonic() >= deadline:
                                raise AssertionError(f'Timed out waiting for {label}')
                            rclpy.spin_once(node, timeout_sec=0.03)

                    def future_result(future, label, timeout=10.0):
                        wait(future.done, label, timeout)
                        return future.result()

                    def velocity(**values):
                        request_id = sdk.velocity(**values)
                        wait(lambda: request_id in results, 'velocity acknowledgement')
                        return results.pop(request_id)

                    def velocity_values():
                        state = sdk.state()
                        if state is None:
                            return None
                        twist = state.twist.twist
                        return (twist.linear.x, twist.linear.y, twist.linear.z,
                                twist.angular.x, twist.angular.y, twist.angular.z)

                    def stopped():
                        values = velocity_values()
                        return values is not None and all(abs(value) < 1e-6 for value in values)

                    wait(lambda: sdk._description.service_is_ready(), 'description service', 20)
                    description = future_result(sdk.describe(), 'description response')
                    assert description.asset_id == asset['id']
                    assert description.model == 'unitree_go2'
                    assert description.base_frame == asset['frames']['base']
                    assert description.map_frame == asset['frames']['map']
                    assert set(description.capabilities) == {
                        'state', 'motion.velocity', 'motion.stop', 'navigation.pose'}
                    assert not any(capability.startswith('flight.')
                                   for capability in description.capabilities)
                    wait(lambda: sdk.state() is not None and sdk.state().ready
                         and sdk.state().connected and sdk.state().pose_valid,
                         'ready mock quadruped')
                    assert 'mock' in sdk.state().detail.lower()
                    names = node.get_node_names_and_namespaces()
                    assert not any('go2_driver' in name or 'mqtt' in name for name, _ in names)
                    print('PASS: installed quadruped model, SDK discovery and capabilities without flight')

                    wait(lambda: sdk._lease.service_is_ready(), 'lease service')
                    lease = future_result(sdk.acquire('go2-smoke', 30.0), 'control lease')
                    assert lease.success and sdk.lease_token, lease.message
                    wait(lambda: node.count_subscribers(asset['namespace'] + '/motion/velocity') > 0,
                         'velocity subscriber')
                    start = sdk.state().pose.pose.position
                    start_x, start_y, start_z = start.x, start.y, start.z
                    command = velocity(x=0.1, y=0.1, yaw=0.2)
                    assert command.success, command.message
                    wait(lambda: velocity_values() is not None and all(
                        math.isclose(actual, expected, abs_tol=1e-6)
                        for actual, expected in zip(velocity_values(), (0.1, 0.1, 0, 0, 0, 0.2))),
                        'x/y/yaw velocity telemetry')
                    wait(lambda: sdk.state().pose.pose.position.x > start_x + 0.005
                         and sdk.state().pose.pose.position.y > start_y + 0.005
                         and abs(sdk.state().pose.pose.orientation.z) > 0.001,
                         'forward, sideways and yaw motion')
                    wait(stopped, 'command watchdog stop', timeout=3.0)
                    assert math.isclose(sdk.state().pose.pose.position.z, start_z, abs_tol=1e-9)
                    print('PASS: leased x/y/yaw motion, lateral displacement and command watchdog stop')

                    rejected = velocity(z=0.1)
                    assert not rejected.success and rejected.code == 'UNSUPPORTED', rejected.message
                    assert stopped(), 'Rejected vertical velocity must not move the quadruped'
                    assert velocity(y=0.1).success
                    wait(lambda: abs(velocity_values()[1]) > 0.05, 'lateral motion before explicit stop')
                    wait(lambda: sdk._stop.service_is_ready(), 'motion stop service')
                    stop = future_result(sdk.stop(), 'motion.stop response')
                    assert stop.success, stop.message
                    wait(stopped, 'explicit stop telemetry')
                    print('PASS: unsupported vertical velocity rejected and motion.stop clears motion')

                    wait(lambda: sdk._navigation.server_is_ready(), 'navigation action server')
                    target = PoseStamped()
                    target.header.frame_id = description.map_frame
                    target.header.stamp = node.get_clock().now().to_msg()
                    target.pose.position.x = sdk.state().pose.pose.position.x + 0.12
                    target.pose.position.y = sdk.state().pose.pose.position.y + 0.10
                    target.pose.orientation.z = math.sin(0.15 / 2)
                    target.pose.orientation.w = math.cos(0.15 / 2)
                    handle = future_result(sdk.navigate(target), 'planar navigation acceptance')
                    assert handle.accepted, 'Mock planar navigation should be accepted'
                    result = future_result(handle.get_result_async(), 'planar navigation result')
                    assert result.status == GoalStatus.STATUS_SUCCEEDED, result.status
                    assert result.result.success, result.result.message
                    wait(lambda: math.isclose(sdk.state().pose.pose.position.x,
                                             target.pose.position.x, abs_tol=1e-4)
                         and math.isclose(sdk.state().pose.pose.position.y,
                                          target.pose.position.y, abs_tol=1e-4)
                         and math.isclose(sdk.state().pose.pose.orientation.z,
                                          target.pose.orientation.z, abs_tol=1e-4)
                         and stopped(), 'final planar navigation pose')
                    target.pose.position.z = 0.1
                    rejected_goal = future_result(sdk.navigate(target), 'vertical navigation rejection')
                    assert not rejected_goal.accepted, 'Quadruped navigation must reject nonzero z'
                    assert abs(sdk.state().pose.pose.position.z) < 1e-9
                    print('PASS: mock planar navigation completes and nonzero-z target is rejected')

                    released = future_result(sdk.release(), 'control lease release')
                    assert released.success and not sdk.lease_token, released.message
                    wait(stopped, 'stationary state after lease release')
                    try:
                        sdk.velocity(y=0.1)
                    except ValueError:
                        pass
                    else:
                        raise AssertionError('SDK must require control after lease release')
                    node.destroy_subscription(result_subscription)
                    print('PASS: lease release clears cached control and prevents further SDK motion')
                except Exception:
                    logfile.flush()
                    logfile.seek(0)
                    print(logfile.read())
                    raise
                finally:
                    if process is not None and process.poll() is None:
                        os.killpg(process.pid, signal.SIGINT)
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=3)
                    if sdk is not None:
                        sdk.close()
                    if node is not None:
                        node.destroy_node()
                    if rclpy.ok():
                        rclpy.shutdown()
                if process.returncode not in (0, -signal.SIGINT):
                    raise AssertionError(f'Launch failed during shutdown: {process.returncode}')
    finally:
        for key, value in previous_environment.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
    print('All Go2 mock ROS integration checks passed; no hardware or broker was used.')


if __name__ == '__main__':
    run()
