"""Validate generated messages and CDR serialization without creating DDS nodes."""

import math
import struct

import pytest

pytest.importorskip('rclpy')
from rclpy.qos import ReliabilityPolicy
from rclpy.serialization import deserialize_message, serialize_message

from chaos_sensors.config import STREAM_TYPES
from chaos_sensors.mock import make_message
from chaos_sensors.ros_support import MESSAGE_TYPES, publisher_qos


@pytest.mark.parametrize('message_type', STREAM_TYPES)
def test_all_mock_messages_serialize_to_the_declared_ros_type(message_type):
    message = make_message(message_type, 'test_frame')
    message.header.stamp.sec = 123
    payload = serialize_message(message)
    recovered = deserialize_message(payload, MESSAGE_TYPES[message_type])
    if message_type.endswith('/LaserScan'):
        # ROS float32 scalar fields round Python doubles.
        for field in message.get_fields_and_field_types():
            value = getattr(message, field)
            expected = pytest.approx(value) if isinstance(value, float) else value
            assert getattr(recovered, field) == expected
    else:
        assert recovered == message
    assert recovered.header.stamp.sec == 123
    assert recovered.header.frame_id == 'test_frame'


def test_image_bytes_match_dimensions_and_mock_intrinsics():
    image = make_message('sensor_msgs/msg/Image', 'optical')
    info = make_message('sensor_msgs/msg/CameraInfo', 'optical')
    assert (image.width, image.height) == (info.width, info.height) == (64, 48)
    assert len(image.data) == image.height * image.step
    assert image.step == image.width * 3
    assert info.k[0] > 0 and info.k[4] > 0
    assert image.encoding == 'rgb8'


def test_cloud_layout_and_values_are_finite():
    cloud = make_message('sensor_msgs/msg/PointCloud2', 'lidar')
    assert [field.offset for field in cloud.fields] == [0, 4, 8]
    assert len(cloud.data) == cloud.height * cloud.row_step
    assert cloud.row_step == cloud.width * cloud.point_step
    points = list(struct.iter_unpack('<fff', cloud.data))
    assert len(points) == 8
    assert all(math.isfinite(coordinate) for point in points for coordinate in point)


def test_laser_scan_angles_and_ranges_are_consistent():
    scan = make_message('sensor_msgs/msg/LaserScan', 'lidar', rate_hz=20)
    assert scan.angle_max == pytest.approx(
        scan.angle_min + (len(scan.ranges) - 1) * scan.angle_increment)
    assert all(scan.range_min < distance < scan.range_max for distance in scan.ranges)
    assert scan.scan_time == pytest.approx(0.05)


def test_imu_is_explicitly_a_known_ideal_stationary_sample():
    imu = make_message('sensor_msgs/msg/Imu', 'imu')
    assert imu.orientation.w == 1
    assert imu.linear_acceleration.z == pytest.approx(9.80665)
    assert imu.orientation_covariance[0] > 0
    assert imu.angular_velocity_covariance[0] > 0


def test_mock_publisher_qos_supports_both_stream_policies():
    assert publisher_qos('sensor_data').reliability == ReliabilityPolicy.BEST_EFFORT
    assert publisher_qos('reliable').reliability == ReliabilityPolicy.RELIABLE
