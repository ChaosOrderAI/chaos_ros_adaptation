"""Report per-stream metadata health without altering robot control or readiness."""

import json

from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
from rclpy.clock import Clock, ClockType
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, qos_profile_sensor_data
from std_msgs.msg import String

from .config import enabled_sensors, public_description, read_json, streams
from .health import HealthTracker
from .ros_support import MESSAGE_TYPES, run


class SensorMonitor(Node):
    def __init__(self):
        super().__init__('sensor_monitor')
        self.declare_parameter('asset_json', '{}')
        asset = read_json(self.get_parameter('asset_json').value)
        self._asset_id = asset['id']
        namespace = asset['namespace'].rstrip('/')
        self._health = []
        self._stream_subscriptions = []
        for sensor_id, sensor in enabled_sensors(asset):
            for stream_id, stream in streams(sensor):
                if not stream.get('monitor', True):
                    continue
                tracker = HealthTracker(
                    stream['frame_id'], stream.get('timeout_s', 2.0),
                    ros_now_ns=lambda: self.get_clock().now().nanoseconds)
                self._health.append((sensor_id, stream_id, stream, tracker))
                # BEST_EFFORT readers accept both BEST_EFFORT and RELIABLE writers.
                # rclpy deserializes entire messages: disable image/point-cloud
                # monitoring when this extra subscriber would cost too much.
                self._stream_subscriptions.append(self.create_subscription(
                    MESSAGE_TYPES[stream['type']], stream['topic'],
                    lambda message, tracker=tracker: tracker.observe(
                        sec=message.header.stamp.sec,
                        nanosec=message.header.stamp.nanosec,
                        frame_id=message.header.frame_id), qos_profile_sensor_data))
        self._diagnostics = self.create_publisher(
            DiagnosticArray, namespace + '/sensors/diagnostics', 10)
        self._description = self.create_publisher(
            String, namespace + '/sensors/description',
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        self._description.publish(String(data=json.dumps(
            public_description(asset), ensure_ascii=False, separators=(',', ':'))))
        # Receipt health still updates while a simulated ROS clock is paused.
        self._steady_clock = Clock(clock_type=ClockType.STEADY_TIME)
        self._diagnostic_timer = self.create_timer(
            1.0, self._publish_health, clock=self._steady_clock)

    def _publish_health(self):
        message = DiagnosticArray()
        message.header.stamp = self.get_clock().now().to_msg()
        for sensor_id, stream_id, stream, tracker in self._health:
            health = tracker.snapshot()
            values = dict(health.values, topic=stream['topic'], message_type=stream['type'])
            message.status.append(DiagnosticStatus(
                # diagnostic_msgs uses ROS "byte" (Python bytes), not uint8/int.
                level=bytes([health.level]),
                name=f'{self._asset_id}/sensors/{sensor_id}/{stream_id}',
                message=health.message, hardware_id=f'{self._asset_id}/{sensor_id}',
                values=[KeyValue(key=key, value=value) for key, value in values.items()]))
        self._diagnostics.publish(message)


def main(args=None):
    run(SensorMonitor, args)
