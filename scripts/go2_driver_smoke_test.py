#!/usr/bin/env python3
"""Exercise the real Go2 ROS bridge with a mandatory in-memory SDK substitute.

Requires a built, sourced chaos_ws and localhost ROS sockets in domain 185.
The real Go2Driver, Go2Adapter and RobotRuntime exchange actual ROS messages,
but importing unitree_sdk2py is prohibited and load_sdk is always replaced.
No vendor DDS participant, robot connection, MQTT or hardware is created.
"""

from contextlib import ExitStack
from importlib.abc import MetaPathFinder
import json
import math
import os
from pathlib import Path
import signal
import sys
import tempfile
import time
from types import SimpleNamespace
from unittest.mock import patch


class BlockVendorSDK(MetaPathFinder):
    """Fail closed if a future code change tries to import the hardware SDK."""

    def find_spec(self, fullname, path=None, target=None):
        if fullname == 'unitree_sdk2py' or fullname.startswith('unitree_sdk2py.'):
            raise AssertionError('This smoke test must never import the real Unitree SDK')
        return None


class FakeSportClient:
    """Record the bounded Move interface; no socket or vendor code is used."""

    def __init__(self):
        self.calls = []
        self.timeout = None
        self.initialized = False

    def SetTimeout(self, seconds):
        self.timeout = seconds

    def Init(self):
        self.initialized = True

    def Move(self, x, y, yaw):
        assert self.initialized and 0 < self.timeout <= 0.1
        self.calls.append((time.monotonic(), (x, y, yaw)))
        return 0


class FakeSubscriber:
    """The test feeds this callback directly; this is not a DDS subscriber."""

    def __init__(self, topic, state_type):
        assert topic == 'rt/sportmodestate'
        assert state_type is SimpleNamespace
        self.callback = None
        self.closed = False
        self.sequence = 0

    def Init(self, callback, depth):
        assert depth == 1
        self.callback = callback

    def Close(self):
        self.closed = True
        self.callback = None

    def emit(self):
        assert not self.closed and self.callback is not None
        self.sequence += 1
        sec, nanosec = divmod(self.sequence * 50_000_000, 1_000_000_000)
        # A fixed pose rotated 90 degrees tests odom-to-body velocity conversion.
        # The synthetic state does not model physical movement or SDK completion.
        sample = SimpleNamespace(
            stamp=SimpleNamespace(sec=sec, nanosec=nanosec), error_code=0,
            position=[1.0, 2.0, 0.3], velocity=[0.2, 0.0, 0.0], yaw_speed=0.0,
            imu_state=SimpleNamespace(
                quaternion=[math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5)],
                gyroscope=[0.0, 0.0, 0.0]),
        )
        assert self.callback(sample), 'Fresh synthetic sport state was rejected'


class FakeSDK:
    def __init__(self):
        self.initialization = None
        self.sport = FakeSportClient()
        self.subscriber = None

    def initialize(self, domain, interface):
        assert domain == 0 and interface == 'fake_sdk'
        self.initialization = domain, interface

    def make_subscriber(self, topic, state_type):
        assert self.subscriber is None
        self.subscriber = FakeSubscriber(topic, state_type)
        return self.subscriber

    def load(self):
        return self.initialize, self.make_subscriber, lambda: self.sport, SimpleNamespace


def assert_vendor_sdk_absent():
    assert not any(name == 'unitree_sdk2py' or name.startswith('unitree_sdk2py.')
                   for name in sys.modules), 'Run this test in a process without the vendor SDK loaded'


def run():
    assert_vendor_sdk_absent()
    blocker = BlockVendorSDK()
    fake = FakeSDK()
    with ExitStack() as cleanup:
        sys.meta_path.insert(0, blocker)
        cleanup.callback(sys.meta_path.remove, blocker)
        root = Path(cleanup.enter_context(tempfile.TemporaryDirectory(prefix='chaos-go2-bridge-')))
        cleanup.enter_context(patch.dict(os.environ, {
            'ROS_DOMAIN_ID': '185', 'ROS_AUTOMATIC_DISCOVERY_RANGE': 'LOCALHOST',
            'ROS_LOG_DIR': str(root / 'ros-log'),
        }))
        os.environ.pop('ROS_LOCALHOST_ONLY', None)

        # Import all application code under the guard, including future changes
        # to module-level imports. Nothing in this test can fall back to SDK2.
        from ament_index_python.packages import get_package_share_directory
        from geometry_msgs.msg import PoseStamped
        import rclpy
        from rclpy.context import Context
        from rclpy.executors import SingleThreadedExecutor
        from rclpy.node import Node
        from rclpy.parameter import Parameter
        from rclpy.signals import SignalHandlerOptions

        from chaos_assets import load_inventory
        from chaos_interfaces.msg import CommandResult
        from chaos_runtime import go2_driver
        from chaos_runtime.adapters.go2 import Go2Adapter
        from chaos_runtime.node import RobotRuntime
        from chaos_sdk import Robot

        cleanup.enter_context(patch.object(go2_driver, 'load_sdk', fake.load))

        def stop_signal(signum, frame):
            raise KeyboardInterrupt

        for signum in (signal.SIGINT, signal.SIGTERM):
            previous = signal.signal(signum, stop_signal)
            cleanup.callback(signal.signal, signum, previous)

        share = Path(get_package_share_directory('chaos_assets'))
        asset = load_inventory(share / 'config/go2.demo.yaml', mode='real').select('dev')[0]
        asset_id = f'go2_bridge_{os.getpid()}'
        asset.update(id=asset_id, namespace=f'/robots/{asset_id}', host='smoke', ros_domain_id=185)
        asset['frames'] = {name: f'{asset_id}/{suffix}' for name, suffix in (
            ('base', 'base_link'), ('odom', 'odom'), ('map', 'map'))}
        asset['driver']['options'].update(
            network_interface='fake_sdk', dds_domain_id=0,
            sport_state_topic='rt/sportmodestate', velocity_frame='odom',
            cmd_vel_topic=f'/test_go2_{os.getpid()}/cmd_vel',
            odom_topic=f'/test_go2_{os.getpid()}/odom')
        assert asset['mode'] == 'real' and asset['driver']['plugin'] == 'unitree_go2'
        assert not any(asset[key] for key in ('transport', 'sensors', 'description', 'data_collection'))

        context = Context()
        rclpy.init(args=[], context=context, domain_id=185,
                   signal_handler_options=SignalHandlerOptions.NO)
        cleanup.callback(rclpy.try_shutdown, context=context)
        executor = SingleThreadedExecutor(context=context)
        cleanup.callback(executor.shutdown, timeout_sec=2.0)

        def add_node(node):
            cleanup.callback(node.destroy_node)
            executor.add_node(node)
            cleanup.callback(executor.remove_node, node)
            return node

        parameters = [Parameter('asset_json', value=json.dumps(asset))]
        driver = add_node(go2_driver.Go2Driver(
            context=context, namespace=asset['namespace'], use_global_arguments=False,
            parameter_overrides=parameters))
        cleanup.callback(driver.close)
        assert fake.initialization == (0, 'fake_sdk') and driver.sport is fake.sport
        assert driver.subscriber is fake.subscriber

        runtime = add_node(RobotRuntime(
            context=context, namespace=asset['namespace'], use_global_arguments=False,
            parameter_overrides=parameters))
        for server in runtime.action_servers:
            cleanup.callback(server.destroy)
        cleanup.callback(runtime.close)
        assert type(runtime.adapter) is Go2Adapter

        client_node = add_node(Node('go2_bridge_smoke_client', context=context, use_global_arguments=False))
        sdk = Robot(client_node, asset_id)
        cleanup.callback(sdk.close)
        results = {}
        client_node.create_subscription(
            CommandResult, asset['namespace'] + '/command_results',
            lambda message: results.__setitem__(message.request_id, message), 50)
        telemetry = client_node.create_timer(0.05, fake.subscriber.emit)
        cleanup.callback(telemetry.cancel)

        def wait(predicate, label, timeout=8.0):
            deadline = time.monotonic() + timeout
            while not predicate():
                if not context.ok() or time.monotonic() >= deadline:
                    raise AssertionError(f'Timed out waiting for {label}')
                executor.spin_once(timeout_sec=0.02)

        def result(future, label):
            wait(future.done, label)
            return future.result()

        wait(lambda: sdk._description.service_is_ready(), 'description service')
        description = result(sdk.describe(), 'description response')
        assert set(description.capabilities) == {'state', 'motion.velocity', 'motion.stop'}
        wait(lambda: sdk.state() is not None and sdk.state().ready
             and sdk.state().connected and sdk.state().pose_valid, 'fresh real-adapter state')
        state = sdk.state()
        assert state.pose.header.frame_id == asset['frames']['odom']
        assert state.twist.header.frame_id == asset['frames']['base']
        assert math.isclose(state.pose.pose.position.x, 1.0, abs_tol=1e-6)
        assert math.isclose(state.twist.twist.linear.x, 0.0, abs_tol=1e-6)
        assert math.isclose(state.twist.twist.linear.y, -0.2, abs_tol=1e-6)
        print('PASS: real Go2 capabilities and synthetic DDS state traverse driver, ROS and adapter')

        wait(lambda: sdk._lease.service_is_ready(), 'lease service')
        lease = result(sdk.acquire('fake-sdk-smoke', 30.0), 'lease acquisition')
        assert lease.success and sdk.lease_token, lease.message
        wait(lambda: sdk._navigation.server_is_ready(), 'navigation endpoint')
        target = PoseStamped()
        target.header.frame_id = asset['frames']['map']
        target.pose.orientation.w = 1.0
        handle = result(sdk.navigate(target), 'unsupported real navigation rejection')
        assert not handle.accepted, 'Real Go2 must not accept mock navigation'

        wait(lambda: client_node.count_subscribers(asset['namespace'] + '/motion/velocity') > 0
             and runtime.count_subscribers(asset['driver']['options']['cmd_vel_topic']) > 0,
             'both velocity subscriptions')
        # Pause only the runtime watchdog. The driver and fake telemetry keep
        # running, so the subsequent zero must originate in the driver watchdog.
        runtime.timer.cancel()
        first_call = len(fake.sport.calls)
        request_id = sdk.velocity(y=0.1, yaw=0.2, valid_for_s=0.2)
        wait(lambda: request_id in results, 'velocity acknowledgement')
        assert results[request_id].success, results[request_id].message
        move = (0.0, 0.1, 0.2)
        wait(lambda: any(values == move for _, values in fake.sport.calls[first_call:]),
             'mapped SportClient.Move', timeout=2.0)
        move_index = next(index for index in range(first_call, len(fake.sport.calls))
                          if fake.sport.calls[index][1] == move)
        wait(lambda: any(values == (0.0, 0.0, 0.0)
                         for _, values in fake.sport.calls[move_index + 1:]),
             'independent driver command watchdog zero', timeout=2.0)
        assert runtime.timer.is_canceled()
        assert driver.core.command is None and not driver.core.moving
        runtime.timer.reset()
        print('PASS: leased y/yaw reaches Move; driver watchdog sends zero with runtime timer paused')

        telemetry.cancel()
        source_sequence = fake.subscriber.sequence
        wait(lambda: sdk.state() is not None and not sdk.state().ready
             and not sdk.state().connected and not sdk.state().pose_valid,
             'stale telemetry clears readiness', timeout=3.0)
        assert fake.subscriber.sequence == source_sequence
        request_id = sdk.velocity(y=0.1)
        wait(lambda: request_id in results, 'stale-state command rejection')
        assert not results[request_id].success and results[request_id].code == 'NOT_READY'

        wait(lambda: sdk._stop.service_is_ready(), 'stop service')
        before_stop = len(fake.sport.calls)
        stopped = result(sdk.stop(), 'explicit stop with stale telemetry')
        assert stopped.success, stopped.message
        wait(lambda: len(fake.sport.calls) > before_stop
             and fake.sport.calls[-1][1] == (0.0, 0.0, 0.0), 'explicit stop reaches fake SDK')
        released = result(sdk.release(), 'lease release')
        assert released.success and not sdk.lease_token, released.message
        assert_vendor_sdk_absent()
        print('PASS: telemetry loss clears readiness and rejects motion; explicit stop still sends zero')

    assert fake.subscriber.closed, 'The fake SDK subscriber was not closed'
    assert fake.sport.calls[-1][1] == (0.0, 0.0, 0.0), 'Shutdown must end with zero output'
    assert_vendor_sdk_absent()
    print('All Go2 bridge checks passed: real ROS bridge with fake SDK, no hardware.')


if __name__ == '__main__':
    run()
