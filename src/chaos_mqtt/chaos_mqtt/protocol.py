"""Validated wire commands and bounded correlation, independently testable."""

from collections import OrderedDict
from dataclasses import dataclass
import json
import math
import re

from .config import COMMAND_CAPABILITIES


class CommandError(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def wire_value(value):
    """Use JSON null for unavailable ROS floating-point telemetry."""
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {key: wire_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [wire_value(item) for item in value]
    return value


def number(value, field_name, minimum=None, maximum=None):
    try:
        finite = not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value)
    except OverflowError:
        finite = False
    if not finite:
        raise CommandError('INVALID_ARGUMENT', f'{field_name} must be finite numeric')
    if minimum is not None and value < minimum or maximum is not None and value > maximum:
        raise CommandError('INVALID_ARGUMENT', f'{field_name} is out of range')
    return float(value)


def nonempty(value, field_name):
    if not isinstance(value, str) or not value or len(value) > 256:
        raise CommandError('INVALID_ARGUMENT', f'{field_name} must be a nonempty string (max 256)')
    return value


def fields(value, allowed, field_name):
    if not isinstance(value, dict) or set(value) - set(allowed):
        raise CommandError('INVALID_ARGUMENT', f'{field_name} contains unsupported fields')
    return value


def vector(value, axes, field_name, defaults=None):
    value = fields(value, axes, field_name)
    defaults = defaults or {}
    return {axis: number(value.get(axis, defaults.get(axis, 0.0)), f'{field_name}.{axis}')
            for axis in axes}


@dataclass(frozen=True)
class Command:
    request_id: str
    name: str
    deadline_ms: int
    arguments: dict


def parse_command(data, capabilities, now_ms, maximum_clock_skew_ms=5000):
    """Validate the Rust String envelope and preserve the sender's deadline.

    TTL uses Unix time, independent of ROS simulation time. Services and actions
    receive that exact deadline so executor queueing never extends validity.
    """
    if not isinstance(data, str) or len(data.encode('utf-8')) > 16384:
        raise CommandError('INVALID_ARGUMENT', 'command payload exceeds 16384 bytes')
    try:
        envelope = json.loads(data)
    except (ValueError, TypeError, RecursionError) as error:
        raise CommandError('INVALID_ARGUMENT', 'invalid JSON') from error
    envelope = fields(envelope, ('bridge', 'payload'), 'envelope')
    bridge = fields(envelope.get('bridge'), ('source', 'request_id', 'timestamp_ms'), 'bridge')
    if bridge.get('source') != 'mqtt':
        raise CommandError('INVALID_ARGUMENT', 'bridge.source must be mqtt')
    request_id = bridge.get('request_id')
    if not isinstance(request_id, str) or not re.fullmatch(r'[A-Za-z0-9_-]{8,64}', request_id):
        raise CommandError('INVALID_ARGUMENT', 'invalid bridge.request_id')
    timestamp = bridge.get('timestamp_ms')
    if (isinstance(timestamp, bool) or not isinstance(timestamp, int)
            or not 0 < timestamp < (2 ** 31) * 1000):
        raise CommandError('INVALID_ARGUMENT', 'timestamp_ms must be positive Unix milliseconds within ROS Time range')
    payload = envelope.get('payload')
    if not isinstance(payload, dict):
        raise CommandError('INVALID_ARGUMENT', 'payload must be an object')
    name = payload.get('command')
    if not isinstance(name, str) or name not in COMMAND_CAPABILITIES:
        raise CommandError('UNSUPPORTED', 'unknown command')
    capability = COMMAND_CAPABILITIES[name]
    if capability is not None and capability not in capabilities:
        raise CommandError('UNSUPPORTED', f'asset does not expose {capability}')
    ttl = number(payload.get('ttl_ms', 1000), 'ttl_ms', 1, 30000)
    deadline_ms = timestamp + ttl
    if abs(now_ms - timestamp) > maximum_clock_skew_ms or now_ms >= deadline_ms:
        raise CommandError('EXPIRED', 'command expired or sender clock exceeds allowed skew')
    # Future timestamps must not grant a control command extra validity.
    if timestamp > now_ms + 100:
        raise CommandError('EXPIRED', 'command timestamp is more than 100 ms in the future')
    arguments = {}
    allowed = {'command', 'ttl_ms'}
    if name.startswith('control.'):
        allowed.update(('owner', 'token', 'duration_s'))
        arguments['owner'] = nonempty(payload.get('owner'), 'owner')
        arguments['token'] = payload.get('token', '')
        if name != 'control.acquire':
            arguments['token'] = nonempty(arguments['token'], 'token')
        elif arguments['token'] != '':
            raise CommandError('INVALID_ARGUMENT', 'acquire must omit token')
        arguments['duration_s'] = number(payload.get('duration_s', 5.0), 'duration_s', 0.001, 3600)
    elif name != 'system.describe':
        allowed.add('lease_token')
        arguments['lease_token'] = nonempty(payload.get('lease_token'), 'lease_token')
    if name == 'motion.velocity':
        allowed.update(('velocity', 'valid_for_s'))
        arguments['valid_for_s'] = number(payload.get('valid_for_s'), 'valid_for_s', 0.001, 30)
        deadline_ms = min(deadline_ms, timestamp + arguments['valid_for_s'] * 1000)
        if deadline_ms <= now_ms:
            raise CommandError('EXPIRED', 'velocity command validity elapsed during transport')
        velocity = fields(payload.get('velocity'), ('linear', 'angular'), 'velocity')
        arguments['velocity'] = {
            axis: vector(velocity.get(axis, {}), ('x', 'y', 'z'), f'velocity.{axis}')
            for axis in ('linear', 'angular')
        }
    elif name == 'navigation.to_pose':
        allowed.add('target')
        target = fields(payload.get('target'), ('frame_id', 'position', 'orientation'), 'target')
        arguments['target'] = {
            'frame_id': nonempty(target.get('frame_id'), 'target.frame_id'),
            'position': vector(target.get('position'), ('x', 'y', 'z'), 'target.position'),
            'orientation': vector(target.get('orientation', {}), ('x', 'y', 'z', 'w'),
                                  'target.orientation', {'w': 1.0}),
        }
        norm = math.hypot(*arguments['target']['orientation'].values())
        if not math.isfinite(norm) or not math.sqrt(0.99) <= norm <= math.sqrt(1.01):
            raise CommandError('INVALID_ARGUMENT', 'orientation must be a unit quaternion')
    elif name in ('flight.takeoff', 'flight.land'):
        allowed.add('altitude_m')
        arguments['altitude_m'] = number(payload.get('altitude_m', 0.0), 'altitude_m', 0, 10000)
    fields(payload, allowed, 'payload')
    return Command(request_id, name, deadline_ms, arguments)


def recover_identity(data):
    """Recover only a valid bridge identity for a correlated validation error."""
    try:
        if len(data.encode('utf-8')) > 16384:
            return None
        value = json.loads(data)
        bridge = value['bridge']
        request_id = bridge['request_id']
        if bridge['source'] != 'mqtt' or not re.fullmatch(r'[A-Za-z0-9_-]{8,64}', request_id):
            return None
        payload = value.get('payload', {})
        name = payload.get('command', '') if isinstance(payload, dict) else ''
        return request_id, name if isinstance(name, str) else ''
    except (KeyError, TypeError, ValueError, AttributeError, RecursionError):
        return None


def response(request_id, command, *, success, code, message, phase='result', **extra):
    return {
        'source': 'mqtt', 'request_id': request_id, 'command': command,
        'response': {'ok': success, 'code': code, 'message': message, 'phase': phase,
                     'terminal': phase != 'accepted', **extra},
    }


@dataclass
class Pending:
    command: Command
    expires_at: float
    accepted: bool = False
    reply: dict | None = None
    goal_handle: object = None
    future: object = None


class Correlations:
    """Bounded active requests and completed replay records; monotonic timeouts."""

    def __init__(self, max_pending=128, max_completed=512):
        self.max_pending = max_pending
        self.max_completed = max_completed
        self.pending = {}
        self.completed = OrderedDict()

    def start(self, command, now, timeout_s):
        if command.request_id in self.pending or command.request_id in self.completed:
            raise CommandError('DUPLICATE', 'request already tracked')
        if len(self.pending) >= self.max_pending:
            raise CommandError('BUSY', 'maximum pending requests reached')
        self.pending[command.request_id] = Pending(command, now + timeout_s)

    def finish(self, request_id, reply, event=None):
        pending = self.pending.pop(request_id, None)
        self.completed[request_id] = (reply, event)
        while len(self.completed) > self.max_completed:
            self.completed.popitem(last=False)
        # Cancel local observation, not robot execution. rclpy's done callbacks
        # release request bookkeeping, keeping lost replies from accumulating.
        if pending and pending.future and not pending.future.done():
            pending.future.cancel()

    def expired(self, now):
        return [key for key, pending in self.pending.items() if now >= pending.expires_at]
