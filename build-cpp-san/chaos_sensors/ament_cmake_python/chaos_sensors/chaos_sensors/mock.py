"""Synthetic messages for integration tests; no hardware or physical simulation.

The 64x48 images, camera intrinsics, ideal stationary IMU, planar point cloud and
constant ranges are deliberately synthetic and are not calibration data.
"""

import math
import struct

from rclpy.node import Node
from sensor_msgs.msg import PointField

from .config import read_json, streams
from .ros_support import MESSAGE_TYPES, publisher_qos, run


def make_message(message_type: str, frame_id: str, rate_hz: float = 10.0):
    message = MESSAGE_TYPES[message_type]()
    message.header.frame_id = frame_id
    if message_type.endswith('/Image'):
        message.height, message.width = 48, 64
        message.encoding = 'rgb8'
        message.is_bigendian = 0
        message.step = message.width * 3
        message.data = bytes(component for y in range(message.height)
                             for x in range(message.width)
                             for component in (x * 4, y * 5, 96))
    elif message_type.endswith('/CameraInfo'):
        message.height, message.width = 48, 64
        message.distortion_model = 'plumb_bob'
        message.d = [0.0] * 5
        message.k = [40.0, 0.0, 31.5, 0.0, 40.0, 23.5, 0.0, 0.0, 1.0]
        message.r = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
        message.p = [40.0, 0.0, 31.5, 0.0, 0.0, 40.0, 23.5, 0.0,
                     0.0, 0.0, 1.0, 0.0]
    elif message_type.endswith('/PointCloud2'):
        points = [(2.0 * math.cos(i * math.pi / 4), 2.0 * math.sin(i * math.pi / 4),
                   0.0) for i in range(8)]
        message.height, message.width = 1, len(points)
        message.fields = [PointField(name=name, offset=index * 4,
                                     datatype=PointField.FLOAT32, count=1)
                          for index, name in enumerate(('x', 'y', 'z'))]
        message.is_bigendian = False
        message.point_step = 12
        message.row_step = message.point_step * message.width
        message.is_dense = True
        message.data = b''.join(struct.pack('<fff', *point) for point in points)
    elif message_type.endswith('/LaserScan'):
        message.angle_min, message.angle_max = -math.pi, math.pi
        message.angle_increment = math.pi / 18
        message.time_increment = 0.0  # One synthetic instantaneous sample.
        message.scan_time = 1.0 / rate_hz
        message.range_min, message.range_max = 0.1, 10.0
        message.ranges = [2.0] * 37
        message.intensities = [1.0] * 37
    elif message_type.endswith('/Imu'):
        message.orientation.w = 1.0
        message.linear_acceleration.z = 9.80665
        message.orientation_covariance = [0.001, 0.0, 0.0, 0.0, 0.001, 0.0,
                                          0.0, 0.0, 0.001]
        message.angular_velocity_covariance = [0.0001, 0.0, 0.0, 0.0, 0.0001, 0.0,
                                               0.0, 0.0, 0.0001]
        message.linear_acceleration_covariance = [0.001, 0.0, 0.0, 0.0, 0.001, 0.0,
                                                  0.0, 0.0, 0.001]
    return message


class MockSensor(Node):
    def __init__(self):
        super().__init__('mock_sensor')
        self.declare_parameter('sensor_json', '{}')
        sensor = read_json(self.get_parameter('sensor_json').value)
        if sensor.get('driver', {}).get('mode') != 'mock':
            raise ValueError('mock_sensor requires driver.mode=mock')
        self._stream_timers = []
        for _, stream in streams(sensor):
            publisher = self.create_publisher(
                MESSAGE_TYPES[stream['type']], stream['topic'],
                publisher_qos(stream.get('qos', 'sensor_data')))
            rate = stream.get('rate_hz', 10.0)
            message = make_message(stream['type'], stream['frame_id'], rate)
            self._stream_timers.append(self.create_timer(
                1.0 / rate,
                lambda publisher=publisher, message=message: self._publish(publisher, message)))

    def _publish(self, publisher, message):
        message.header.stamp = self.get_clock().now().to_msg()
        publisher.publish(message)


def main(args=None):
    run(MockSensor, args)
