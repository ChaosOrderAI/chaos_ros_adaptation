import math
import threading
from types import SimpleNamespace

import pytest

from chaos_runtime.adapters.base import Snapshot, validate_absolute_topic
from chaos_runtime.adapters.mock import MockAdapter
from chaos_runtime.adapters.lingao import LingaoAdapter
from chaos_runtime.adapters.dji_legacy import DjiLegacyAdapter
from chaos_runtime.control import Limits, Rejected


def asset(kind='ugv'):
    return {'kind': kind, 'driver': {'plugin': 'mock', 'options': {'kind': kind}},
            'frames': {'base': 'r/base', 'odom': 'r/odom', 'map': 'r/map'}}


def test_mock_motion_stop_and_independent_instances():
    first = MockAdapter(None, asset(), Limits())
    second = MockAdapter(None, asset(), Limits())
    first.set_velocity([1.0, 0, 0, 0, 0, 0])
    first.update(0.1)
    assert first.snapshot().position[0] == pytest.approx(0.1)
    assert second.snapshot().position[0] == 0
    first.stop()
    first.update(0.1)
    assert first.snapshot().position[0] == pytest.approx(0.1)


def test_mock_target_progress_and_cancel_stop():
    mock = MockAdapter(None, asset(), Limits())
    mock.start_operation('navigation.pose', [1.0, 0.0, 0.0, 0.0])
    mock.update(0.1)
    assert 0 < mock.poll_operation()[-1] < 1
    assert mock.snapshot().velocity[0] > 0
    mock.stop()
    position = mock.snapshot().position
    mock.update(0.1)
    assert mock.snapshot().position == position
    assert not mock.poll_operation()[0]


def test_mock_navigation_converges_and_stays_stopped():
    mock = MockAdapter(None, asset(), Limits())
    mock.start_operation('navigation.pose', [0.2, 0.0, 0.0, 0.2])
    for _ in range(8):
        mock.update(0.1)
    assert mock.poll_operation()[:2] == (True, True)
    assert mock.snapshot().position == pytest.approx([0.2, 0.0, 0.0])
    assert mock.snapshot().velocity == [0.0] * 6


def test_uav_takeoff_land_and_ground_capabilities():
    drone = MockAdapter(None, asset('uav'), Limits())
    drone.start_operation('flight.takeoff', 0.2)
    for _ in range(4):
        drone.update(0.1)
    assert drone.snapshot().position[2] == pytest.approx(0.2)
    assert drone.poll_operation()[0]
    drone.start_operation('flight.land', 0)
    for _ in range(4):
        drone.update(0.1)
    assert drone.snapshot().position[2] == 0
    assert 'flight.land' not in MockAdapter.capabilities_for(asset())
    with pytest.raises(Rejected):
        MockAdapter(None, asset(), Limits()).start_operation('flight.takeoff', 1)


@pytest.mark.parametrize('height,speed', [(0.0, -0.5), (20.0, 0.5)])
def test_mock_vertical_limits_report_actual_velocity(height, speed):
    drone = MockAdapter(None, asset('uav'), Limits())
    drone.position[2] = height
    drone.set_velocity([0, 0, speed, 0, 0, 0])
    drone.update(0.1)
    assert drone.snapshot().position[2] == height
    assert drone.snapshot().velocity[2] == 0


@pytest.mark.parametrize('name', ['/bad-topic', '/bad topic', '/x//y', 'relative', '/1topic', '/'])
def test_invalid_topic_names_fail_preflight(name):
    with pytest.raises(ValueError):
        validate_absolute_topic(name, 'test')


def test_lingao_requires_explicit_topics():
    with pytest.raises(ValueError):
        LingaoAdapter.validate_options({'driver': {'plugin': 'lingao_ros2', 'options': {}}})


def test_lingao_stale_and_invalid_telemetry_is_not_ready(monkeypatch):
    adapter = LingaoAdapter.__new__(LingaoAdapter)
    adapter.limits = Limits(state_timeout_s=2)
    adapter.last_received = 100.0
    adapter.data = Snapshot(connected=True, ready=True, pose_valid=True)
    monkeypatch.setattr('chaos_runtime.adapters.lingao.time.monotonic', lambda: 101.0)
    assert adapter.snapshot().ready
    monkeypatch.setattr('chaos_runtime.adapters.lingao.time.monotonic', lambda: 102.0)
    state = adapter.snapshot()
    assert not state.connected and not state.ready and not state.pose_valid
    assert math.isnan(state.battery)


def test_dji_status_does_not_claim_control_or_pose(monkeypatch):
    adapter = DjiLegacyAdapter.__new__(DjiLegacyAdapter)
    adapter.node = SimpleNamespace(adapter_lock=threading.RLock())
    adapter.limits = Limits()
    adapter.last_received = 0
    adapter.flight_fresh = False
    monkeypatch.setattr('chaos_runtime.adapters.dji_legacy.time.monotonic', lambda: 100.0)
    adapter._on_status(SimpleNamespace(data='{"ok":true,"flight_telemetry_fresh":true}'))
    snapshot = adapter.snapshot()
    assert snapshot.connected
    assert not snapshot.ready and not snapshot.pose_valid
    adapter._on_status(SimpleNamespace(data='[]'))
    assert adapter.snapshot().connected
    monkeypatch.setattr('chaos_runtime.adapters.dji_legacy.time.monotonic', lambda: 103.0)
    assert not adapter.snapshot().connected


@pytest.mark.parametrize('payload', [
    '[' * 2000 + '0' + ']' * 2000,
    '{"ok":true,"flight_telemetry_fresh":true,"padding":"' + 'x' * 65536 + '"}',
    None,
])
def test_malformed_dji_status_cannot_refresh_freshness(payload):
    adapter = DjiLegacyAdapter.__new__(DjiLegacyAdapter)
    adapter.node = SimpleNamespace(adapter_lock=threading.RLock())
    adapter.last_received = 123.0
    adapter.flight_fresh = False
    adapter._on_status(SimpleNamespace(data=payload))
    assert adapter.last_received == 123.0 and not adapter.flight_fresh
