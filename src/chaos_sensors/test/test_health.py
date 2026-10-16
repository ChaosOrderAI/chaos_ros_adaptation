import pytest

from chaos_sensors.health import HealthTracker


class Clocks:
    monotonic_s = 100.0
    ros_ns = 1_700_000_000_000_000_000

    def tracker(self, timeout=2.0):
        return HealthTracker('camera_frame', timeout,
                             lambda: self.ros_ns, lambda: self.monotonic_s)

    def observe(self, tracker, offset_ns=0, frame='camera_frame'):
        sec, nanosec = divmod(self.ros_ns + offset_ns, 1_000_000_000)
        tracker.observe(sec=sec, nanosec=nanosec, frame_id=frame)


def test_no_messages_is_stale_and_first_fresh_message_recovers():
    clocks = Clocks()
    tracker = clocks.tracker()
    assert tracker.snapshot().level == 3
    assert tracker.snapshot().values['receipt'] == 'no data'
    clocks.observe(tracker)
    assert tracker.snapshot().level == 0
    assert tracker.snapshot().values['received_count'] == '1'


def test_receipt_timeout_uses_monotonic_clock_even_when_ros_clock_is_paused():
    clocks = Clocks()
    tracker = clocks.tracker()
    clocks.observe(tracker)
    clocks.monotonic_s += 2.1
    health = tracker.snapshot()
    assert health.level == 3
    assert health.values['receipt'] == 'timeout'
    assert health.values['source'] == 'ok'


def test_recent_receipt_does_not_hide_old_measurement():
    clocks = Clocks()
    tracker = clocks.tracker()
    clocks.observe(tracker, -3_000_000_000)
    health = tracker.snapshot()
    assert health.level == 2
    assert health.values['receipt'] == 'ok'
    assert health.values['source'] == 'source timestamp expired'


@pytest.mark.parametrize('offset, expected', [
    (100_000_000, 0), (100_000_001, 2), (-2_000_000_000, 0), (-2_000_000_001, 2),
])
def test_source_age_boundaries(offset, expected):
    clocks = Clocks()
    tracker = clocks.tracker()
    clocks.observe(tracker, offset)
    assert tracker.snapshot().level == expected


@pytest.mark.parametrize('sec,nanosec,problem', [
    (0, 0, 'zero source timestamp'), (-1, 0, 'invalid source timestamp'),
    (10, 1_000_000_000, 'invalid source timestamp'),
])
def test_zero_and_invalid_timestamps(sec, nanosec, problem):
    clocks = Clocks()
    tracker = clocks.tracker()
    tracker.observe(sec=sec, nanosec=nanosec, frame_id='camera_frame')
    assert tracker.snapshot().level == 2
    assert tracker.snapshot().values['source'] == problem
    assert tracker.snapshot().values['invalid_stamp_count'] == '1'


def test_repeated_stamp_is_bad_before_age_timeout_and_recovers_on_new_sample():
    clocks = Clocks()
    tracker = clocks.tracker()
    clocks.observe(tracker)
    clocks.observe(tracker)
    assert tracker.snapshot().values['source'] == 'source timestamp did not advance'
    assert tracker.snapshot().level == 2
    clocks.ros_ns += 100_000_000
    clocks.observe(tracker)
    assert tracker.snapshot().level == 0
    assert tracker.snapshot().values['nonforward_stamp_count'] == '1'


def test_sensor_clock_restart_recovers_after_one_advancing_sample():
    clocks = Clocks()
    tracker = clocks.tracker()
    clocks.observe(tracker)
    clocks.ros_ns -= 1_000_000_000
    clocks.observe(tracker)
    assert tracker.snapshot().level == 2
    clocks.ros_ns += 100_000_000
    clocks.observe(tracker)
    assert tracker.snapshot().level == 0


def test_frame_mismatch_has_independent_diagnostic_and_recovers():
    clocks = Clocks()
    tracker = clocks.tracker()
    clocks.observe(tracker, frame='other_frame')
    health = tracker.snapshot()
    assert health.level == 2
    assert health.values['source'] == 'ok'
    assert health.values['frame'] == 'mismatch'
    clocks.ros_ns += 100_000_000
    clocks.observe(tracker)
    assert tracker.snapshot().level == 0
    assert tracker.snapshot().values['frame_mismatch_count'] == '1'


@pytest.mark.parametrize('timeout', [0.0, -1.0, 0.000999, 300.000001,
                                   True, None, float('nan'), float('inf')])
def test_invalid_timeout_is_rejected(timeout):
    with pytest.raises(ValueError, match='timeout_s'):
        Clocks().tracker(timeout)
