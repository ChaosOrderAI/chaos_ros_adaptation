"""Attach to a separately started Lingao ROS driver. No process/hardware startup."""
import math
import time

from chaos_runtime.adapters.base import Adapter, Snapshot, validate_absolute_topic
from chaos_runtime.control import Rejected


class LingaoAdapter(Adapter):
    supported_capabilities = frozenset({'state', 'motion.velocity', 'motion.stop'})
    option_names = frozenset({'odom_topic', 'cmd_vel_topic'})

    @classmethod
    def validate_options(cls, asset):
        super().validate_options(asset)
        if not {'odom_topic', 'cmd_vel_topic'} <= set(asset['driver'].get('options', {})):
            raise ValueError('Lingao requires explicit absolute odom_topic and cmd_vel_topic options')
        for name, value in asset['driver'].get('options', {}).items():
            validate_absolute_topic(value, f'Lingao {name}')

    def __init__(self, node, asset, limits):
        from geometry_msgs.msg import Twist
        from nav_msgs.msg import Odometry
        from rclpy.qos import qos_profile_sensor_data
        super().__init__(node, asset, limits)
        options = asset['driver'].get('options', {})
        self._twist_type = Twist
        self._publisher = node.create_publisher(Twist, options.get('cmd_vel_topic', '/cmd_vel'), 10)
        self._subscription = node.create_subscription(
            Odometry, options.get('odom_topic', '/lingao/odom'), self._on_odom,
            qos_profile_sensor_data, callback_group=node.io_group)
        self.last_received = 0.0
        self.data = Snapshot()
        self.output_owned = False

    def _on_odom(self, msg):
        p, q, t = msg.pose.pose.position, msg.pose.pose.orientation, msg.twist.twist
        values = [p.x, p.y, p.z, q.x, q.y, q.z, q.w,
                  t.linear.x, t.linear.y, t.linear.z, t.angular.x, t.angular.y, t.angular.z]
        with self.node.adapter_lock:
            expected = self.asset['frames']
            if (msg.header.frame_id != expected['odom'] or msg.child_frame_id != expected['base']
                    or not all(math.isfinite(v) for v in values)
                    or abs(math.hypot(q.x, q.y, q.z, q.w) - 1) > 0.01):
                self.data = Snapshot(detail='Invalid odometry values or frame mismatch')
                self.last_received = 0.0
                return
            source_s = msg.header.stamp.sec + msg.header.stamp.nanosec / 1e9
            age = self.node.get_clock().now().nanoseconds / 1e9 - source_s
            if source_s <= 0 or age < -0.1 or age >= self.limits.state_timeout_s:
                self.data = Snapshot(detail='Source odometry timestamp is stale or invalid')
                self.last_received = 0.0
                return
            # Account for source age so receiving an old sample cannot refresh it.
            self.last_received = time.monotonic() - max(age, 0.0)
            self.data = Snapshot(
                connected=True, ready=True, pose_valid=True, position=values[:3],
                quaternion=values[3:7], velocity=values[7:],
                detail='Attached to Lingao odometry; driver must be independently running',
                pose_frame=msg.header.frame_id, twist_frame=msg.child_frame_id,
                source_stamp=msg.header.stamp,
            )

    def snapshot(self):
        if not self.last_received or time.monotonic() - self.last_received >= self.limits.state_timeout_s:
            return Snapshot(detail='Lingao odometry unavailable or stale')
        return self.data

    def validate_velocity(self, values):
        if any(abs(values[index]) > 1e-9 for index in (1, 2, 3, 4)):
            raise Rejected('UNSUPPORTED', 'Lingao accepts linear x and angular z only')

    def set_velocity(self, values):
        self.validate_velocity(values)
        message = self._twist_type()
        message.linear.x, message.angular.z = values[0], values[5]
        self.output_owned = True
        self._publisher.publish(message)

    def stop(self):
        # A configured stop command sets output_owned in runtime; shutdown alone
        # does not touch an independently controlled driver we never commanded.
        if self.output_owned:
            self._publisher.publish(self._twist_type())
            self.output_owned = False

    def controlled_stop(self):
        self.output_owned = True
        self.stop()
