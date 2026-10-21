"""Metadata-only freshness checks, with injectable clocks and no ROS dependency.

These checks cannot detect a driver that assigns a new timestamp to cached hardware
data. Drivers must preserve the measurement timestamp for that guarantee.
"""

from dataclasses import dataclass
import math
import time
from typing import Callable


@dataclass(frozen=True)
class Health:
    level: int
    message: str
    values: dict[str, str]


class HealthTracker:
    """Track receipt and source freshness independently for a single stream."""

    def __init__(self, frame_id: str, timeout_s: float,
                 ros_now_ns: Callable[[], int],
                 monotonic: Callable[[], float] = time.monotonic):
        if not frame_id:
            raise ValueError('frame_id must not be empty')
        if (isinstance(timeout_s, bool) or not isinstance(timeout_s, (int, float)) or
                not math.isfinite(timeout_s) or not 0.001 <= timeout_s <= 300.0):
            raise ValueError('timeout_s must be finite and between 0.001 and 300 seconds')
        self.frame_id = frame_id
        self.timeout_s = timeout_s
        self._ros_now_ns = ros_now_ns
        self._monotonic = monotonic
        self._receipt = None
        self._stamp_ns = None
        self._previous_stamp_ns = None
        self._source_issue = ''
        self._received_frame = ''
        self.received_count = 0
        self.invalid_stamp_count = 0
        self.nonforward_stamp_count = 0
        self.frame_mismatch_count = 0

    def observe(self, *, sec: int, nanosec: int, frame_id: str) -> None:
        self._receipt = self._monotonic()
        self._received_frame = frame_id
        self.received_count += 1
        if frame_id != self.frame_id:
            self.frame_mismatch_count += 1
        self._source_issue = ''
        if sec < 0 or not 0 <= nanosec < 1_000_000_000:
            self._stamp_ns = None
            self._source_issue = 'invalid source timestamp'
            self.invalid_stamp_count += 1
            return
        self._stamp_ns = sec * 1_000_000_000 + nanosec
        if self._stamp_ns == 0:
            self._source_issue = 'zero source timestamp'
            self.invalid_stamp_count += 1
            return
        if (self._previous_stamp_ns is not None and
                self._stamp_ns <= self._previous_stamp_ns):
            self._source_issue = 'source timestamp did not advance'
            self.nonforward_stamp_count += 1
        # A restarted sensor/ROS clock can recover on its next advancing sample.
        self._previous_stamp_ns = self._stamp_ns

    def snapshot(self) -> Health:
        values = {
            'expected_frame': self.frame_id,
            'received_frame': self._received_frame,
            'timeout_s': str(self.timeout_s),
            'received_count': str(self.received_count),
            'invalid_stamp_count': str(self.invalid_stamp_count),
            'nonforward_stamp_count': str(self.nonforward_stamp_count),
            'frame_mismatch_count': str(self.frame_mismatch_count),
        }
        if self._receipt is None:
            values.update(receipt='no data', source='no data', frame='no data')
            return Health(3, 'no messages received', values)
        issues = []
        level = 0
        age = max(0.0, self._monotonic() - self._receipt)
        values['receipt_age_s'] = f'{age:.6f}'
        values['receipt'] = 'ok'
        if age > self.timeout_s:
            values['receipt'] = 'timeout'
            issues.append('receipt timeout')
            level = 3
        values['frame'] = 'ok'
        if self._received_frame != self.frame_id:
            values['frame'] = 'mismatch'
            issues.append('frame mismatch')
            level = max(level, 2)
        source_issues = []
        if self._source_issue:
            source_issues.append(self._source_issue)
        if self._stamp_ns is not None and self._stamp_ns > 0:
            source_age_s = (self._ros_now_ns() - self._stamp_ns) / 1_000_000_000
            values['source_age_s'] = f'{source_age_s:.6f}'
            if source_age_s > self.timeout_s:
                source_issues.append('source timestamp expired')
            elif source_age_s < -0.1:
                source_issues.append('source timestamp is in the future')
        values['source'] = '; '.join(source_issues) if source_issues else 'ok'
        if source_issues:
            issues.extend(source_issues)
            level = max(level, 2)
        return Health(level, '; '.join(issues) if issues else 'receiving fresh data', values)
