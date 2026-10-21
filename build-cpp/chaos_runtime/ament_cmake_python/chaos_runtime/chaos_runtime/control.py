"""Control validation with monotonic leases and bounded Unix ingress deadlines."""

from dataclasses import dataclass
import math
import secrets
import time


class Rejected(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def finite_positive(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise Rejected('INVALID_ARGUMENT', f'{name} must be finite and positive')
    return float(value)


@dataclass(frozen=True)
class CommandDeadline:
    """An ingress deadline cannot be extended by a later wall-clock correction."""

    unix_ns: int
    monotonic_ns: int
    valid: bool = True

    @classmethod
    def capture(cls, stamp):
        # Capture before taking the device lock so lock contention consumes TTL.
        unix_ns = stamp.sec * 1_000_000_000 + stamp.nanosec
        valid = stamp.sec >= 0 and 0 <= stamp.nanosec < 1_000_000_000
        monotonic_ns = time.monotonic_ns()
        remaining_ns = unix_ns - time.time_ns()
        # Read monotonic first: preemption between the two clock reads may
        # shorten the allowed interval but must never extend it.
        return cls(unix_ns, monotonic_ns + remaining_ns, valid)

    def require(self):
        if not self.valid:
            raise Rejected('INVALID_ARGUMENT', 'expires_at must be a valid Unix UTC timestamp')
        if self.unix_ns and (time.time_ns() >= self.unix_ns
                             or time.monotonic_ns() >= self.monotonic_ns):
            raise Rejected('STALE_COMMAND', 'Command expired before execution')


@dataclass(frozen=True)
class Limits:
    linear_mps: float = 1.0
    angular_rps: float = 1.0
    command_timeout_s: float = 0.5
    state_timeout_s: float = 2.0
    lease_max_s: float = 30.0
    action_timeout_s: float = 120.0
    max_altitude_m: float = 20.0

    @classmethod
    def from_dict(cls, values):
        names = cls.__dataclass_fields__
        supplied = {key: finite_positive(value, key) for key, value in values.items() if key in names}
        return cls(**supplied)


class ControlGuard:
    """Called under the runtime lock; injected clocks make timing rules testable."""

    def __init__(self, limits, clock=time.monotonic, token_factory=None):
        self.limits = limits
        self.clock = clock
        self.token_factory = token_factory or (lambda: secrets.token_urlsafe(32))
        self.owner = ''
        self.token = ''
        self.expires = 0.0
        self.velocity_expires = 0.0

    @property
    def remaining(self):
        return max(0.0, self.expires - self.clock()) if self.token else 0.0

    def expire(self):
        if self.token and self.remaining <= 0:
            self.owner = self.token = ''
            return True
        return False

    def require(self, token):
        self.expire()
        if (not isinstance(token, str) or not token or not token.isascii() or not self.token
                or not secrets.compare_digest(token, self.token)):
            raise Rejected('NO_CONTROL', 'A current control lease is required')

    def lease(self, operation, owner, token='', duration_s=5.0):
        self.expire()
        if not isinstance(owner, str) or not owner.strip() or len(owner) > 128:
            raise Rejected('INVALID_ARGUMENT', 'owner must be a nonempty string of at most 128 characters')
        if operation not in ('acquire', 'renew', 'release'):
            raise Rejected('INVALID_ARGUMENT', 'operation must be acquire, renew or release')
        if operation != 'release':
            duration_s = finite_positive(duration_s, 'duration_s')
            if duration_s > self.limits.lease_max_s:
                raise Rejected('OUT_OF_RANGE', 'duration_s exceeds lease_max_s')
        if operation == 'acquire':
            if self.token:
                raise Rejected('BUSY', 'Control is already leased')
            self.owner, self.token = owner, self.token_factory()
            self.expires = self.clock() + duration_s
        else:
            self.require(token)
            if self.owner != owner:
                raise Rejected('NO_CONTROL', 'Lease owner does not match')
            if operation == 'renew':
                self.expires = self.clock() + duration_s
            else:
                self.owner = self.token = ''
                self.expires = self.velocity_expires = 0.0
        return self.token, self.remaining

    def velocity(self, token, stamp_s, ros_now_s, valid_for_s, frame, base_frame, values):
        self.require(token)
        ttl = finite_positive(valid_for_s, 'valid_for_s')
        if ttl > self.limits.command_timeout_s:
            raise Rejected('OUT_OF_RANGE', 'valid_for_s exceeds command_timeout_s')
        if frame != base_frame:
            raise Rejected('INVALID_FRAME', f'Expected velocity frame {base_frame}')
        if len(values) != 6 or not all(math.isfinite(v) for v in values):
            raise Rejected('INVALID_ARGUMENT', 'Velocity components must be finite')
        if math.hypot(*values[:3]) > self.limits.linear_mps + 1e-9:
            raise Rejected('OUT_OF_RANGE', 'Linear speed exceeds asset limit')
        if math.hypot(*values[3:]) > self.limits.angular_rps + 1e-9:
            raise Rejected('OUT_OF_RANGE', 'Angular speed exceeds asset limit')
        if not math.isfinite(stamp_s) or stamp_s <= 0:
            raise Rejected('STALE_COMMAND', 'A positive source timestamp is required')
        age = ros_now_s - stamp_s
        if not math.isfinite(age) or age < -0.1 or age >= ttl:
            raise Rejected('STALE_COMMAND', 'Command is expired or timestamp is in the future')
        # Network transit consumes TTL. Slight future skew never extends beyond TTL.
        self.velocity_expires = self.clock() + min(ttl, ttl - age)

    def velocity_timed_out(self):
        return bool(self.velocity_expires and self.clock() >= self.velocity_expires)


def validate_pose(frame, expected_frame, position, quaternion, kind, max_altitude):
    if frame != expected_frame:
        raise Rejected('INVALID_FRAME', f'Expected navigation frame {expected_frame}')
    if not all(math.isfinite(v) for v in (*position, *quaternion)):
        raise Rejected('INVALID_ARGUMENT', 'Pose must be finite')
    if abs(math.hypot(*quaternion) - 1.0) > 0.01:
        raise Rejected('INVALID_ARGUMENT', 'Orientation quaternion must be normalized')
    # First adapters support planar yaw, never silently discard roll/pitch.
    if abs(quaternion[0]) > 1e-6 or abs(quaternion[1]) > 1e-6:
        raise Rejected('UNSUPPORTED', 'Navigation orientation supports yaw only')
    if kind in ('ugv', 'quadruped') and abs(position[2]) > 1e-6:
        raise Rejected('UNSUPPORTED', 'Ground navigation requires z=0')
    if kind == 'uav' and not 0 <= position[2] <= max_altitude:
        raise Rejected('OUT_OF_RANGE', 'Navigation altitude exceeds limits')
