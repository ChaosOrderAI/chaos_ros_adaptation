"""Read-only host time quality and monotonic recording admission checks.

Chrony tracking describes the host clock, not a camera/IMU's hardware clock.
The error bound assumes that chrony's upstream reference itself is correct.
No function here adjusts the clock or changes the time service configuration.
"""

from dataclasses import asdict, dataclass
import csv
import math
import re
import subprocess
import time
from typing import Callable


NANOSECONDS = 1_000_000_000


def _positive(value: float, name: str) -> float:
    if isinstance(value, bool) or not math.isfinite(value) or value <= 0:
        raise ValueError(f'{name} must be finite and positive')
    return value


@dataclass(frozen=True)
class TimeSample:
    status: str
    reason: str
    sample_utc_ns: int
    sample_monotonic_ns: int
    source: str = 'chrony'
    offset_s: float | None = None
    uncertainty_s: float | None = None
    reference_utc_ns: int | None = None
    stratum: int | None = None
    reference_id: str | None = None
    reference_name: str | None = None
    root_delay_s: float | None = None
    root_dispersion_s: float | None = None
    leap_status: str | None = None
    offset_basis: str = 'ntp_minus_system'

    def to_dict(self) -> dict:
        return asdict(self)


def parse_tracking(
    output: str, *, wall_ns: int, monotonic_ns: int,
    max_uncertainty_s: float = 0.01, max_reference_age_s: float = 120.0,
) -> TimeSample:
    """Parse chrony 4.x ``chronyc -c tracking`` (14 CSV fields).

    System time's CSV sign is NTP minus system; the bound is
    abs(system time) + root dispersion + root delay / 2 (chronyc manual).
    """
    _positive(max_uncertainty_s, 'max_uncertainty_s')
    _positive(max_reference_age_s, 'max_reference_age_s')

    def result(status: str, reason: str, **values) -> TimeSample:
        return TimeSample(status, reason, wall_ns, monotonic_ns, **values)

    try:
        if not isinstance(output, str) or len(output) > 4096:
            raise ValueError('invalid response size')
        rows = list(csv.reader(output.strip().splitlines(), strict=True))
        if len(rows) != 1 or len(rows[0]) != 14:
            raise ValueError('expected one tracking record with 14 fields')
        fields = [item.strip() for item in rows[0]]
        if not re.fullmatch(r'[0-9A-Fa-f]{8}', fields[0]):
            raise ValueError('invalid reference ID')
        if not re.fullmatch(r'\d{1,2}', fields[2]):
            raise ValueError('invalid stratum')
        values = [float(item) for item in fields[3:13]]
        if not all(math.isfinite(item) for item in values):
            raise ValueError('nonfinite numeric field')
        reference, offset, _last, rms, _freq, _residual, skew, delay, dispersion, interval = values
        if min(reference, rms, skew, delay, dispersion, interval) < 0:
            raise ValueError('negative reference or uncertainty field')
        # ROS builtin_interfaces/Time uses signed 32-bit seconds. This also
        # bounds integer conversion of malformed but finite values.
        if reference >= 2**31 or abs(offset) >= 2**31:
            raise ValueError('out-of-range reference or offset')
        uncertainty = abs(offset) + dispersion + delay / 2.0
        if not math.isfinite(uncertainty):
            raise ValueError('nonfinite uncertainty')
        info = dict(
            offset_s=offset, uncertainty_s=uncertainty,
            reference_utc_ns=round(reference * NANOSECONDS), stratum=int(fields[2]),
            reference_id=fields[0].upper(), reference_name=fields[1],
            root_delay_s=delay, root_dispersion_s=dispersion, leap_status=fields[13],
        )
    except (ValueError, TypeError, csv.Error, OverflowError):
        return result('unknown', 'malformed_tracking')

    if info['reference_id'] == '00000000' or reference == 0:
        return result('unsynced', 'no_reference', **info)
    if info['reference_id'] == '7F7F0101':
        return result('unsynced', 'local_reference_without_external_sync', **info)
    if not 1 <= info['stratum'] <= 15:
        return result('unsynced', 'invalid_stratum', **info)
    if info['leap_status'] != 'Normal':
        return result('unsynced', 'leap_status_not_normal', **info)
    # A far-future reference is also suspect, including after a wall-clock step.
    reference_age_s = (wall_ns - info['reference_utc_ns']) / NANOSECONDS
    if reference_age_s < -max_uncertainty_s:
        return result('unsynced', 'reference_in_future', **info)
    if reference_age_s > max_reference_age_s:
        return result('unsynced', 'reference_stale', **info)
    if uncertainty > max_uncertainty_s:
        return result('unsynced', 'uncertainty_exceeded', **info)
    return result('synced', 'within_host_time_bound', **info)


class ChronyProbe:
    def __init__(
        self, *, max_uncertainty_s: float = 0.01,
        max_reference_age_s: float = 120.0, timeout_s: float = 1.0,
        runner: Callable = subprocess.run,
    ):
        self.max_uncertainty_s = _positive(max_uncertainty_s, 'max_uncertainty_s')
        self.max_reference_age_s = _positive(max_reference_age_s, 'max_reference_age_s')
        self.timeout_s = _positive(timeout_s, 'timeout_s')
        self._runner = runner

    def sample(self, wall_ns: int | None = None, monotonic_ns: int | None = None) -> TimeSample:
        reason = None
        output = ''
        try:
            completed = self._runner(
                ['chronyc', '-c', '-n', 'tracking'], capture_output=True,
                text=True, timeout=self.timeout_s, check=False,
            )
            if completed.returncode != 0:
                reason = 'chronyc_failed'
            else:
                output = completed.stdout
        except FileNotFoundError:
            reason = 'chronyc_not_found'
        except subprocess.TimeoutExpired:
            reason = 'chronyc_timeout'
        except (OSError, UnicodeError):
            reason = 'chronyc_unavailable'
        # Mark the completion time, not command start: no probe may be freshly
        # accepted simply because a subprocess has been blocked for a long time.
        wall_ns = time.time_ns() if wall_ns is None else wall_ns
        monotonic_ns = time.monotonic_ns() if monotonic_ns is None else monotonic_ns
        if reason is not None:
            return TimeSample('unknown', reason, wall_ns, monotonic_ns)
        return parse_tracking(
            output, wall_ns=wall_ns, monotonic_ns=monotonic_ns,
            max_uncertainty_s=self.max_uncertainty_s,
            max_reference_age_s=self.max_reference_age_s,
        )


@dataclass(frozen=True)
class ClockDecision:
    allowed: bool
    reason: str
    epoch: int
    jump_ns: int
    stable_samples: int
    time_sample: TimeSample

    def to_dict(self) -> dict:
        return asdict(self)


class ClockGuard:
    """Gate recording on fresh, distinct samples and detect host clock steps.

    ``strict=False`` records despite poor/unknown synchronisation, with the
    failure reason preserved. Epoch changes remain visible in either mode.
    Call update with actual wall and monotonic clocks, never ROS /clock.
    """

    def __init__(
        self, *, required_samples: int = 3, jump_threshold_s: float = 0.1,
        max_sample_age_s: float = 5.0, strict: bool = True,
    ):
        if type(required_samples) is not int or required_samples < 1:
            raise ValueError('required_samples must be a positive integer')
        if type(strict) is not bool:
            raise ValueError('strict must be boolean')
        self.required_samples = required_samples
        self.jump_threshold_ns = round(_positive(jump_threshold_s, 'jump_threshold_s') * NANOSECONDS)
        self.max_sample_age_ns = round(_positive(max_sample_age_s, 'max_sample_age_s') * NANOSECONDS)
        self.strict = strict
        self.epoch = 0
        self.stable_samples = 0
        self._last_wall_ns: int | None = None
        self._last_monotonic_ns: int | None = None
        self._last_sample_ns: int | None = None
        self._invalid_before_ns: int | None = None

    def reset(self, reason: str = 'paused') -> None:
        """Require fresh measurements after pause; preserve the step baseline."""
        del reason  # Calling code owns the pause/event description.
        self.stable_samples = 0
        if self._last_monotonic_ns is not None:
            self._invalid_before_ns = self._last_monotonic_ns

    def update(self, sample: TimeSample, wall_ns: int, monotonic_ns: int) -> ClockDecision:
        jump_ns = 0
        problem = None
        if self._last_wall_ns is not None:
            mono_delta = monotonic_ns - self._last_monotonic_ns
            jump_ns = wall_ns - self._last_wall_ns - mono_delta
            if mono_delta < 0:
                problem = 'monotonic_clock_reversed'
            elif abs(jump_ns) > self.jump_threshold_ns:
                problem = 'wall_clock_jump'
        self._last_wall_ns = wall_ns
        self._last_monotonic_ns = monotonic_ns
        if problem is not None:
            self.epoch += 1
            self.stable_samples = 0
            self._invalid_before_ns = monotonic_ns
            self._last_sample_ns = None
        else:
            age_ns = monotonic_ns - sample.sample_monotonic_ns
            if age_ns < 0 or age_ns > self.max_sample_age_ns:
                problem = 'time_sample_stale_or_future'
            elif (self._invalid_before_ns is not None
                  and sample.sample_monotonic_ns <= self._invalid_before_ns):
                problem = 'time_sample_before_epoch_or_pause'
            elif sample.status != 'synced':
                problem = sample.reason or 'host_not_synced'
            elif (sample.uncertainty_s is None or not math.isfinite(sample.uncertainty_s)
                  or sample.uncertainty_s < 0 or sample.offset_s is None
                  or not math.isfinite(sample.offset_s)):
                problem = 'invalid_time_sample'
            elif (self._last_sample_ns is not None
                  and sample.sample_monotonic_ns < self._last_sample_ns):
                problem = 'time_sample_out_of_order'
            else:
                if sample.sample_monotonic_ns != self._last_sample_ns:
                    self.stable_samples = min(self.required_samples, self.stable_samples + 1)
                    self._last_sample_ns = sample.sample_monotonic_ns
                if self.stable_samples < self.required_samples:
                    problem = 'waiting_for_stable_sync'
            if problem is not None and problem != 'waiting_for_stable_sync':
                self.stable_samples = 0
                # Do not let a previously seen good sample contribute again
                # after a stale/failed observation.
                self._invalid_before_ns = monotonic_ns
        return ClockDecision(
            allowed=(not self.strict or problem is None),
            reason=problem or 'ready', epoch=self.epoch,
            jump_ns=jump_ns, stable_samples=self.stable_samples, time_sample=sample,
        )
