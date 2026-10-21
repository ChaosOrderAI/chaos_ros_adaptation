"""Go2 regression tests use no robot, DDS participant, vendor SDK, or ROS graph."""

import math
import json
import threading
from types import SimpleNamespace as NS

import pytest

from chaos_runtime.go2_driver_core import Go2DriverCore, ZERO, fill_odometry


def asset(**options):
    return {
        'id': 'go2_01',
        'frames': {'base': 'go2_01/base_link', 'odom': 'go2_01/odom'},
        'driver': {'plugin': 'unitree_go2', 'options': options},
        'limits': {'linear_mps': 0.5, 'angular_rps': 0.8,
                   'command_timeout_s': 0.5, 'state_timeout_s': 1.0},
    }


class Clock:
    def __init__(self):
        self.mono = 10.0
        self.ros = 100_000_000_000

    def advance(self, seconds):
        self.mono += seconds
        self.ros += round(seconds * 1_000_000_000)


def state(tick=None, stamp=1, **changes):
    values = dict(stamp=NS(sec=stamp, nanosec=0) if isinstance(stamp, int) else stamp, error_code=0,
                  position=[1.0, 2.0, 0.3], velocity=[0.1, 0.2, 0.0], yaw_speed=0.3,
                  imu_state=NS(quaternion=[1.0, 0.0, 0.0, 0.0], gyroscope=[0.01, 0.02, 0.3]))
    if tick is not None:
        values['tick'] = tick
    values.update(changes)
    return NS(**values)


def command(clock, values=(0.2, 0, 0, 0, 0, 0.1), *, stamp=None, frame='go2_01/base_link'):
    ns = clock.ros if stamp is None else stamp
    sec, nanosec = divmod(ns, 1_000_000_000)
    return NS(header=NS(stamp=NS(sec=sec, nanosec=nanosec), frame_id=frame),
              twist=NS(linear=NS(x=values[0], y=values[1], z=values[2]),
                       angular=NS(x=values[3], y=values[4], z=values[5])))


class Client:
    """Anything except the bounded no-reply Move path is a test failure."""
    def __init__(self):
        self.calls = []
        self.failures = []

    def Move(self, *values):
        self.calls.append(values)
        if self.failures:
            result = self.failures.pop(0)
            if isinstance(result, Exception):
                raise result
            return result
        return 0

    def __getattr__(self, name):
        raise AssertionError(f'Blocking or posture-changing API must not be used: {name}')


def harness(**options):
    clock, client = Clock(), Client()
    core = Go2DriverCore(asset(**options), clock=lambda: clock.mono, ros_clock=lambda: clock.ros)
    return core, clock, client


def ready(**options):
    core, clock, client = harness(**options)
    assert core.accept_state(state())
    assert core.step(client.Move) is not None
    assert client.calls == [ZERO]
    return core, clock, client


def test_initial_unknown_state_and_failed_initial_zero_inhibit_motion():
    core, clock, client = harness()
    assert not core.accept_command(command(clock))
    assert core.step(client.Move) is None
    assert client.calls == []
    assert core.accept_state(state())
    core.request_stop()
    client.failures = [1]
    assert core.step(client.Move) is None
    assert core.output_error and core.stop_pending and not core.fault
    assert not core.accept_command(command(clock))
    assert core.step(client.Move) is not None
    assert core.initialized and not core.stop_pending
    assert core.accept_command(command(clock))


def test_initial_sdk_discovery_retries_never_publish_ready_before_zero_succeeds():
    core, clock, client = harness()
    client.failures = [1, 1]
    core.accept_state(state())
    assert core.step(client.Move) is None
    assert core.step(client.Move) is None
    assert not core.initialized and core.stop_pending
    assert core.step(client.Move) is not None
    assert core.initialized and not core.fault
    assert client.calls == [ZERO, ZERO, ZERO]


def test_command_watchdog_works_when_runtime_stops_publishing():
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    assert client.calls[-1] == (0.2, 0.0, 0.1)
    clock.advance(0.5)
    assert core.accept_state(state(stamp=2))
    core.step(client.Move)
    assert client.calls[-1] == ZERO
    assert core.command is None and not core.moving
    clock.advance(0.1)
    core.accept_state(state(stamp=3))
    core.step(client.Move)
    assert client.calls[-1] == ZERO  # new telemetry cannot revive an old command


def test_source_age_consumes_deadline_and_duplicate_command_cannot_extend_it():
    core, clock, client = ready()
    message = command(clock, stamp=clock.ros - 400_000_000)
    assert core.accept_command(message)
    core.step(client.Move)
    clock.advance(0.05)
    assert not core.accept_command(message)
    clock.advance(0.051)
    core.step(client.Move)
    assert client.calls[-1] == ZERO


def test_telemetry_loss_stops_even_with_fresh_incoming_command():
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    clock.advance(0.9)
    assert core.accept_command(command(clock))
    core.step(client.Move)
    clock.advance(0.101)
    assert core.step(client.Move) is None
    assert client.calls[-1] == ZERO
    assert not core.accept_command(command(clock))


def test_ros_clock_pause_does_not_pause_watchdog():
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    clock.mono += 0.501
    core.step(client.Move)
    assert client.calls[-1] == ZERO


def test_replayed_command_with_frozen_ros_clock_cannot_refresh_or_restart_motion():
    core, clock, client = ready()
    message = command(clock)
    assert core.accept_command(message)
    core.step(client.Move)
    for index in range(1, 8):
        clock.mono += 0.1
        assert core.accept_state(state(stamp=index + 1))
        assert not core.accept_command(message)
        core.step(client.Move)
        if index >= 6:
            assert client.calls[-1] == ZERO and core.command is None
    assert core.accept_command(command(clock, (0,) * 6))
    core.step(client.Move)
    assert client.calls[-1] == ZERO


def test_stop_does_not_allow_same_command_stamp_to_restart_motion():
    core, clock, client = ready()
    message = command(clock)
    assert core.accept_command(message)
    core.step(client.Move)
    assert core.accept_command(command(clock, (0,) * 6))
    core.step(client.Move)
    assert not core.accept_command(message)
    core.step(client.Move)
    assert client.calls[-1] == ZERO


def test_stop_stamp_blocks_delayed_pre_stop_motion_and_old_zero_cannot_lower_barrier():
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    clock.advance(0.1)
    delayed = command(clock)
    old_zero = command(clock, (0,) * 6)
    clock.advance(0.1)
    assert core.accept_command(command(clock, (0,) * 6))
    barrier = clock.ros
    core.step(client.Move)
    assert core.accept_command(old_zero)
    core.step(client.Move)
    assert core.last_command_stamp_ns == barrier
    assert not core.accept_command(delayed)
    core.step(client.Move)
    assert client.calls[-1] == ZERO
    clock.advance(0.1)
    assert core.accept_command(command(clock))
    core.step(client.Move)
    assert client.calls[-1] != ZERO


@pytest.mark.parametrize('correction', [-1_000_000_000, 1_000_000_000])
def test_ros_clock_correction_cannot_extend_active_command(correction):
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    clock.ros += correction
    core.step(client.Move)
    assert client.calls[-1] == ZERO


@pytest.mark.parametrize('values', [
    (0, 0, 0.01, 0, 0, 0), (0, 0, 0, 0.01, 0, 0), (0, 0, 0, 0, 0.01, 0),
    (0.4, 0.4, 0, 0, 0, 0), (0, 0, 0, 0, 0, 0.81),
    (math.nan, 0, 0, 0, 0, 0), (0, 0, 0, 0, 0, math.inf),
])
def test_unsupported_axes_nonfinite_and_norm_limits_reject_and_stop(values):
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    assert not core.accept_command(command(clock, values))
    core.step(client.Move)
    assert client.calls[-1] == ZERO


@pytest.mark.parametrize('kwargs', [
    {'frame': 'map'}, {'frame': ''}, {'stamp': 0}, {'stamp': 99_500_000_000},
    {'stamp': 100_000_000_001},
])
def test_wrong_frame_or_missing_stale_future_stamp_never_moves(kwargs):
    core, clock, client = ready()
    assert not core.accept_command(command(clock, **kwargs))
    core.step(client.Move)
    assert all(values == ZERO for values in client.calls)


def test_duplicate_and_backward_sport_stamps_do_not_refresh_state():
    core, clock, client = ready()
    received = core.sample.received
    for index in range(5):
        clock.advance(0.25)
        assert not core.accept_state(state(stamp=1 if index % 2 else 0))
        assert core.sample.received == received
        assert core.step(client.Move) is None
    assert not core.accept_command(command(clock))
    assert core.accept_state(state(stamp=2))


def test_uint32_tick_wrap_accepted_but_duplicates_and_old_ticks_ignored():
    core, clock, client = harness()
    assert core.accept_state(state(tick=2**32 - 2))
    first = core.sample
    clock.advance(0.01)
    assert not core.accept_state(state(tick=2**32 - 2))
    assert core.sample is first
    assert core.accept_state(state(tick=1))
    assert not core.accept_state(state(tick=2**32 - 1))
    assert core.source_identity == ('tick', 1)


@pytest.mark.parametrize('changes', [
    {'position': [math.nan, 0, 0]}, {'velocity': [0, math.inf, 0]},
    {'imu_state': NS(quaternion=[0, 0, 0, 0], gyroscope=[0, 0, 0])},
    {'imu_state': NS(quaternion=[1.1, 0, 0, 0], gyroscope=[0, 0, 0])},
    {'imu_state': NS(quaternion=[1, 0, 0, 0], gyroscope=[0, math.nan, 0])},
    {'yaw_speed': math.nan}, {'error_code': 123},
    {'stamp': NS(sec=1, nanosec=1_000_000_000)},
])
def test_bad_telemetry_invalidates_motion_and_does_not_publish(changes):
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    assert not core.accept_state(state(**{'stamp': 2, **changes}))
    assert core.step(client.Move) is None
    assert client.calls[-1] == ZERO
    assert not core.accept_command(command(clock))


@pytest.mark.parametrize('failure', [1, None, False, RuntimeError('injected send failure')])
def test_write_failure_latches_fault_and_retries_zero_without_resuming(failure):
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    client.failures = [failure, 1]
    assert core.step(client.Move) is None
    assert client.calls[-2:] == [(0.2, 0.0, 0.1), ZERO]
    assert core.stop_pending and core.fault
    client.failures = [1]
    core.step(client.Move)
    assert core.stop_pending
    core.step(client.Move)
    assert not core.stop_pending
    assert core.fault and not core.accept_command(command(clock))
    assert core.accept_state(state(stamp=2))
    assert core.step(client.Move) is None  # telemetry must not advertise ready


def test_zero_command_is_allowed_after_fault_and_uses_only_move():
    core, clock, client = ready()
    core.fault = 'latched output fault'
    assert core.accept_command(command(clock, (0,) * 6))
    core.step(client.Move)
    assert client.calls[-1] == ZERO and core.fault


def test_expiry_during_sdk_write_sends_zero_in_the_same_iteration():
    core, clock, client = ready()
    assert core.accept_command(command(clock))

    def delayed_move(*values):
        result = client.Move(*values)
        if values != ZERO:
            clock.advance(0.501)
        return result

    core.step(delayed_move)
    assert client.calls[-2:] == [(0.2, 0.0, 0.1), ZERO]


def test_sdk_io_does_not_hold_callback_cache_lock():
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    callback_completed = threading.Event()

    def move_with_callback(*values):
        worker = threading.Thread(target=lambda: (
            core.accept_state(state(stamp=2, error_code=1)), callback_completed.set()))
        worker.start()
        worker.join(timeout=1)
        assert callback_completed.is_set(), 'SDK I/O held the DDS callback lock'
        return client.Move(*values)

    core.step(move_with_callback)
    assert client.calls[-1] == ZERO


def test_world_velocity_rotates_into_child_frame_and_keeps_full_imu_angular_velocity():
    core, clock, client = harness()
    half = math.sqrt(0.5)
    assert core.accept_state(state(velocity=[1.0, 0.0, 0.0],
                                   imu_state=NS(quaternion=[half, 0, 0, half],
                                                gyroscope=[0.1, 0.2, 0.3])))
    sample = core.step(client.Move)
    assert sample.quaternion == pytest.approx((0, 0, half, half))
    assert sample.linear == pytest.approx((0, -1, 0))
    assert sample.angular == pytest.approx((0.1, 0.2, 0.3))


def test_rotation_includes_roll_and_body_velocity_option_is_not_rotated():
    half = math.sqrt(0.5)
    message = state(velocity=[0, 1, 0],
                    imu_state=NS(quaternion=[half, half, 0, 0], gyroscope=[0, 0, 0]))
    core, clock, client = harness()
    core.accept_state(message)
    assert core.step(client.Move).linear == pytest.approx((0, 0, -1))
    core, clock, client = harness(velocity_frame='body')
    core.accept_state(message)
    assert core.step(client.Move).linear == (0, 1, 0)


def test_timer_never_republishes_or_restamps_cached_telemetry():
    core, clock, client = harness()
    clock.ros += 123
    received_stamp = clock.ros
    core.accept_state(state())
    clock.advance(0.25)
    sample = core.step(client.Move)
    vector = lambda: NS(x=0.0, y=0.0, z=0.0)
    odom = NS(header=NS(stamp=NS(sec=0, nanosec=0), frame_id=''), child_frame_id='',
              pose=NS(pose=NS(position=vector(), orientation=NS(x=0, y=0, z=0, w=0))),
              twist=NS(twist=NS(linear=vector(), angular=vector())))
    fill_odometry(odom, sample, odom_frame=core.odom_frame, base_frame=core.base_frame)
    assert odom.header.stamp.sec * 1_000_000_000 + odom.header.stamp.nanosec == received_stamp
    assert odom.header.frame_id == 'go2_01/odom'
    assert odom.child_frame_id == 'go2_01/base_link'
    for _ in range(5):
        clock.advance(0.01)
        assert core.step(client.Move) is None


def test_shutdown_retries_failed_zero_and_never_accepts_motion_again():
    core, clock, client = ready()
    assert core.accept_command(command(clock))
    core.step(client.Move)
    core.request_stop(closing=True)
    client.failures = [1]
    core.step(client.Move)
    assert core.stop_pending
    core.step(client.Move)
    assert not core.stop_pending and client.calls[-1] == ZERO
    assert not core.accept_command(command(clock))
    assert not core.accept_state(state(stamp=2))


@pytest.fixture
def driver_factory(monkeypatch):
    """Exercise the real ROS wrapper, replacing node/SDK I/O before construction."""
    pytest.importorskip('rclpy')
    from chaos_runtime import go2_driver
    from rclpy.clock import ClockType

    config = asset(network_interface='eno1', dds_domain_id=0,
                   sport_state_topic='rt/sportmodestate', velocity_frame='odom',
                   cmd_vel_topic='/go2/cmd_vel', odom_topic='/go2/odom')
    config.update(mode='real', kind='quadruped', ros_domain_id=87)
    clock, client, published, events = Clock(), Client(), [], []
    client.SetTimeout = lambda timeout: events.append(('timeout', timeout))
    client.Init = lambda: events.append(('init',))

    class Subscriber:
        def __init__(self, topic, message_type):
            events.append(('subscriber', topic))

        def Init(self, callback, depth):
            self.callback = callback
            events.append(('reader', depth))

        def Close(self):
            core = self.callback.__self__
            assert core.lock.acquire(blocking=False), 'Close must not join under the callback lock'
            core.lock.release()
            events.append(('close',))

    def fake_node_init(self, *args, **kwargs):
        self._context = NS(get_domain_id=lambda: 87)

    def timer(self, period, callback, *, clock):
        assert clock.clock_type == ClockType.STEADY_TIME
        events.append(('timer', period))
        return NS(cancel=lambda: events.append(('cancel',)))

    def sdk():
        events.append(('sdk',))
        return (lambda domain, iface: events.append(('initialize', domain, iface)),
                Subscriber, lambda: client, object)

    monkeypatch.setattr(go2_driver.Node, '__init__', fake_node_init)
    monkeypatch.setattr(go2_driver.Go2Driver, 'declare_parameter', lambda *args: None)
    monkeypatch.setattr(go2_driver.Go2Driver, 'get_parameter',
                        lambda *args: NS(value=json.dumps(config)))
    monkeypatch.setattr(go2_driver.Go2Driver, 'get_clock',
                        lambda *args: NS(now=lambda: NS(nanoseconds=clock.ros)))
    monkeypatch.setattr(go2_driver.Go2Driver, 'get_logger', lambda *args: NS(
        info=lambda *args, **kwargs: None, warning=lambda *args, **kwargs: None,
        error=lambda *args, **kwargs: None))
    monkeypatch.setattr(go2_driver.Go2Driver, 'create_publisher',
                        lambda *args: NS(publish=published.append))
    monkeypatch.setattr(go2_driver.Go2Driver, 'create_subscription', lambda *args: None)
    monkeypatch.setattr(go2_driver.Go2Driver, 'create_timer', timer)
    monkeypatch.setattr(go2_driver, 'load_sdk', sdk)
    return NS(create=go2_driver.Go2Driver, config=config, clock=clock, client=client,
              published=published, events=events)


def test_ros_wrapper_uses_bounded_no_reply_output_and_original_sample_stamp(driver_factory):
    fake = driver_factory
    node = fake.create()
    assert ('timeout', 0.05) in fake.events
    assert ('initialize', 0, 'eno1') in fake.events
    assert ('reader', 1) in fake.events and ('timer', 0.05) in fake.events
    node._tick()
    assert fake.client.calls == [] and fake.published == []
    node.subscriber.callback(state())
    stamp = fake.clock.ros
    fake.clock.advance(0.1)
    node._tick()
    assert fake.client.calls == [ZERO]
    assert len(fake.published) == 1
    assert fake.published[0].header.stamp.sec * 1_000_000_000 + fake.published[0].header.stamp.nanosec == stamp
    node._tick()
    assert len(fake.published) == 1
    node.close()
    assert fake.events[-1] == ('close',)


@pytest.mark.parametrize('change', [
    {'mode': 'mock'}, {'ros_domain_id': 88}, {'kind': 'uav'},
    {'driver': {'plugin': 'mock', 'options': {}}},
])
def test_ros_wrapper_rejects_bad_asset_before_loading_sdk(driver_factory, change):
    fake = driver_factory
    fake.config.update(change)
    with pytest.raises(ValueError):
        fake.create()
    assert ('sdk',) not in fake.events


def test_ros_wrapper_shutdown_uses_three_zero_attempts_and_closes_outside_lock(driver_factory):
    fake = driver_factory
    node = fake.create()
    fake.client.failures = [1, 1, 1]
    node.close()
    assert fake.client.calls == [ZERO] * 3
    assert fake.events[-2:] == [('cancel',), ('close',)]
    node.close()
    assert fake.client.calls == [ZERO] * 3
