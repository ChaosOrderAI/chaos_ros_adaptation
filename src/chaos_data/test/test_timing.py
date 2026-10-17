from dataclasses import replace
from types import SimpleNamespace
import subprocess

import pytest

from chaos_data.timing import ChronyProbe, ClockGuard, parse_tracking


WALL = 1_700_000_000_000_000_000
MONO = 1_000_000_000
TRACKING = 'CB00710F,203.0.113.15,3,1699999999.0,0.001,-0.0001,0.0002,1.2,0.1,0.02,0.002,0.0005,64.0,Normal\n'


def sample(output=TRACKING, *, tick=0, **kwargs):
    return parse_tracking(output, wall_ns=WALL + tick, monotonic_ns=MONO + tick, **kwargs)


def changed(index, value):
    fields = TRACKING.strip().split(',')
    fields[index] = value
    return ','.join(fields)


def test_tracking_keeps_signed_correction_and_absolute_bound():
    result = sample(changed(4, '-0.001'))
    assert result.status == 'synced'
    assert result.offset_s == -0.001
    assert result.offset_basis == 'ntp_minus_system'
    assert result.uncertainty_s == pytest.approx(0.0025)
    assert result.reference_id == 'CB00710F'


@pytest.mark.parametrize('index,value,reason', [
    (0, '00000000', 'no_reference'),
    (0, '7F7F0101', 'local_reference_without_external_sync'),
    (2, '0', 'invalid_stratum'), (2, '16', 'invalid_stratum'),
    (3, '0', 'no_reference'), (3, '1699999879', 'reference_stale'),
    (3, '1700000001', 'reference_in_future'),
    (4, '0.02', 'uncertainty_exceeded'),
    (10, '.03', 'uncertainty_exceeded'),
    (11, '.03', 'uncertainty_exceeded'),
    (13, 'Not synchronised', 'leap_status_not_normal'),
    (13, 'Insert second', 'leap_status_not_normal'),
    (13, 'Delete second', 'leap_status_not_normal'),
])
def test_unsynchronised_or_excess_uncertainty_never_passes(index, value, reason):
    result = sample(changed(index, value))
    assert result.status == 'unsynced'
    assert result.reason == reason


@pytest.mark.parametrize('output', [
    '', TRACKING + TRACKING, TRACKING[:-10], 'x' * 4097,
    changed(0, 'garbage'), changed(2, '3.0'), changed(2, '-1'),
    changed(4, 'NaN'), changed(11, 'inf'), changed(9, '-1'),
    changed(10, '-1'), changed(11, '-1'), changed(3, '1e100'),
    changed(4, '1e100'), changed(6, 'nan'),
])
def test_malformed_fields_cannot_be_mistaken_for_good_clock(output):
    assert sample(output).status == 'unknown'


def test_probe_only_runs_read_only_tracking_command():
    calls = []

    def run(command, **kwargs):
        calls.append((command, kwargs))
        return SimpleNamespace(returncode=0, stdout=TRACKING)

    result = ChronyProbe(runner=run).sample(WALL, MONO)
    assert result.status == 'synced'
    assert calls == [(['chronyc', '-c', '-n', 'tracking'], {
        'capture_output': True, 'text': True, 'timeout': 1.0, 'check': False,
    })]


@pytest.mark.parametrize('error,reason', [
    (FileNotFoundError(), 'chronyc_not_found'),
    (subprocess.TimeoutExpired('chronyc', 1), 'chronyc_timeout'),
    (PermissionError(), 'chronyc_unavailable'),
])
def test_probe_failure_remains_unknown(error, reason):
    def run(*args, **kwargs):
        raise error
    result = ChronyProbe(runner=run).sample(WALL, MONO)
    assert result.status == 'unknown' and result.reason == reason
    assert result.uncertainty_s is None


def test_nonzero_probe_exit_cannot_use_valid_looking_stdout():
    probe = ChronyProbe(runner=lambda *args, **kwargs: SimpleNamespace(returncode=1, stdout=TRACKING))
    assert probe.sample(WALL, MONO).reason == 'chronyc_failed'


def update(guard, tick=0, *, offset=0, reading=None):
    return guard.update(reading or sample(tick=tick), WALL + tick + offset, MONO + tick)


def test_guard_requires_distinct_consecutive_measurements():
    guard = ClockGuard()
    assert not update(guard).allowed
    assert not update(guard).allowed
    assert guard.stable_samples == 1
    assert not update(guard, 1_000_000_000).allowed
    assert update(guard, 2_000_000_000).allowed
    assert guard.stable_samples == 3


def test_bad_sync_resets_admission_and_old_good_sample_cannot_recover():
    guard = ClockGuard(required_samples=1)
    old = sample()
    assert update(guard, reading=old).allowed
    bad = replace(sample(tick=1_000_000_000), status='unknown', reason='offline')
    assert not update(guard, 1_000_000_000, reading=bad).allowed
    assert not update(guard, 2_000_000_000, reading=old).allowed
    assert update(guard, 3_000_000_000).allowed


def test_sample_expiry_uses_monotonic_time_and_requires_fresh_recovery():
    guard = ClockGuard(required_samples=1, max_sample_age_s=2)
    old = sample()
    assert update(guard, reading=old).allowed
    assert update(guard, 2_000_000_000, reading=old).allowed
    assert not update(guard, 2_000_000_001, reading=old).allowed
    assert update(guard, 3_000_000_000).allowed


@pytest.mark.parametrize('step', [-200_000_000, 200_000_000])
def test_clock_steps_make_new_epoch_and_need_fresh_stable_samples(step):
    guard = ClockGuard(required_samples=1)
    assert update(guard).allowed
    jumped = update(guard, 1_000_000_000, offset=step)
    assert not jumped.allowed
    assert jumped.reason == 'wall_clock_jump'
    assert jumped.epoch == 1 and jumped.jump_ns == step
    assert jumped.stable_samples == 0
    recovered = update(guard, 2_000_000_000, offset=step)
    assert recovered.allowed and recovered.epoch == 1


def test_pause_does_not_forget_jump_baseline_or_reuse_old_sample():
    guard = ClockGuard(required_samples=1)
    assert update(guard).allowed
    guard.reset()
    assert not update(guard).allowed
    assert update(guard, 1_000_000_000).allowed
    guard.reset()
    assert update(guard, 2_000_000_000, offset=300_000_000).epoch == 1


def test_observe_mode_preserves_unknown_status_and_epoch():
    guard = ClockGuard(strict=False)
    unknown = replace(sample(), status='unknown', reason='missing')
    decision = update(guard, reading=unknown)
    assert decision.allowed and decision.reason == 'missing'
    decision = update(guard, 1_000_000_000, offset=-1_000_000_000)
    assert decision.allowed and decision.epoch == 1
    assert decision.reason == 'wall_clock_jump'


def test_monotonic_reversal_starts_epoch():
    guard = ClockGuard(required_samples=1)
    assert update(guard).allowed
    decision = guard.update(sample(), WALL, MONO - 1)
    assert not decision.allowed and decision.epoch == 1
    assert decision.reason == 'monotonic_clock_reversed'


@pytest.mark.parametrize('fields', [
    {'uncertainty_s': None}, {'uncertainty_s': float('inf')},
    {'uncertainty_s': -.1}, {'offset_s': float('nan')}, {'offset_s': None},
])
def test_incomplete_synced_sample_is_not_accepted(fields):
    guard = ClockGuard(required_samples=1)
    assert not update(guard, reading=replace(sample(), **fields)).allowed


@pytest.mark.parametrize('kwargs', [
    {'required_samples': 0}, {'required_samples': True}, {'strict': 1},
    {'jump_threshold_s': float('nan')}, {'max_sample_age_s': 0},
])
def test_invalid_guard_configuration_rejected(kwargs):
    with pytest.raises(ValueError):
        ClockGuard(**kwargs)
