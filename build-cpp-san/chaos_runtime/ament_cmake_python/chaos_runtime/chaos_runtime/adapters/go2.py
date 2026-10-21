"""Standard ROS boundary to the separately supervised Unitree Go2 driver."""
import math
import re
import time

from chaos_runtime.adapters.base import Adapter, Snapshot, validate_absolute_topic
from chaos_runtime.control import Rejected


class Go2Adapter(Adapter):
    supported_capabilities = frozenset({'state', 'motion.velocity', 'motion.stop'})
    option_names = frozenset({
        'odom_topic', 'cmd_vel_topic', 'network_interface', 'dds_domain_id',
        'sport_state_topic', 'velocity_frame',
    })

    @classmethod
    def validate_options(cls, asset):
        super().validate_options(asset)
        options = asset['driver'].get('options', {})
        if cls.option_names - options.keys():
            raise ValueError(f'Go2 requires explicit options: {sorted(cls.option_names - options.keys())}')
        if asset.get('kind') != 'quadruped':
            raise ValueError('Go2 requires kind quadruped')
        for name in ('odom_topic', 'cmd_vel_topic'):
            validate_absolute_topic(options[name], f'Go2 {name}')
        if options['odom_topic'] == options['cmd_vel_topic']:
            raise ValueError('Go2 odom_topic and cmd_vel_topic must differ')
        interface = options['network_interface']
        if not isinstance(interface, str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,15}', interface):
            raise ValueError('Go2 network_interface must name an explicit network interface')
        domain = options['dds_domain_id']
        if type(domain) is not int or not 0 <= domain <= 232:
            raise ValueError('Go2 dds_domain_id must be an integer from 0 to 232')
        if options['sport_state_topic'] not in ('rt/sportmodestate', 'rt/lf/sportmodestate'):
            raise ValueError('Go2 sport_state_topic must be rt/sportmodestate or rt/lf/sportmodestate')
        if options['velocity_frame'] not in ('odom', 'body'):
            raise ValueError('Go2 velocity_frame must be odom or body')

    def __init__(self, node, asset, limits):
        from geometry_msgs.msg import TwistStamped
        from nav_msgs.msg import Odometry
        from rclpy.qos import qos_profile_sensor_data
        super().__init__(node, asset, limits)
        options = asset['driver']['options']
        self._twist_type = TwistStamped
        # A single pending command cannot accumulate a queue of old motion.
        self._publisher = node.create_publisher(TwistStamped, options['cmd_vel_topic'], 1)
        self.last_received = None
        self.data = Snapshot()
        self.output_owned = False
        self._subscription = node.create_subscription(
            Odometry, options['odom_topic'], self._on_odom, qos_profile_sensor_data,
            callback_group=node.io_group)

    def _on_odom(self, msg):
        p, q, t = msg.pose.pose.position, msg.pose.pose.orientation, msg.twist.twist
        values = [p.x, p.y, p.z, q.x, q.y, q.z, q.w,
                  t.linear.x, t.linear.y, t.linear.z, t.angular.x, t.angular.y, t.angular.z]
        with self.node.adapter_lock:
            frames = self.asset['frames']
            source_s = msg.header.stamp.sec + msg.header.stamp.nanosec / 1e9
            age = self.node.get_clock().now().nanoseconds / 1e9 - source_s
            if (msg.header.frame_id != frames['odom'] or msg.child_frame_id != frames['base']
                    or not all(math.isfinite(v) for v in values)
                    or abs(math.hypot(*values[3:7]) - 1) > 0.01
                    or source_s <= 0 or age < -0.1 or age >= self.limits.state_timeout_s):
                self.last_received = None
                self.data = Snapshot(detail='Go2 odometry invalid, stale or frame mismatch')
                return
            self.last_received = time.monotonic() - max(age, 0.0)
            self.data = Snapshot(
                connected=True, ready=True, pose_valid=True,
                position=values[:3], quaternion=values[3:7], velocity=values[7:],
                pose_frame=msg.header.frame_id, twist_frame=msg.child_frame_id,
                source_stamp=msg.header.stamp,
                detail='Go2 DDS telemetry via managed driver; movement requires an operable sport mode',
            )

    def snapshot(self):
        if (self.last_received is None
                or time.monotonic() - self.last_received >= self.limits.state_timeout_s):
            return Snapshot(detail='Go2 driver telemetry unavailable or stale')
        return self.data

    def validate_velocity(self, values):
        if len(values) != 6 or not all(math.isfinite(v) for v in values):
            raise Rejected('INVALID_ARGUMENT', 'Go2 velocity must contain six finite components')
        if any(abs(values[index]) > 1e-9 for index in (2, 3, 4)):
            raise Rejected('UNSUPPORTED', 'Go2 accepts linear x/y and angular z only')

    def _command(self, values):
        message = self._twist_type()
        message.header.stamp = self.node.get_clock().now().to_msg()
        message.header.frame_id = self.asset['frames']['base']
        message.twist.linear.x, message.twist.linear.y = values[:2]
        message.twist.angular.z = values[5]
        self._publisher.publish(message)

    def set_velocity(self, values):
        self.validate_velocity(values)
        self.output_owned = True
        self._command(values)

    def stop(self):
        if self.output_owned:
            self._command([0.0] * 6)
            self.output_owned = False

    def controlled_stop(self):
        self.output_owned = True
        self.stop()
