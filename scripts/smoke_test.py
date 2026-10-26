#!/usr/bin/env python3
"""Launch two mock robots on localhost; exercise real generated ROS interfaces.

Requires a sourced built workspace. Always constructs a mock-only inventory;
never uses deployment credentials, vendor drivers or an MQTT broker.
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
from rclpy.action import ActionClient
from rclpy.node import Node
from action_msgs.msg import GoalStatus
from chaos_interfaces.action import Flight, NavigatePose
from chaos_interfaces.msg import CommandResult, RobotState, VelocityCommand
from chaos_interfaces.srv import ControlLease, GetDescription, Stop
from chaos_sdk import Robot
import yaml


def run():
    os.environ['ROS_DOMAIN_ID'] = '187'
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)
    with tempfile.TemporaryDirectory(prefix='chaos-smoke-') as directory:
        root = Path(directory)
        share = Path(get_package_share_directory('chaos_assets'))
        data = yaml.safe_load((share / 'config/demo.yaml').read_text())
        data['hosts'] = {'smoke': {'ros_domain_id': 187}}
        for asset in data['assets']:
            asset['host'], asset['mode'] = 'smoke', 'mock'
            asset['model_file'] = str((share / 'config' / asset['model_file']).resolve())
        inventory = root / 'inventory.yaml'
        inventory.write_text(yaml.safe_dump(data))
        os.environ['ROS_LOG_DIR'] = str(root / 'ros-log')
        logfile = (root / 'launch.log').open('w+')
        process = subprocess.Popen(
            ['ros2', 'launch', 'chaos_bringup', 'system.launch.py',
             f'inventory:={inventory}', 'host_id:=smoke', 'mode:=mock'],
            stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
        node = None
        try:
            rclpy.init()
            node = Node('chaos_smoke_client')
            states, results = {}, {}
            subscriptions = []
            publishers = {}
            for name in ('vehicle_001', 'drone_001'):
                prefix = '/robots/' + name
                subscriptions.append(node.create_subscription(
                    RobotState, prefix + '/state', lambda value, name=name: states.__setitem__(name, value), 10))
                subscriptions.append(node.create_subscription(
                    CommandResult, prefix + '/command_results',
                    lambda value: results.__setitem__(value.request_id, value), 50))
                publishers[name] = node.create_publisher(VelocityCommand, prefix + '/motion/velocity', 10)

            def wait(predicate, timeout=10):
                deadline = time.monotonic() + timeout
                while not predicate():
                    if process.poll() is not None:
                        raise AssertionError(f'launch exited early with {process.returncode}')
                    if time.monotonic() >= deadline:
                        raise AssertionError('Timed out waiting for ROS response/state')
                    rclpy.spin_once(node, timeout_sec=0.03)

            def future_result(future, timeout=10):
                wait(future.done, timeout)
                return future.result()

            def service(asset, kind, endpoint, request):
                client = node.create_client(kind, f'/robots/{asset}/{endpoint}')
                try:
                    wait(client.service_is_ready)
                    return future_result(client.call_async(request))
                finally:
                    node.destroy_client(client)

            def lease(asset, operation='acquire', token='', duration=10.0):
                request = ControlLease.Request(operation=operation, owner='smoke', token=token, duration_s=duration)
                return service(asset, ControlLease, 'control/lease', request)

            def velocity(asset, token, age=0.0, x=0.2):
                message = VelocityCommand()
                message.header.stamp = node.get_clock().now().to_msg()
                message.header.stamp.sec -= int(age)
                message.header.frame_id = f'{asset}/base_link'
                message.request_id = uuid.uuid4().hex
                message.lease_token = token
                message.valid_for_s = 0.3
                message.velocity.linear.x = float(x)
                wait(lambda: publishers[asset].get_subscription_count() > 0)
                publishers[asset].publish(message)
                wait(lambda: message.request_id in results)
                return results[message.request_id]

            def action(asset, kind, endpoint, goal):
                client = ActionClient(node, kind, f'/robots/{asset}/{endpoint}')
                wait(client.server_is_ready)
                return client, future_result(client.send_goal_async(goal))

            wait(lambda: len(states) == 2 and all(state.ready for state in states.values()), 15)
            for asset in states:
                description = service(asset, GetDescription, 'description', GetDescription.Request())
                assert description.asset_id == asset
                assert ('flight.takeoff' in description.capabilities) == (asset == 'drone_001')
            assert not velocity('vehicle_001', 'invalid').success
            vehicle_lease = lease('vehicle_001')
            assert vehicle_lease.success, vehicle_lease.message
            token = vehicle_lease.token
            assert lease('vehicle_001').code == 'BUSY'
            assert velocity('vehicle_001', token).success
            wait(lambda: states['vehicle_001'].pose.pose.position.x > 0.01)
            wait(lambda: abs(states['vehicle_001'].twist.twist.linear.x) < 1e-6)
            assert states['drone_001'].pose.pose.position.x == 0.0
            assert velocity('vehicle_001', token, age=2).code == 'STALE_COMMAND'
            assert velocity('vehicle_001', token, x=float('nan')).code == 'INVALID_ARGUMENT'
            assert velocity('vehicle_001', token, x=100).code == 'OUT_OF_RANGE'
            print('PASS: isolated assets, capability discovery, lease exclusivity, velocity watchdog and validation')

            goal = NavigatePose.Goal(lease_token=token)
            goal.target.header.frame_id = 'vehicle_001/map'
            goal.target.pose.position.x = 0.3
            goal.target.pose.orientation.w = 1.0
            client, handle = action('vehicle_001', NavigatePose, 'navigation/to_pose', goal)
            assert handle.accepted
            result = future_result(handle.get_result_async())
            assert result.status == GoalStatus.STATUS_SUCCEEDED and result.result.success
            goal.target.pose.position.x = 5.0
            moving = future_result(client.send_goal_async(goal))
            assert moving.accepted
            duplicate = future_result(client.send_goal_async(goal))
            assert not duplicate.accepted
            canceled = future_result(moving.cancel_goal_async())
            assert canceled.goals_canceling
            result = future_result(moving.get_result_async())
            assert result.status == GoalStatus.STATUS_CANCELED
            client.destroy()
            print('PASS: navigation completion, conflicting action rejection and cancellation')

            drone_lease = lease('drone_001')
            assert drone_lease.success
            for endpoint, altitude in (('flight/takeoff', 0.3), ('flight/land', 0.0)):
                client, handle = action('drone_001', Flight, endpoint,
                                        Flight.Goal(lease_token=drone_lease.token, altitude_m=altitude))
                assert handle.accepted
                result = future_result(handle.get_result_async())
                assert result.status == GoalStatus.STATUS_SUCCEEDED and result.result.success
                client.destroy()
            assert lease('drone_001', 'release', drone_lease.token).success
            assert lease('vehicle_001', 'release', token).success
            assert velocity('vehicle_001', token).code == 'NO_CONTROL'
            short_lease = lease('vehicle_001', duration=0.3)
            assert short_lease.success
            goal.lease_token = short_lease.token
            client, handle = action('vehicle_001', NavigatePose, 'navigation/to_pose', goal)
            assert handle.accepted
            result = future_result(handle.get_result_async())
            assert result.status == GoalStatus.STATUS_ABORTED
            assert result.result.code in ('LEASE_EXPIRED', 'NO_CONTROL')
            client.destroy()
            print('PASS: mock takeoff/landing, released token rejection and task abort on lease expiry')

            # The native client owns its context, its node and its executor
            # thread: it does not share the node of this script.
            sdk = Robot('vehicle_001')
            try:
                assert sdk.wait_ready(20.0)
                assert future_result(sdk.describe()).base_frame == 'vehicle_001/base_link'
                assert future_result(sdk.acquire('sdk-smoke', 5)).success
                assert sdk.lease_token
                invalid_token = Stop.Request(request_id='invalid-token', lease_token='错误令牌')
                assert service('vehicle_001', Stop, 'motion/stop', invalid_token).code == 'NO_CONTROL'
                request_id = sdk.velocity(x=0, yaw=0)
                wait(lambda: request_id in results)
                assert results[request_id].success
                assert future_result(sdk.renew(5)).success
                assert future_result(sdk.stop()).success
                # An accepted stop must clear the SDK cache immediately.
                assert not sdk.lease_token
                # A fresh acquire restores control after the stop.
                assert future_result(sdk.acquire('sdk-smoke', 5)).success
                assert sdk.lease_token
                assert future_result(sdk.release()).success
                assert not sdk.lease_token
                wait(lambda: sdk.state() is not None)
            finally:
                sdk.close()
            print('PASS: Python SDK discovery, integer inputs, lease caching, renew/stop/release and Unicode token rejection')
        except Exception:
            logfile.flush()
            logfile.seek(0)
            print(logfile.read())
            raise
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=3)
            if node is not None:
                node.destroy_node()
            if rclpy.ok():
                rclpy.shutdown()
            logfile.close()
        if process.returncode not in (0, -signal.SIGINT):
            raise AssertionError(f'launch failed during shutdown: {process.returncode}')
    print('All mock ROS integration checks passed.')


if __name__ == '__main__':
    run()
