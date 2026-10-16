"""Shared ROS message lookup, QoS, and orderly executable lifecycle."""

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.qos import QoSProfile, ReliabilityPolicy, qos_profile_sensor_data
from sensor_msgs.msg import CameraInfo, Image, Imu, LaserScan, PointCloud2


MESSAGE_TYPES = {f'sensor_msgs/msg/{message.__name__}': message for message in (
    Image, CameraInfo, PointCloud2, LaserScan, Imu)}


def publisher_qos(name: str) -> QoSProfile:
    if name == 'sensor_data':
        return qos_profile_sensor_data
    if name == 'reliable':
        return QoSProfile(depth=5, reliability=ReliabilityPolicy.RELIABLE)
    raise ValueError('unsupported sensor QoS')


def run(node_type, args=None):
    rclpy.init(args=args)
    node = None
    try:
        node = node_type()
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
