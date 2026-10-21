import math
import threading
from types import SimpleNamespace

import pytest
from geometry_msgs.msg import TwistStamped
from nav_msgs.msg import Odometry

from chaos_runtime.adapters.go2 import Go2Adapter
from chaos_runtime.adapters.mock import MockAdapter
from chaos_runtime.control import Limits, Rejected, validate_pose


def asset():
    return {
        'kind': 'quadruped', 'frames': {'base': 'dog/base', 'odom': 'dog/odom', 'map': 'dog/map'},
        'driver': {'plugin': 'unitree_go2', 'options': {
            'odom_topic': '/go2/odom', 'cmd_vel_topic': '/go2/cmd_vel',
            'network_interface': 'eno1', 'dds_domain_id': 0,
            'sport_state_topic': 'rt/sportmodestate', 'velocity_frame': 'odom',
        }},
    }


def adapter(monkeypatch):
    stamp = SimpleNamespace(sec=100, nanosec=0)
    clock = SimpleNamespace(now=lambda: SimpleNamespace(nanoseconds=100_000_000_000,
                                                        to_msg=lambda: stamp))
    published = []
    node = SimpleNamespace(
        adapter_lock=threading.RLock(), io_group=None, get_clock=lambda: clock,
        create_publisher=lambda *a, **k: SimpleNamespace(publish=published.append),
        create_subscription=lambda *a, **k: None,
    )
    monotonic = [10.0]
    monkeypatch.setattr('chaos_runtime.adapters.go2.time.monotonic', lambda: monotonic[0])
    result = Go2Adapter(node, asset(), Limits(state_timeout_s=1.0))
    # Generated ROS messages require a generated builtin Time, not SimpleNamespace.
    from builtin_interfaces.msg import Time
    clock.now = lambda: SimpleNamespace(nanoseconds=100_000_000_000, to_msg=lambda: Time(sec=100))
    return result, published, monotonic


def odom():
    msg = Odometry()
    msg.header.stamp.sec = 100
    msg.header.frame_id = 'dog/odom'
    msg.child_frame_id = 'dog/base'
    msg.pose.pose.orientation.w = 1.0
    msg.twist.twist.linear.y = 0.2
    return msg


@pytest.mark.parametrize('name,value', [
    ('cmd_vel_topic', 'relative'), ('odom_topic', '/bad-topic'),
    ('network_interface', ''), ('network_interface', 'a' * 16),
    ('dds_domain_id', True), ('dds_domain_id', -1), ('dds_domain_id', 233),
    ('sport_state_topic', 'rt/lowstate'), ('velocity_frame', 'map'), ('typo', 'value'),
])
def test_invalid_driver_options_fail_before_hardware(name, value):
    data = asset()
    data['driver']['options'][name] = value
    with pytest.raises(ValueError):
        Go2Adapter.validate_options(data)


def test_explicit_options_and_supported_capabilities():
    data = asset()
    Go2Adapter.validate_options(data)
    assert Go2Adapter.capabilities_for(data) == {'state', 'motion.velocity', 'motion.stop'}
    del data['driver']['options']['network_interface']
    with pytest.raises(ValueError, match='requires explicit'):
        Go2Adapter.validate_options(data)


def test_fresh_odometry_source_stamp_and_expiry(monkeypatch):
    dog, _, monotonic = adapter(monkeypatch)
    assert not dog.snapshot().ready
    msg = odom()
    msg.header.stamp.sec = 99
    msg.header.stamp.nanosec = 500_000_000
    dog._on_odom(msg)
    state = dog.snapshot()
    assert state.ready and state.pose_valid
    assert state.velocity[1] == 0.2 and math.isnan(state.battery)
    assert state.source_stamp == msg.header.stamp
    monotonic[0] += 0.5  # Transit consumes half of the state timeout.
    assert not dog.snapshot().connected


@pytest.mark.parametrize('mutate', [
    lambda m: setattr(m.header, 'frame_id', 'wrong'),
    lambda m: setattr(m, 'child_frame_id', 'wrong'),
    lambda m: setattr(m.header.stamp, 'sec', 99),
    lambda m: setattr(m.header.stamp, 'sec', 101),
    lambda m: setattr(m.pose.pose.orientation, 'w', 0.0),
    lambda m: setattr(m.pose.pose.position, 'x', math.nan),
])
def test_bad_telemetry_invalidates_previous_ready_sample(monkeypatch, mutate):
    dog, _, _ = adapter(monkeypatch)
    dog._on_odom(odom())
    assert dog.snapshot().ready
    bad = odom()
    mutate(bad)
    dog._on_odom(bad)
    assert not dog.snapshot().ready


def test_stamped_lateral_commands_and_owned_stop(monkeypatch):
    dog, published, _ = adapter(monkeypatch)
    dog.close()
    assert not published
    dog.set_velocity([0.1, 0.2, 0, 0, 0, -0.3])
    msg = published[-1]
    assert isinstance(msg, TwistStamped)
    assert msg.header.frame_id == 'dog/base' and msg.header.stamp.sec == 100
    assert (msg.twist.linear.x, msg.twist.linear.y, msg.twist.angular.z) == (0.1, 0.2, -0.3)
    dog.stop()
    assert published[-1].twist == TwistStamped().twist
    dog.stop()
    assert len(published) == 2
    dog.controlled_stop()
    assert len(published) == 3  # Explicit stop works even without prior motion.


def test_stop_publish_failure_retains_output_ownership(monkeypatch):
    dog, _, _ = adapter(monkeypatch)
    dog.set_velocity([0, 0.1, 0, 0, 0, 0])
    def fail(_):
        raise RuntimeError('transport failure')
    dog._publisher.publish = fail
    with pytest.raises(RuntimeError):
        dog.stop()
    assert dog.output_owned


@pytest.mark.parametrize('values', [[0, 0, 0.1, 0, 0, 0], [0, 0, 0, 0.1, 0, 0],
                                   [0, 0, 0, 0, 0.1, 0], [math.nan] * 6])
def test_unsupported_velocity_never_publishes(monkeypatch, values):
    dog, published, _ = adapter(monkeypatch)
    with pytest.raises(Rejected):
        dog.set_velocity(values)
    assert not published


def test_quadruped_mock_is_holonomic_and_cannot_fly():
    data = asset()
    data['driver'] = {'plugin': 'mock', 'options': {'kind': 'quadruped'}}
    MockAdapter.validate_options(data)
    dog = MockAdapter(None, data, Limits())
    dog.set_velocity([0, 0.2, 0, 0, 0, 0])
    dog.update(0.1)
    assert dog.snapshot().position == pytest.approx([0, 0.02, 0])
    assert 'flight.takeoff' not in dog.capabilities_for(data)
    with pytest.raises(Rejected):
        dog.validate_velocity([0, 0, 0.1, 0, 0, 0])
    with pytest.raises(Rejected):
        dog.start_operation('flight.takeoff', 1)
    with pytest.raises(Rejected):
        validate_pose('map', 'map', [0, 0, 1], [0, 0, 0, 1], 'quadruped', 20)
