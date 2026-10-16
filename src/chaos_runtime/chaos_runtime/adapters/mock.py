"""Deterministic synthetic telemetry for integration tests, not a physics simulator."""
import math

from chaos_runtime.adapters.base import Adapter, Snapshot
from chaos_runtime.control import Rejected


class MockAdapter(Adapter):
    option_names = frozenset({'kind'})
    supported_capabilities = frozenset({
        'state', 'motion.velocity', 'motion.stop', 'navigation.pose',
        'flight.takeoff', 'flight.land',
    })

    @classmethod
    def capabilities_for(cls, asset):
        kind = asset['driver'].get('options', {}).get('kind', asset.get('kind', 'ugv'))
        if kind == 'ugv':
            return cls.supported_capabilities - {'flight.takeoff', 'flight.land'}
        return cls.supported_capabilities

    @classmethod
    def validate_options(cls, asset):
        super().validate_options(asset)
        kind = asset['driver'].get('options', {}).get('kind', asset.get('kind', 'ugv'))
        if kind not in ('ugv', 'uav'):
            raise ValueError('mock kind must be ugv or uav')
        if asset.get('kind', kind) != kind:
            raise ValueError('mock options.kind must agree with the asset kind')

    def __init__(self, node, asset, limits):
        super().__init__(node, asset, limits)
        self.kind = asset['driver'].get('options', {}).get('kind', asset.get('kind', 'ugv'))
        self.position = [0.0, 0.0, 0.0]
        self.yaw = 0.0
        self.velocity = [0.0] * 6
        self.target = None
        self.initial_distance = 0.0
        self.progress = 0.0
        self.finished = False

    def snapshot(self):
        return Snapshot(
            connected=True, ready=True, pose_valid=True,
            position=list(self.position),
            quaternion=[0.0, 0.0, math.sin(self.yaw / 2), math.cos(self.yaw / 2)],
            velocity=list(self.velocity), battery=1.0,
            detail='Synthetic mock telemetry; no hardware connected',
            pose_frame=self.asset['frames']['map'],
            twist_frame=self.asset['frames']['base'],
        )

    def validate_velocity(self, values):
        if abs(values[3]) > 1e-9 or abs(values[4]) > 1e-9:
            raise Rejected('UNSUPPORTED', 'Mock velocity supports yaw angular velocity only')
        if self.kind == 'ugv' and (abs(values[1]) > 1e-9 or abs(values[2]) > 1e-9):
            raise Rejected('UNSUPPORTED', 'Ground velocity supports linear x and angular z only')

    def set_velocity(self, values):
        self.validate_velocity(values)
        self.velocity = list(values)

    def stop(self):
        self.velocity = [0.0] * 6
        self.target = None
        self.finished = False

    def start_operation(self, kind, target):
        self.velocity = [0.0] * 6
        if kind == 'navigation.pose':
            self.target = list(target)
        elif kind == 'flight.takeoff':
            if self.kind != 'uav':
                raise Rejected('UNSUPPORTED', 'Only mock UAV supports takeoff')
            if self.position[2] > 0.1:
                raise Rejected('INVALID_STATE', 'Mock UAV is already airborne')
            self.target = [self.position[0], self.position[1], target, self.yaw]
        elif kind == 'flight.land':
            if self.kind != 'uav':
                raise Rejected('UNSUPPORTED', 'Only mock UAV supports landing')
            self.target = [self.position[0], self.position[1], 0.0, self.yaw]
        else:
            raise Rejected('UNSUPPORTED', kind)
        self.initial_distance = self._distance()
        self.progress = 0.0
        self.finished = False

    def _distance(self):
        distance = math.dist(self.position, self.target[:3])
        angle = abs(math.remainder(self.target[3] - self.yaw, 2 * math.pi))
        return distance + angle

    def update(self, dt):
        dt = max(0.0, min(dt, 0.2))
        if self.target is not None:
            old_position, old_yaw = list(self.position), self.yaw
            delta = [t - p for p, t in zip(self.position, self.target[:3])]
            distance = math.hypot(*delta)
            step = min(distance, self.limits.linear_mps * dt)
            if distance > 1e-9:
                self.position = [p + d / distance * step for p, d in zip(self.position, delta)]
            angle = math.remainder(self.target[3] - self.yaw, 2 * math.pi)
            self.yaw += max(-self.limits.angular_rps * dt, min(self.limits.angular_rps * dt, angle))
            if dt > 0:
                dx, dy, dz = [(new - old) / dt for old, new in zip(old_position, self.position)]
                self.velocity = [dx * math.cos(self.yaw) + dy * math.sin(self.yaw),
                                 -dx * math.sin(self.yaw) + dy * math.cos(self.yaw),
                                 dz, 0.0, 0.0, (self.yaw - old_yaw) / dt]
            remaining = self._distance()
            self.progress = max(0.0, min(1.0, 1 - remaining / max(self.initial_distance, 1e-9)))
            if remaining < 1e-5:
                self.position, self.yaw = self.target[:3], self.target[3]
                self.target = None
                self.finished = True
                self.progress = 1.0
                self.velocity = [0.0] * 6
            return
        vx, vy, vz, _, _, wz = self.velocity
        self.position[0] += (vx * math.cos(self.yaw) - vy * math.sin(self.yaw)) * dt
        self.position[1] += (vx * math.sin(self.yaw) + vy * math.cos(self.yaw)) * dt
        previous_z = self.position[2]
        self.position[2] = max(0.0, min(self.limits.max_altitude_m, previous_z + vz * dt))
        if dt > 0 and self.position[2] in (0.0, self.limits.max_altitude_m):
            self.velocity[2] = (self.position[2] - previous_z) / dt
        self.yaw = math.remainder(self.yaw + wz * dt, 2 * math.pi)

    def poll_operation(self):
        return self.finished, self.finished, 'OK', 'Mock target reached', self.progress
