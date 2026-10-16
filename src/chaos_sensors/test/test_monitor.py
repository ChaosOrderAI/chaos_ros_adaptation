"""Exercise the actual diagnostics publication path without starting DDS."""

from types import SimpleNamespace

import pytest

pytest.importorskip('rclpy')
from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
from rclpy.serialization import deserialize_message, serialize_message

from chaos_sensors.health import HealthTracker
from chaos_sensors.monitor import SensorMonitor


def test_diagnostic_array_publication_serializes_all_health_levels():
    now = SimpleNamespace(steady=0.0, ros_ns=100_000_000_000)

    def tracker():
        return HealthTracker('imu_frame', 2.0,
                             lambda: now.ros_ns, lambda: now.steady)

    fresh = tracker()
    fresh.observe(sec=100, nanosec=0, frame_id='imu_frame')
    wrong_frame = tracker()
    wrong_frame.observe(sec=100, nanosec=0, frame_id='incorrect_frame')
    no_data = tracker()
    stream = {'topic': '/robot/imu/data', 'type': 'sensor_msgs/msg/Imu'}
    published = []

    def publish(message):
        # A Python int for DiagnosticStatus.level can construct successfully but
        # abort the process in the C serializer. Verify the production byte field
        # before serialization so a regression reports a normal test failure.
        assert all(isinstance(status.level, bytes) and len(status.level) == 1
                   for status in message.status)
        payload = serialize_message(message)
        published.append(deserialize_message(payload, DiagnosticArray))

    stamp = DiagnosticArray().header.stamp
    stamp.sec = 100
    node = SimpleNamespace(
        _asset_id='robot',
        _health=[('imu', name, stream, health) for name, health in (
            ('fresh', fresh), ('wrong_frame', wrong_frame), ('no_data', no_data))],
        _diagnostics=SimpleNamespace(publish=publish),
        get_clock=lambda: SimpleNamespace(now=lambda: SimpleNamespace(to_msg=lambda: stamp)),
    )
    SensorMonitor._publish_health(node)
    assert len(published) == 1
    message = published[0]
    assert message.header.stamp.sec == 100
    assert [status.level for status in message.status] == [
        DiagnosticStatus.OK, DiagnosticStatus.ERROR, DiagnosticStatus.STALE]
    assert [status.name for status in message.status] == [
        'robot/sensors/imu/fresh', 'robot/sensors/imu/wrong_frame',
        'robot/sensors/imu/no_data']
    assert dict((item.key, item.value) for item in message.status[1].values)['frame'] == 'mismatch'

    # Receipt timeout changes the previously healthy stream to STALE and remains
    # a serializable DiagnosticArray even while the ROS clock does not advance.
    now.steady = 3.0
    SensorMonitor._publish_health(node)
    assert published[1].status[0].level == DiagnosticStatus.STALE
