"""Exercise executor edge cases without creating nodes or DDS participants."""
import asyncio
import threading
import time
from types import SimpleNamespace

import pytest

pytest.importorskip('rclpy')
pytest.importorskip('chaos_interfaces.action')

from rclpy.action import CancelResponse, GoalResponse
from chaos_interfaces.action import Flight
from chaos_interfaces.srv import ControlLease, Stop
from chaos_runtime.adapters.base import Snapshot
from chaos_runtime.control import ControlGuard, Limits
from chaos_runtime.node import RobotRuntime


class FakeAdapter:
    def __init__(self):
        self.fail_stop = False
        self.starts = 0
        self.stops = 0

    def stop(self):
        self.stops += 1
        if self.fail_stop:
            raise RuntimeError('injected stop failure')

    controlled_stop = stop

    def snapshot(self):
        return Snapshot(connected=True, ready=True, pose_valid=True)

    def start_operation(self, *args):
        self.starts += 1

    def update(self, dt):
        pass

    def close(self):
        pass


class Harness:
    def __init__(self):
        self.adapter_lock = threading.RLock()
        self.limits = Limits()
        self.guard = ControlGuard(self.limits, token_factory=lambda: 'token')
        self.guard.lease('acquire', 'test', duration_s=10)
        self.capabilities = {'flight.takeoff'}
        self.asset = {'kind': 'uav'}
        self.closing = self.driving = False
        self.fault = ''
        self.operation = None
        self.stop_pending = None
        self.adapter = FakeAdapter()
        self.waiters = []
        self.last_tick = self.last_state = time.monotonic()
        self.timer = SimpleNamespace(cancel=lambda: None)
        self.context = None

    def get_logger(self):
        return SimpleNamespace(error=lambda *args: None, warning=lambda *args: None)

    def _publish_state(self, snapshot):
        pass


for _name in ('_halt', '_retry_stop', '_expire', '_require_capability', '_require_ready', '_goal',
              '_cancel', '_execute', '_finish_action', '_tick', '_next_tick', '_lease', '_stop', '_acceptance_failed',
              'close'):
    setattr(Harness, _name, getattr(RobotRuntime, _name))
Harness._response = staticmethod(RobotRuntime._response)


def goal_request():
    goal = Flight.Goal()
    goal.lease_token = 'token'
    goal.altitude_m = 1.0
    return goal


class Handle:
    is_active = True
    is_cancel_requested = False

    def __init__(self, request):
        self.request = request
        self.transitions = []

    def __eq__(self, other):
        raise AssertionError('Goal handles must use identity checks, including None')

    def abort(self):
        self.is_active = False
        self.transitions.append('aborted')

    def succeed(self):
        self.is_active = False
        self.transitions.append('succeeded')

    def canceled(self):
        self.is_active = False
        self.transitions.append('canceled')

    def publish_feedback(self, feedback):
        pass


def test_cancel_handle_identity_avoids_server_equality_crash():
    runtime = Harness()
    request = goal_request()
    assert runtime._goal('flight.takeoff', request) == GoalResponse.ACCEPT
    handle = Handle(request)
    runtime.operation['handle'] = handle
    assert runtime._cancel(handle) == CancelResponse.ACCEPT
    assert runtime.operation['interrupted'][0] == 'CANCELED'


def test_failed_stop_prevents_acceptance_and_start():
    runtime = Harness()
    runtime.adapter.fail_stop = True
    assert runtime._goal('flight.takeoff', goal_request()) == GoalResponse.REJECT
    assert runtime.operation is None
    assert runtime.adapter.starts == 0


def test_fault_between_acceptance_and_execution_prevents_start():
    runtime = Harness()
    request = goal_request()
    assert runtime._goal('flight.takeoff', request) == GoalResponse.ACCEPT
    runtime.fault = 'injected fault after acceptance'
    result = asyncio.run(runtime._execute('flight.takeoff', Flight, Handle(request)))
    assert result.code == 'DRIVER_ERROR'
    assert runtime.adapter.starts == 0
    assert runtime.operation is None


def test_disappeared_client_reservation_expires():
    runtime = Harness()
    assert runtime._goal('flight.takeoff', goal_request()) == GoalResponse.ACCEPT
    runtime.operation['start'] -= 3.0
    runtime._tick()
    assert runtime.operation is None
    assert runtime._goal('flight.takeoff', goal_request()) == GoalResponse.ACCEPT


def test_late_old_execution_cannot_clear_new_reservation():
    runtime = Harness()
    old_request, new_request = goal_request(), goal_request()
    assert runtime._goal('flight.takeoff', new_request) == GoalResponse.ACCEPT
    reservation = runtime.operation
    result = asyncio.run(runtime._execute('flight.takeoff', Flight, Handle(old_request)))
    assert not result.success
    assert runtime.operation is reservation
    assert runtime.adapter.starts == 0


def test_stop_failure_retries_without_a_lease_and_does_not_resume_motion(monkeypatch):
    runtime = Harness()
    now = [time.monotonic()]
    monkeypatch.setattr('chaos_runtime.node.time.monotonic', lambda: now[0])
    runtime.driving = True
    runtime.adapter.fail_stop = True
    assert not runtime._halt('COMMAND_EXPIRED', 'expired')
    assert runtime.driving and runtime.stop_pending is not None
    runtime.guard.token = ''
    for _ in range(20):
        runtime._tick()
    assert runtime.adapter.stops == 1  # repeated watchdog checks respect backoff
    now[0] += 0.11
    runtime._tick()
    assert runtime.adapter.stops == 2
    runtime.adapter.fail_stop = False
    now[0] += 0.21
    runtime._tick()
    assert runtime.adapter.stops == 3
    assert runtime.stop_pending is None and not runtime.driving
    assert runtime.fault  # recovering a stop never automatically resumes motion
    assert runtime._goal('flight.takeoff', goal_request()) == GoalResponse.REJECT


def test_release_reports_stop_failure_and_revokes_token():
    runtime = Harness()
    runtime.adapter.fail_stop = True
    request = ControlLease.Request(operation='release', owner='test', token='token')
    response = runtime._lease(request, ControlLease.Response())
    assert not response.success and response.code == 'STOP_FAILED'
    assert response.token == '' and response.remaining_s == 0
    assert runtime.guard.token == '' and runtime.stop_pending is not None


def test_stop_can_be_retried_explicitly_while_fault_is_latched():
    runtime = Harness()
    runtime.capabilities.add('motion.stop')
    request = Stop.Request(lease_token='token')
    runtime.adapter.fail_stop = True
    assert runtime._stop(request, Stop.Response()).code == 'STOP_FAILED'
    runtime.adapter.fail_stop = False
    assert runtime._stop(request, Stop.Response()).success
    assert runtime.stop_pending is None and runtime.fault


def test_action_expired_before_acceptance_is_rejected(monkeypatch):
    runtime = Harness()
    request = goal_request()
    request.expires_at.sec = 100
    request.expires_at.nanosec = 10_000_000
    monkeypatch.setattr('chaos_runtime.control.time.time_ns', lambda: 100_100_000_000)
    assert runtime._goal('flight.takeoff', request) == GoalResponse.REJECT
    assert runtime.adapter.starts == 0 and runtime.operation is None


def test_action_expired_between_acceptance_and_execution_never_starts(monkeypatch):
    runtime = Harness()
    now = [100_000_000_000]
    monkeypatch.setattr('chaos_runtime.control.time.time_ns', lambda: now[0])
    request = goal_request()
    request.expires_at.sec = 100
    request.expires_at.nanosec = 10_000_000
    assert runtime._goal('flight.takeoff', request) == GoalResponse.ACCEPT
    now[0] += 100_000_000
    result = asyncio.run(runtime._execute('flight.takeoff', Flight, Handle(request)))
    assert not result.success and result.code == 'STALE_COMMAND'
    assert runtime.adapter.starts == 0 and runtime.operation is None


def test_service_expiration_is_checked_after_waiting_for_lock(monkeypatch):
    runtime = Harness()
    now = [100_000_000_000]
    monkeypatch.setattr('chaos_runtime.control.time.time_ns', lambda: now[0])

    class DelayedLock:
        def __enter__(self):
            now[0] += 100_000_000

        def __exit__(self, *args):
            pass

    runtime.adapter_lock = DelayedLock()
    request = ControlLease.Request(operation='release', owner='test', token='token')
    request.expires_at.sec = 100
    request.expires_at.nanosec = 10_000_000
    response = runtime._lease(request, ControlLease.Response())
    assert not response.success and response.code == 'STALE_COMMAND'
    assert runtime.guard.token == 'token' and runtime.adapter.stops == 0


def test_acceptance_failure_releases_reservation_without_starting_device():
    runtime = Harness()
    request = goal_request()
    assert runtime._goal('flight.takeoff', request) == GoalResponse.ACCEPT
    runtime._acceptance_failed(Handle(request))
    assert runtime.operation is None and runtime.adapter.starts == 0


def test_shutdown_retries_pending_stop_even_during_backoff():
    runtime = Harness()
    runtime.adapter.fail_stop = True
    runtime._halt('STOPPED', 'stop')
    assert runtime.adapter.stops == 1
    runtime.adapter.fail_stop = False
    runtime.close()
    assert runtime.adapter.stops == 2 and runtime.stop_pending is None
    assert runtime.closing


def test_release_stop_failure_retries_even_without_driving_or_active_operation(monkeypatch):
    runtime = Harness()
    now = [time.monotonic()]
    monkeypatch.setattr('chaos_runtime.node.time.monotonic', lambda: now[0])
    runtime.adapter.fail_stop = True
    runtime._lease(ControlLease.Request(operation='release', owner='test', token='token'),
                   ControlLease.Response())
    assert not runtime.driving and runtime.operation is None and not runtime.guard.token
    runtime.adapter.fail_stop = False
    now[0] += 0.11
    runtime._tick()
    assert runtime.adapter.stops == 2 and runtime.stop_pending is None


@pytest.mark.parametrize('stop_fails', [False, True])
@pytest.mark.parametrize('outcome', ['success', 'cancel', 'failure'])
def test_action_terminal_state_waits_for_stop_outcome(monkeypatch, outcome, stop_fails):
    runtime = Harness()
    request = goal_request()
    assert runtime._goal('flight.takeoff', request) == GoalResponse.ACCEPT
    handle = Handle(request)
    monkeypatch.setattr('chaos_runtime.node.rclpy.ok', lambda **kwargs: True)

    def poll_operation():
        runtime.adapter.fail_stop = stop_fails
        success = outcome == 'success'
        return True, success, 'OK' if success else 'BACKEND_REJECTED', 'done', 1.0

    runtime.adapter.poll_operation = poll_operation
    if outcome == 'cancel':
        runtime.adapter.fail_stop = stop_fails
        assert runtime._cancel(handle) == CancelResponse.ACCEPT
        handle.is_cancel_requested = True

    result = asyncio.run(runtime._execute('flight.takeoff', Flight, handle))
    if stop_fails:
        assert not result.success and result.code == 'STOP_FAILED'
        assert handle.transitions == ['aborted']
        assert runtime.stop_pending is not None and runtime.fault
    else:
        assert result.success == (outcome == 'success')
        expected = {'success': 'succeeded', 'cancel': 'canceled', 'failure': 'aborted'}[outcome]
        assert handle.transitions == [expected]
        assert runtime.stop_pending is None
    assert runtime.operation is None and runtime.adapter.stops >= 2


def test_cancel_waits_for_ros_canceling_transition_before_terminal_state(monkeypatch):
    runtime = Harness()
    request = goal_request()
    assert runtime._goal('flight.takeoff', request) == GoalResponse.ACCEPT
    handle = Handle(request)
    assert runtime._cancel(handle) == CancelResponse.ACCEPT
    monkeypatch.setattr('chaos_runtime.node.rclpy.ok', lambda **kwargs: True)
    waits = []

    async def transition_on_next_tick():
        assert handle.transitions == []
        waits.append(True)
        handle.is_cancel_requested = True

    runtime._next_tick = transition_on_next_tick
    result = asyncio.run(runtime._execute('flight.takeoff', Flight, handle))
    assert waits == [True] and handle.transitions == ['canceled']
    assert not result.success and result.code == 'CANCELED'
    assert runtime.adapter.starts == 0
