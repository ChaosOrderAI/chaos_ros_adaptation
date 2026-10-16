"""SDK contract tests without starting ROS nodes or hardware transports."""
from types import SimpleNamespace

import pytest

pytest.importorskip('rclpy')
pytest.importorskip('chaos_interfaces.srv')

from builtin_interfaces.msg import Time
from geometry_msgs.msg import PoseStamped
from rclpy.task import Future
from chaos_interfaces.srv import ControlLease, GetDescription
from chaos_sdk import Robot


class Endpoint:
    def __init__(self, *args):
        self.sent = []
        self.future = None

    def publish(self, message):
        self.sent.append(message)

    def call_async(self, message):
        self.sent.append(message)
        self.future = Future()
        return self.future

    def send_goal_async(self, goal, **kwargs):
        return self.call_async(goal)


class FakeNode:
    def create_client(self, *args):
        return Endpoint()

    def create_publisher(self, *args):
        return Endpoint()

    def create_subscription(self, *args):
        return Endpoint()

    def get_clock(self):
        return SimpleNamespace(now=lambda: SimpleNamespace(to_msg=lambda: Time(sec=123)))


@pytest.fixture
def robot(monkeypatch):
    monkeypatch.setattr('chaos_sdk.client.ActionClient', Endpoint)
    return Robot(FakeNode(), 'test')


def test_acquire_token_available_when_returned_future_finishes(robot):
    future = robot.acquire('operator', duration_s=5)
    response = ControlLease.Response(success=True, token='token', remaining_s=5.0)
    robot._lease.future.set_result(response)
    assert future.done()
    assert future.result().success
    assert robot.lease_token == 'token'
    assert robot.owner == 'operator'
    release = robot.release()
    robot._lease.future.set_result(ControlLease.Response(success=True))
    assert release.done()
    assert robot.lease_token == '' and robot.owner == ''


def test_failed_acquire_does_not_install_token(robot):
    future = robot.acquire('operator')
    robot._lease.future.set_result(ControlLease.Response(success=False, code='BUSY'))
    assert not future.result().success
    assert robot.lease_token == ''


def test_release_stop_failure_clears_revoked_token(robot):
    robot.lease_token, robot.owner = 'token', 'operator'
    future = robot.release()
    robot._lease.future.set_result(ControlLease.Response(success=False, code='STOP_FAILED', token=''))
    assert not future.result().success
    assert robot.lease_token == '' and robot.owner == ''


def test_rejected_release_preserves_live_token(robot):
    robot.lease_token, robot.owner = 'token', 'operator'
    future = robot.release()
    robot._lease.future.set_result(ControlLease.Response(success=False, code='STALE_COMMAND'))
    assert not future.result().success
    assert robot.lease_token == 'token' and robot.owner == 'operator'


def test_describe_resolves_velocity_frame_and_limit(robot):
    future = robot.describe()
    robot._description.future.set_result(
        GetDescription.Response(base_frame='test/base', command_timeout_s=0.2))
    assert future.done()
    assert robot.base_frame == 'test/base'
    assert robot.command_timeout_s == 0.2


def test_sdk_casts_integer_velocity_and_altitude_to_ros_float_fields(robot):
    robot.lease_token = 'token'
    robot.base_frame = 'test/base'
    request_id = robot.velocity(x=1, yaw=1, valid_for_s=1)
    message = robot._velocity.sent[-1]
    assert message.request_id == request_id
    assert type(message.velocity.linear.x) is float
    assert type(message.velocity.angular.z) is float
    assert type(message.valid_for_s) is float
    robot.takeoff(2)
    assert robot._takeoff.sent[-1].altitude_m == 2.0
    robot.land()
    assert robot._land.sent[-1].altitude_m == 0.0


def test_sdk_requires_explicit_control_and_known_frame(robot):
    with pytest.raises(ValueError, match='Acquire control'):
        robot.velocity(x=1)
    robot.lease_token = 'token'
    with pytest.raises(ValueError, match='describe'):
        robot.velocity(x=1)


def test_service_and_action_deadlines_use_unix_clock(robot, monkeypatch):
    monkeypatch.setattr('chaos_sdk.client.time.time_ns', lambda: 1_700_000_000_123_000_000)
    robot.acquire('operator')
    default = robot._lease.sent[-1].expires_at
    assert (default.sec, default.nanosec) == (1_700_000_005, 123_000_000)
    robot.lease_token = 'token'
    robot.owner = 'operator'
    calls = [
        (lambda: robot.renew(ttl_s=0.2), robot._lease),
        (lambda: robot.release(ttl_s=0.2), robot._lease),
        (lambda: robot.stop(ttl_s=0.2), robot._stop),
        (lambda: robot.navigate(PoseStamped(), ttl_s=0.2), robot._navigation),
        (lambda: robot.takeoff(1, ttl_s=0.2), robot._takeoff),
        (lambda: robot.land(ttl_s=0.2), robot._land),
    ]
    for call, endpoint in calls:
        call()
        stamp = endpoint.sent[-1].expires_at
        assert (stamp.sec, stamp.nanosec) == (1_700_000_000, 323_000_000)


@pytest.mark.parametrize('ttl', [0, -1, float('nan'), float('inf'), True, '5', 31])
def test_invalid_deadline_never_sends_request(robot, ttl):
    with pytest.raises(ValueError, match='TTL'):
        robot.acquire('operator', ttl_s=ttl)
    assert not robot._lease.sent


def test_constructor_sets_default_deadline_before_creating_endpoints(monkeypatch):
    monkeypatch.setattr('chaos_sdk.client.ActionClient', Endpoint)
    monkeypatch.setattr('chaos_sdk.client.time.time_ns', lambda: 1_700_000_000_000_000_000)
    client = Robot(FakeNode(), 'test', command_ttl_s=0.25)
    client.acquire('operator')
    assert client._lease.sent[-1].expires_at.nanosec == 250_000_000
    with pytest.raises(ValueError, match='TTL'):
        Robot(object(), 'test', command_ttl_s=0)
