import math
from types import SimpleNamespace
import pytest

from chaos_runtime.control import CommandDeadline, ControlGuard, Limits, Rejected, validate_pose


class Clock:
    value = 100.0

    def __call__(self):
        return self.value


@pytest.fixture
def control():
    clock = Clock()
    return ControlGuard(Limits(), clock, lambda: 'secret'), clock


def assert_rejected(code, callback):
    with pytest.raises(Rejected) as error:
        callback()
    assert error.value.code == code


def command(guard, **overrides):
    kwargs = dict(token='secret', stamp_s=200.0, ros_now_s=200.0,
                  valid_for_s=0.5, frame='robot/base', base_frame='robot/base',
                  values=[0.5, 0.0, 0.0, 0.0, 0.0, 0.0])
    kwargs.update(overrides)
    guard.velocity(**kwargs)


def test_lease_contention_renewal_and_exact_expiry(control):
    guard, clock = control
    assert guard.lease('acquire', 'operator', duration_s=5) == ('secret', 5.0)
    assert_rejected('BUSY', lambda: guard.lease('acquire', 'other'))
    assert_rejected('NO_CONTROL', lambda: guard.lease('renew', 'other', 'secret', 5))
    clock.value += 4
    guard.lease('renew', 'operator', 'secret', 5)
    assert guard.remaining == 5
    clock.value += 5
    assert_rejected('NO_CONTROL', lambda: guard.require('secret'))
    assert guard.lease('acquire', 'other', duration_s=3)[1] == 3


@pytest.mark.parametrize('token', ['', 'wrong', '🔒', None])
def test_malformed_tokens_rejected_without_exception_leak(control, token):
    guard, _ = control
    guard.lease('acquire', 'operator')
    assert_rejected('NO_CONTROL', lambda: guard.require(token))


@pytest.mark.parametrize('duration', [0, -1, math.nan, math.inf, True])
def test_invalid_lease_duration(control, duration):
    guard, _ = control
    assert_rejected('INVALID_ARGUMENT', lambda: guard.lease('acquire', 'operator', duration_s=duration))


def test_bounded_lease_and_owner_release(control):
    guard, _ = control
    assert_rejected('OUT_OF_RANGE', lambda: guard.lease('acquire', 'operator', duration_s=31))
    guard.lease('acquire', 'operator')
    assert_rejected('NO_CONTROL', lambda: guard.lease('release', 'other', 'secret'))
    guard.lease('release', 'operator', 'secret', 0)
    assert guard.remaining == 0


def test_idle_watchdog_and_transit_consumes_ttl(control):
    guard, clock = control
    assert not guard.velocity_timed_out()
    guard.lease('acquire', 'operator')
    command(guard, ros_now_s=200.3)
    assert guard.velocity_expires == pytest.approx(100.2)
    clock.value += 0.19
    assert not guard.velocity_timed_out()
    clock.value += 0.02
    assert guard.velocity_timed_out()


def test_monotonic_watchdog_independent_of_source_clock(control):
    guard, clock = control
    guard.lease('acquire', 'operator')
    command(guard)
    guard.lease('renew', 'operator', 'secret', 20)
    clock.value += 0.5
    assert guard.velocity_timed_out()  # Renew never extends a velocity sample.


@pytest.mark.parametrize('overrides,code', [
    ({'frame': 'map'}, 'INVALID_FRAME'),
    ({'values': [math.nan, 0, 0, 0, 0, 0]}, 'INVALID_ARGUMENT'),
    ({'values': [0.8, 0.8, 0, 0, 0, 0]}, 'OUT_OF_RANGE'),
    ({'values': [0, 0, 0, 0, 0, 1.01]}, 'OUT_OF_RANGE'),
    ({'valid_for_s': 0.6}, 'OUT_OF_RANGE'),
    ({'valid_for_s': -1}, 'INVALID_ARGUMENT'),
    ({'ros_now_s': 200.5}, 'STALE_COMMAND'),
    ({'stamp_s': 201.0}, 'STALE_COMMAND'),
    ({'stamp_s': 0.0}, 'STALE_COMMAND'),
    ({'stamp_s': math.nan}, 'STALE_COMMAND'),
])
def test_velocity_rejections(control, overrides, code):
    guard, _ = control
    guard.lease('acquire', 'operator')
    assert_rejected(code, lambda: command(guard, **overrides))
    assert guard.velocity_expires == 0


def test_invalid_packet_cannot_extend_valid_command(control):
    guard, clock = control
    guard.lease('acquire', 'operator')
    command(guard)
    deadline = guard.velocity_expires
    clock.value += 0.2
    assert_rejected('INVALID_FRAME', lambda: command(guard, frame='wrong'))
    assert guard.velocity_expires == deadline


def test_pose_contract():
    validate_pose('map', 'map', (1, 2, 0), (0, 0, 0, 1), 'ugv', 20)
    assert_rejected('INVALID_ARGUMENT', lambda: validate_pose('map', 'map', (1, 2, 0), (0, 0, 0, 0), 'ugv', 20))
    assert_rejected('UNSUPPORTED', lambda: validate_pose('map', 'map', (1, 2, 1), (0, 0, 0, 1), 'ugv', 20))
    assert_rejected('OUT_OF_RANGE', lambda: validate_pose('map', 'map', (1, 2, 21), (0, 0, 0, 1), 'uav', 20))


def test_deadline_backward_wall_clock_change_does_not_extend_ttl(monkeypatch):
    wall, monotonic = [100_000_000_000], [5_000_000_000]
    monkeypatch.setattr('chaos_runtime.control.time.time_ns', lambda: wall[0])
    monkeypatch.setattr('chaos_runtime.control.time.monotonic_ns', lambda: monotonic[0])
    deadline = CommandDeadline.capture(SimpleNamespace(sec=100, nanosec=10_000_000))
    deadline.require()
    wall[0] -= 1_000_000_000
    monotonic[0] += 10_000_000
    assert_rejected('STALE_COMMAND', deadline.require)


@pytest.mark.parametrize('sec,nanosec', [(-1, 0), (0, 1_000_000_000), (1, -1)])
def test_invalid_deadline_is_rejected(sec, nanosec):
    deadline = CommandDeadline.capture(SimpleNamespace(sec=sec, nanosec=nanosec))
    assert_rejected('INVALID_ARGUMENT', deadline.require)


def test_zero_deadline_preserves_direct_ros_compatibility():
    CommandDeadline.capture(SimpleNamespace(sec=0, nanosec=0)).require()
