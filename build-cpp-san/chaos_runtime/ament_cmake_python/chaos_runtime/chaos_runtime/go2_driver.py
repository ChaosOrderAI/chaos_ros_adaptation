"""Independent ROS 2 process bridging a configured Go2 to Unitree SDK2.

The vendor package is installed separately; neither imports nor sys.path depend
on the original sbdog workspace. This driver never changes robot posture/mode.
"""

import json
import signal

import rclpy
from geometry_msgs.msg import TwistStamped
from nav_msgs.msg import Odometry
from rclpy.clock import Clock, ClockType
from rclpy.node import Node
from rclpy.qos import QoSProfile
from rclpy.signals import SignalHandlerOptions

from chaos_runtime.adapters.go2 import Go2Adapter
from chaos_runtime.go2_driver_core import Go2DriverCore, fill_odometry


def load_sdk():
    """Only the hardware driver process requires Cyclone DDS and Unitree SDK2."""
    try:
        from unitree_sdk2py.core.channel import ChannelFactoryInitialize, ChannelSubscriber
        from unitree_sdk2py.go2.sport.sport_client import SportClient
        from unitree_sdk2py.idl.unitree_go.msg.dds_ import SportModeState_
    except ImportError as error:
        raise RuntimeError('Install unitree_sdk2_python in this ROS environment before starting go2_driver') from error
    return ChannelFactoryInitialize, ChannelSubscriber, SportClient, SportModeState_


class Go2Driver(Node):
    def __init__(self, **kwargs):
        super().__init__('go2_driver', **kwargs)
        self.declare_parameter('asset_json', '{}')
        self.asset = json.loads(self.get_parameter('asset_json').value)
        if self.asset.get('driver', {}).get('plugin') != 'unitree_go2':
            raise ValueError('go2_driver requires a unitree_go2 asset')
        if self.asset.get('mode') != 'real':
            raise ValueError('go2_driver requires a real asset; mock mode must not load the SDK')
        Go2Adapter.validate_options(self.asset)
        domain_id = self.asset.get('ros_domain_id')
        if type(domain_id) is not int or domain_id != self.context.get_domain_id():
            raise ValueError('Go2 asset ros_domain_id does not match the active ROS context')
        options = self.asset['driver']['options']
        interface, domain = options['network_interface'], options['dds_domain_id']
        sport_topic = options['sport_state_topic']
        cmd_topic, odom_topic = options['cmd_vel_topic'], options['odom_topic']
        self.core = Go2DriverCore(self.asset, ros_clock=lambda: self.get_clock().now().nanoseconds)
        self.odom_pub = self.create_publisher(Odometry, odom_topic, QoSProfile(depth=1))
        self.create_subscription(TwistStamped, cmd_topic, self._command, QoSProfile(depth=1))
        self.sport = self.subscriber = self.timer = None
        self.closing = False
        self.logged_fault = ''
        try:
            initialize, subscriber_type, sport_type, state_type = load_sdk()
            initialize(domain, interface)
            self.sport = sport_type()
            # Move is no-reply but the SDK writer still waits for a matched
            # subscriber, in 0.1s increments. Zero timeout always fails; this
            # positive value bounds that wait to one increment. Never use the
            # synchronous StopMove, Damp, or StandUp paths here.
            self.sport.SetTimeout(0.05)
            self.sport.Init()
            self.subscriber = subscriber_type(sport_topic, state_type)
            self.subscriber.Init(self.core.accept_state, 1)
            self.timer = self.create_timer(0.05, self._tick,
                                           clock=Clock(clock_type=ClockType.STEADY_TIME))
        except BaseException:
            self.close()
            raise
        self.get_logger().info(f'{self.asset["id"]}: Go2 driver started on {interface}; waiting for fresh telemetry')

    def _command(self, message):
        if not self.core.accept_command(message):
            self.get_logger().warning(self.core.detail, throttle_duration_sec=2.0)

    def _tick(self):
        sample = self.core.step(self.sport.Move)
        if self.core.fault and self.core.fault != self.logged_fault:
            self.logged_fault = self.core.fault
            self.get_logger().error(self.logged_fault + '; motion disabled until driver restart')
        elif self.core.output_error and not self.core.initialized:
            self.get_logger().warning(self.core.output_error + '; retrying initial zero output',
                                      throttle_duration_sec=2.0)
        if sample is not None:
            self.odom_pub.publish(fill_odometry(Odometry(), sample,
                                               odom_frame=self.core.odom_frame,
                                               base_frame=self.core.base_frame))

    def close(self):
        if self.closing:
            return
        self.closing = True
        if self.timer is not None:
            self.timer.cancel()
        self.core.request_stop(closing=True)
        if self.sport is not None:
            for _ in range(3):
                self.core.step(self.sport.Move)
                if not self.core.stop_pending:
                    break
            if self.core.stop_pending:
                self.get_logger().error('Shutdown zero output failed after three attempts')
        if self.subscriber is not None:
            # SDK Close joins its reader thread. Never hold the core lock while
            # waiting: an in-flight DDS callback may need that lock to finish.
            self.subscriber.Close()


def main(args=None):
    rclpy.init(args=args, signal_handler_options=SignalHandlerOptions.NO)
    node = None
    previous_handlers = {}

    def stop_signal(signum, frame):
        # Unwind any active SDK write before sending the final zero in finally.
        raise KeyboardInterrupt

    for signum in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[signum] = signal.signal(signum, stop_signal)
    try:
        node = Go2Driver()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.close()
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
