"""Go2 transport safety and telemetry conversion, independent of ROS and the SDK."""

from dataclasses import dataclass
import math
import threading
import time

from chaos_runtime.control import Limits


NANOSECONDS = 1_000_000_000
ZERO = (0.0, 0.0, 0.0)


def stamp_ns(stamp):
    if (isinstance(stamp.sec, bool) or not isinstance(stamp.sec, int)
            or isinstance(stamp.nanosec, bool) or not isinstance(stamp.nanosec, int)
            or stamp.sec < 0 or not 0 <= stamp.nanosec < NANOSECONDS):
        raise ValueError('Invalid timestamp')
    return stamp.sec * NANOSECONDS + stamp.nanosec


def finite_vector(values, count, name):
    result = tuple(float(value) for value in values)
    if len(result) != count or not all(math.isfinite(value) for value in result):
        raise ValueError(f'{name} must contain {count} finite values')
    return result


def world_to_body(vector, quaternion):
    """Apply the inverse of the normalized xyzw body-to-odom rotation."""
    x, y, z, w = quaternion
    vx, vy, vz = vector
    return (
        (1 - 2 * (y*y + z*z))*vx + 2*(x*y + z*w)*vy + 2*(x*z - y*w)*vz,
        2*(x*y - z*w)*vx + (1 - 2*(x*x + z*z))*vy + 2*(y*z + x*w)*vz,
        2*(x*z + y*w)*vx + 2*(y*z - x*w)*vy + (1 - 2*(x*x + y*y))*vz,
    )


@dataclass(frozen=True)
class SportSample:
    received: float
    ros_stamp_ns: int
    position: tuple
    quaternion: tuple
    linear: tuple
    angular: tuple


@dataclass(frozen=True)
class Velocity:
    values: tuple
    monotonic_deadline: float
    source_deadline_ns: int
    received_ros_ns: int


class Go2DriverCore:
    """DDS callbacks cache state; one control thread owns all SDK writes.

    Never hold ``lock`` during Move or publication. The ROS wrapper uses a
    mutually exclusive command/timer callback group and a steady-clock timer.
    A failed SDK write latches a fault until restart; a successful subsequent
    zero write does not silently restore readiness or replay an old command.
    """

    def __init__(self, asset, *, clock=time.monotonic, ros_clock=time.time_ns):
        self.limits = Limits.from_dict(asset.get('limits', {}))
        self.base_frame = asset['frames']['base']
        self.odom_frame = asset['frames']['odom']
        self.velocity_frame = asset['driver'].get('options', {}).get('velocity_frame', 'odom')
        if self.velocity_frame not in ('odom', 'body'):
            raise ValueError('velocity_frame must be odom or body')
        self.clock, self.ros_clock = clock, ros_clock
        self.lock = threading.Lock()
        self.sample = None
        self.command = None
        self.last_command_stamp_ns = 0
        self.source_identity = None
        self.sequence = self.published_sequence = 0
        self.stop_pending = True
        self.stop_generation = 0
        self.moving = False
        self.initialized = False
        self.output_error = ''
        self.fault = ''
        self.closed = False
        self.detail = 'Waiting for fresh sport telemetry and initial zero output'

    def _stop_locked(self):
        self.command = None
        self.stop_pending = True
        self.stop_generation += 1

    def request_stop(self, *, closing=False):
        with self.lock:
            self.closed = self.closed or closing
            self._stop_locked()

    def _state_fresh_locked(self, now):
        return (self.sample is not None
                and 0 <= now - self.sample.received < self.limits.state_timeout_s)

    @staticmethod
    def _identity(message):
        # Current Go2 SDK2 SportModeState has stamp, not tick. Some firmware
        # variants expose a uint32 tick instead; use wrap-aware ordering there.
        if hasattr(message, 'tick'):
            tick = message.tick
            if isinstance(tick, bool) or not isinstance(tick, int) or not 0 <= tick < 2**32:
                raise ValueError('Invalid sport tick')
            return 'tick', tick
        if hasattr(message, 'stamp'):
            return 'stamp', stamp_ns(message.stamp)
        # For a type without a source counter each DDS callback is one sample.
        return None

    def accept_state(self, message):
        received, ros_ns = self.clock(), self.ros_clock()
        try:
            identity = self._identity(message)
            position = finite_vector(message.position, 3, 'position')
            w, x, y, z = finite_vector(message.imu_state.quaternion, 4, 'quaternion')
            norm = math.hypot(w, x, y, z)
            if abs(norm - 1.0) > 0.01:
                raise ValueError('Invalid orientation quaternion')
            quaternion = (x / norm, y / norm, z / norm, w / norm)
            linear = finite_vector(message.velocity, 3, 'velocity')
            angular = finite_vector(message.imu_state.gyroscope, 3, 'gyroscope')
            if not math.isfinite(float(message.yaw_speed)):
                raise ValueError('Invalid yaw speed')
            if message.error_code != 0:
                raise ValueError(f'Go2 sport error_code={message.error_code}')
            if ros_ns <= 0:
                raise ValueError('ROS clock has not started')
            if self.velocity_frame == 'odom':
                # Unitree's official read_motion_state.cpp identifies velocity
                # as odometry-frame data. ROS Odometry twist uses child_frame.
                # https://github.com/unitreerobotics/unitree_ros2/blob/master/example/src/src/read_motion_state.cpp
                linear = world_to_body(linear, quaternion)
                linear = finite_vector(linear, 3, 'body velocity')
            sample = SportSample(received, ros_ns, position, quaternion, linear, angular)
        except (AttributeError, TypeError, ValueError, OverflowError) as error:
            with self.lock:
                self.sample = None
                self.detail = f'Invalid sport telemetry: {error}'
                self._stop_locked()
            return False
        with self.lock:
            if self.closed:
                return False
            previous = self.source_identity
            if identity is not None and previous is not None:
                if identity[0] != previous[0]:
                    self.detail = 'Sport telemetry clock changed; restart driver'
                    self.sample = None
                    self._stop_locked()
                    return False
                delta = identity[1] - previous[1]
                if identity[0] == 'tick':
                    delta %= 2**32
                    newer = 0 < delta < 2**31
                else:
                    newer = delta > 0
                if not newer:
                    # Repeated/backward source counters cannot keep a dead
                    # publisher alive, even if DDS keeps delivering callbacks.
                    return False
            self.source_identity = identity
            self.sample = sample
            self.sequence += 1
            return True

    def accept_command(self, message):
        received, ros_ns = self.clock(), self.ros_clock()
        try:
            source_ns = stamp_ns(message.header.stamp)
            ttl_ns = int(self.limits.command_timeout_s * NANOSECONDS)
            age_ns = ros_ns - source_ns
            if source_ns <= 0 or not 0 <= age_ns < ttl_ns:
                raise ValueError('Velocity command timestamp is missing, stale or in the future')
            if message.header.frame_id != self.base_frame:
                raise ValueError(f'Velocity command must use frame {self.base_frame}')
            twist = message.twist
            vx, vy, vz, wx, wy, wz = finite_vector(
                (twist.linear.x, twist.linear.y, twist.linear.z,
                 twist.angular.x, twist.angular.y, twist.angular.z), 6, 'velocity command')
            if vz != 0.0 or wx != 0.0 or wy != 0.0:
                raise ValueError('Go2 accepts only body x/y translation and yaw rotation')
            if math.hypot(vx, vy) > self.limits.linear_mps or abs(wz) > self.limits.angular_rps:
                raise ValueError('Velocity command exceeds configured speed limits')
            values = vx, vy, wz
            # Source age consumes the receiving watchdog budget; later ROS
            # clock corrections can never extend this monotonic deadline.
            command = Velocity(values, received + (ttl_ns - age_ns) / NANOSECONDS,
                               source_ns + ttl_ns, ros_ns)
        except (AttributeError, TypeError, ValueError, OverflowError) as error:
            with self.lock:
                self.detail = str(error)
                self._stop_locked()
            return False
        with self.lock:
            if values == ZERO:
                # A stop is also a source-time barrier: delayed commands sent
                # before it must not restart motion after the zero is issued.
                self.last_command_stamp_ns = max(self.last_command_stamp_ns, source_ns)
                self._stop_locked()
                return True
            if source_ns <= self.last_command_stamp_ns:
                self.detail = 'Repeated or backward velocity command timestamp'
                return False
            if (self.closed or self.fault or self.stop_pending
                    or not self._state_fresh_locked(self.clock())):
                self.detail = self.fault or 'Motion requires fresh telemetry and a successful zero output'
                self._stop_locked()
                return False
            self.command = command
            self.last_command_stamp_ns = source_ns
            return True

    def _expire_locked(self, now, ros_ns):
        fresh = self._state_fresh_locked(now)
        command = self.command
        expired = command is not None and (
            now >= command.monotonic_deadline or ros_ns >= command.source_deadline_ns
            or ros_ns < command.received_ros_ns)
        if (not fresh or expired or self.closed or self.fault) and command is not None:
            self._stop_locked()
        if not fresh:
            self.detail = 'Waiting for fresh sport telemetry'
        elif expired:
            self.detail = 'Velocity command expired'
        return fresh

    def _write(self, move, values, generation):
        try:
            code = move(*values)
            if isinstance(code, bool) or not isinstance(code, int) or code != 0:
                raise RuntimeError(f'Move returned {code!r}')
        except Exception as error:
            with self.lock:
                self.output_error = f'Go2 velocity output failed: {error}'
                # DDS discovery of the sport API writer may lag telemetry.
                # Initial zero failures keep the driver unready and retry;
                # failures after initialization are permanently latched.
                if self.initialized:
                    self.fault = self.output_error
                self.detail = self.output_error
                self._stop_locked()
            return False
        with self.lock:
            self.moving = values != ZERO
            if values == ZERO and generation == self.stop_generation:
                self.stop_pending = False
                self.initialized = True
            self.output_error = ''
        return True

    def step(self, move):
        """Run one watchdog iteration and return at most one fresh odom sample.

        ``move`` is SportClient.Move: its configured send timeout is bounded,
        and its no-reply path avoids StopMove's synchronous RPC wait. The return
        code confirms only local output, never physical robot response.
        """
        with self.lock:
            fresh = self._expire_locked(self.clock(), self.ros_clock())
            if not self.initialized and not fresh and not self.closed:
                return None
            values = ZERO if self.stop_pending else (self.command.values if self.command else None)
            generation = self.stop_generation
        if values is not None:
            self._write(move, values, generation)
            # A source expiry, bad DDS sample, or failed write during I/O must
            # immediately replace a nonzero output with a zero attempt.
            if values != ZERO:
                with self.lock:
                    self._expire_locked(self.clock(), self.ros_clock())
                    stop, generation = self.stop_pending, self.stop_generation
                if stop:
                    self._write(move, ZERO, generation)
        with self.lock:
            fresh = self._expire_locked(self.clock(), self.ros_clock())
            if (not fresh or self.fault or self.stop_pending or self.closed
                    or self.sequence == self.published_sequence):
                return None
            self.published_sequence = self.sequence
            return self.sample


def fill_odometry(message, sample, *, odom_frame, base_frame):
    """Keep the DDS acquisition timestamp; timer publication never refreshes it."""
    message.header.stamp.sec, message.header.stamp.nanosec = divmod(sample.ros_stamp_ns, NANOSECONDS)
    message.header.frame_id, message.child_frame_id = odom_frame, base_frame
    pose, twist = message.pose.pose, message.twist.twist
    pose.position.x, pose.position.y, pose.position.z = sample.position
    pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w = sample.quaternion
    twist.linear.x, twist.linear.y, twist.linear.z = sample.linear
    twist.angular.x, twist.angular.y, twist.angular.z = sample.angular
    return message
