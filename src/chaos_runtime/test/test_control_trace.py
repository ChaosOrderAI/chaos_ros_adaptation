"""Training evidence follows control decisions without becoming a control path."""
import asyncio
import threading
from types import SimpleNamespace

import pytest

pytest.importorskip('rclpy')
pytest.importorskip('chaos_interfaces.msg')

from builtin_interfaces.msg import Time
from chaos_interfaces.action import Flight
from chaos_interfaces.msg import VelocityCommand
from chaos_interfaces.srv import ControlLease, Stop
from chaos_runtime.adapters.base import Snapshot
from chaos_runtime.control import CommandDeadline, ControlGuard, Limits
from chaos_runtime.node import RobotRuntime


class Harness:
    def __init__(self):
        self.now = 10.0
        self.ros_ns = 100_000_000_000
        self.asset = {'id': 'test_robot', 'frames': {'base': 'test_base'}}
        self.limits = Limits()
        self.guard = ControlGuard(self.limits, clock=lambda: self.now,
                                  token_factory=lambda: 'private-token')
        self.guard.lease('acquire', 'private-owner', duration_s=10.0)
        self.adapter_lock = threading.RLock()
        self.capabilities = {'motion.velocity', 'motion.stop'}
        self.closing = self.driving = False
        self.operation = self.stop_pending = None
        self.fault = ''
        self.trace_sequence = 0
        self.traces, self.results, self.events, self.logs = [], [], [], []
        self.fail_publish = self.fail_driver = self.fail_stop = False
        self.driver_delay = 0.0
        self.trace_pub = SimpleNamespace(publish=self.publish_trace)
        self.results_pub = SimpleNamespace(publish=self.results.append)
        self.adapter = SimpleNamespace(
            snapshot=lambda: Snapshot(connected=True, ready=True, pose_valid=True),
            validate_velocity=lambda values: None,
            set_velocity=self.set_velocity, stop=self.stop_driver,
            controlled_stop=self.stop_driver,
        )

    def get_logger(self):
        return SimpleNamespace(warning=self.logs.append, error=self.logs.append)

    def get_clock(self):
        def now():
            sec, nanosec = divmod(self.ros_ns, 1_000_000_000)
            return SimpleNamespace(nanoseconds=self.ros_ns,
                                   to_msg=lambda: Time(sec=sec, nanosec=nanosec))
        return SimpleNamespace(now=now)

    def publish_trace(self, trace):
        if self.fail_publish:
            raise RuntimeError('private-publication-error')
        self.events.append(('trace', trace.stage))
        self.traces.append(trace)

    def set_velocity(self, values):
        self.events.append(('driver_velocity', list(values)))
        self.now += self.driver_delay
        self.ros_ns += round(self.driver_delay * 1_000_000_000)
        if self.fail_driver:
            raise RuntimeError('private-driver-error')

    def stop_driver(self):
        self.events.append(('driver_stop', None))
        if self.fail_stop:
            raise RuntimeError('private-stop-error')


for _name in ('_velocity', '_result', '_halt', '_retry_stop', '_expire',
              '_require_capability', '_require_ready', '_stop', '_lease',
              '_execute', '_finish_action'):
    setattr(Harness, _name, getattr(RobotRuntime, _name))
Harness._response = staticmethod(RobotRuntime._response)


def velocity(runtime, *, age=0.0, ttl=0.4, request_id='request-1', x=0.2):
    request = VelocityCommand()
    ns = runtime.ros_ns - round(age * 1_000_000_000)
    request.header.stamp = Time(sec=ns // 1_000_000_000, nanosec=ns % 1_000_000_000)
    request.header.frame_id = 'test_base'
    request.request_id, request.lease_token = request_id, 'private-token'
    request.valid_for_s = ttl
    request.velocity.linear.x, request.velocity.angular.z = x, 0.3
    return request


def test_issued_trace_uses_driver_values_and_remaining_lease_not_requested_ttl():
    runtime = Harness()
    runtime.guard.expires = runtime.now + 0.15
    runtime._velocity(velocity(runtime, age=0.1))
    trace, = runtime.traces
    assert runtime.events[0][0] == 'driver_velocity'
    assert trace.command == 'motion.velocity' and trace.stage == 'issued'
    assert trace.velocity_valid and trace.valid_for_s == pytest.approx(0.15)
    assert trace.velocity.linear.x == 0.2 and trace.velocity.angular.z == 0.3
    assert trace.header.frame_id == 'test_base' and trace.header.stamp.sec == 100
    assert trace.asset_id == 'test_robot' and trace.request_id == 'request-1'
    assert trace.sequence == 1 and trace.monotonic_ns > 0
    assert runtime.results[-1].success
    assert 'private-token' not in repr(trace) and 'private-owner' not in repr(trace)


def test_adapter_dispatch_time_consumes_action_label_validity():
    runtime = Harness()
    runtime.driver_delay = 0.12
    runtime._velocity(velocity(runtime, age=0.1))
    trace, = runtime.traces
    assert trace.valid_for_s == pytest.approx(0.18)
    assert trace.header.stamp.nanosec == 120_000_000


def test_rejected_new_velocity_does_not_invalidate_previous_accepted_command():
    runtime = Harness()
    runtime._velocity(velocity(runtime))
    deadline = runtime.guard.velocity_expires
    runtime._velocity(velocity(runtime, x=50.0, request_id='rejected-request'))
    assert [item.stage for item in runtime.traces] == ['issued', 'rejected']
    trace = runtime.traces[-1]
    assert trace.code == 'OUT_OF_RANGE' and not trace.velocity_valid
    assert trace.valid_for_s == 0.0 and trace.velocity.linear.x == 0.0
    assert runtime.driving and runtime.guard.velocity_expires == deadline
    assert not runtime.results[-1].success


def test_stop_invalidates_before_driver_output_and_never_creates_zero_velocity_label():
    runtime = Harness()
    runtime._velocity(velocity(runtime))
    runtime.events.clear()
    result = runtime._stop(Stop.Request(lease_token='private-token', request_id='stop-1'),
                           Stop.Response())
    assert result.success
    assert runtime.events == [('trace', 'invalidated'), ('driver_stop', None), ('trace', 'issued')]
    invalidated, stopped = runtime.traces[-2:]
    assert invalidated.code == 'STOPPED' and stopped.code == 'STOP_OUTPUT_ISSUED'
    assert invalidated.request_id == stopped.request_id == 'stop-1'
    assert all(not row.velocity_valid and row.valid_for_s == 0.0 for row in (invalidated, stopped))
    assert not runtime.driving and runtime.guard.velocity_expires == 0.0


def test_failed_stop_invalidates_immediately_and_retry_does_not_claim_physical_confirmation():
    runtime = Harness()
    runtime._velocity(velocity(runtime))
    runtime.fail_stop = True
    assert not runtime._halt('COMMAND_EXPIRED', 'not included in trace')
    assert runtime.traces[-1].stage == 'invalidated'
    assert runtime.traces[-1].code == 'COMMAND_EXPIRED'
    assert runtime.stop_pending is not None
    runtime.fail_stop = False
    assert runtime._retry_stop(force=True)
    assert runtime.traces[-1].stage == 'issued'
    assert runtime.traces[-1].command == 'motion.stop'
    assert not runtime.traces[-1].velocity_valid and runtime.fault


@pytest.mark.parametrize('reason', ['release', 'expired'])
def test_lease_loss_invalidates_action_even_when_stop_output_fails(reason):
    runtime = Harness()
    runtime._velocity(velocity(runtime))
    runtime.fail_stop = True
    if reason == 'release':
        response = runtime._lease(ControlLease.Request(operation='release', owner='private-owner',
                                                      token='private-token'), ControlLease.Response())
        assert response.code == 'STOP_FAILED'
    else:
        runtime.now = runtime.guard.expires + 0.01
        runtime._expire()
    assert runtime.traces[-1].stage == 'invalidated'
    assert runtime.traces[-1].code == ('CONTROL_RELEASED' if reason == 'release' else 'LEASE_EXPIRED')
    assert not runtime.guard.token


def test_publication_failure_leaves_sequence_gap_without_altering_control_outcome():
    runtime = Harness()
    runtime.fail_publish = True
    runtime._velocity(velocity(runtime))
    assert runtime.results[-1].success and runtime.driving and not runtime.fault
    assert runtime.trace_sequence == 1 and not runtime.traces
    runtime.fail_publish = False
    runtime._velocity(velocity(runtime, request_id='next'))
    assert runtime.traces[-1].sequence == 2
    assert 'private-publication-error' not in str(runtime.logs)


def test_driver_failure_has_invalidation_and_rejection_without_error_or_authorization_data():
    runtime = Harness()
    runtime.fail_driver = True
    runtime._velocity(velocity(runtime))
    assert [row.stage for row in runtime.traces] == ['invalidated', 'issued', 'rejected']
    assert not any(row.velocity_valid for row in runtime.traces)
    assert runtime.traces[-1].code == 'DRIVER_ERROR'
    assert not runtime.results[-1].success
    serialized = repr(runtime.traces)
    assert all(secret not in serialized for secret in ('private-token', 'private-owner', 'private-driver-error'))


def test_free_form_request_identifier_is_not_copied_to_dataset_evidence():
    runtime = Harness()
    runtime._velocity(velocity(runtime, request_id='free form\nprivate text'))
    assert runtime.results[-1].success
    assert runtime.traces[-1].request_id == ''


def test_action_dispatch_invalidates_velocity_without_fabricating_continuous_labels(monkeypatch):
    runtime = Harness()
    runtime.capabilities.add('flight.takeoff')
    request = Flight.Goal(lease_token='private-token', altitude_m=1.0)
    runtime.operation = {
        'kind': 'flight.takeoff', 'request': request, 'target': 1.0,
        'interrupted': None, 'deadline': CommandDeadline.capture(request.expires_at),
    }
    transitions = []
    handle = SimpleNamespace(request=request, is_active=True, abort=lambda: transitions.append('aborted'))
    runtime.adapter.start_operation = lambda *args: runtime.events.append(('driver_action', args))
    runtime.context = None
    monkeypatch.setattr('chaos_runtime.node.rclpy.ok', lambda **kwargs: False)
    result = asyncio.run(runtime._execute('flight.takeoff', Flight, handle))
    assert result.code == 'SHUTDOWN'
    assert runtime.events[:2] == [('trace', 'invalidated'), ('driver_action', ('flight.takeoff', 1.0))]
    assert runtime.traces[0].command == 'flight.takeoff'
    assert runtime.traces[0].code == 'OPERATION_STARTING'
    assert not any(row.velocity_valid for row in runtime.traces)
    assert transitions == ['aborted']
