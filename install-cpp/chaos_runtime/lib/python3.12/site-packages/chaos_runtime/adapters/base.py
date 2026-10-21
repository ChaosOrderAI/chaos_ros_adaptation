from dataclasses import dataclass, field
from importlib.metadata import entry_points
import math
import re

from chaos_runtime.control import Rejected


def validate_absolute_topic(value, name):
    # Fully resolved topic subset: ordinary slash-separated ROS identifiers.
    # This excludes remap substitutions, repeated slashes and hidden wildcards.
    if not isinstance(value, str) or not re.fullmatch(r'(?:/[A-Za-z_][A-Za-z0-9_]*)+', value):
        raise ValueError(f'{name} must be a valid absolute ROS topic')


@dataclass
class Snapshot:
    connected: bool = False
    ready: bool = False
    pose_valid: bool = False
    position: list = field(default_factory=lambda: [0.0, 0.0, 0.0])
    quaternion: list = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])
    velocity: list = field(default_factory=lambda: [0.0] * 6)
    battery: float = math.nan
    detail: str = 'Waiting for telemetry'
    pose_frame: str = ''
    twist_frame: str = ''
    source_stamp: object = None


class Adapter:
    """Plugin contract. Runtime serializes all methods under its lock.

    ROS callbacks in a plugin must acquire node.adapter_lock as well. snapshot()
    must report stale data as invalid. stop() must stop only owned outputs.
    poll_operation() returns (done, success, code, message, progress).

    All methods run on the shared control path and must return promptly: never
    wait for device/network I/O, a ROS Future, or task completion under this lock.
    start_operation() dispatches work; poll_operation() reads cached progress.
    Put blocking vendor SDK calls in a separate driver process with bounded IPC
    and its own command watchdog. Python threads cannot cancel a blocked native
    SDK call or guarantee the runtime watchdog will execute on time.
    A successful stop() means stop output was issued, not physical confirmation.
    """
    supported_capabilities = frozenset({'state'})
    option_names = frozenset()

    @classmethod
    def capabilities_for(cls, asset):
        return cls.supported_capabilities

    @classmethod
    def validate_options(cls, asset):
        options = asset['driver'].get('options', {})
        unknown = set(options) - cls.option_names
        if unknown:
            raise ValueError(f'Unsupported {asset["driver"]["plugin"]} options: {sorted(unknown)}')

    def __init__(self, node, asset, limits):
        self.node, self.asset, self.limits = node, asset, limits

    def update(self, dt):
        pass

    def snapshot(self):
        return Snapshot()

    def validate_velocity(self, values):
        raise Rejected('UNSUPPORTED', 'Adapter does not support velocity')

    def set_velocity(self, values):
        raise Rejected('UNSUPPORTED', 'Adapter does not support velocity')

    def stop(self):
        pass

    def controlled_stop(self):
        self.stop()

    def start_operation(self, kind, target):
        raise Rejected('UNSUPPORTED', f'Adapter does not support {kind}')

    def poll_operation(self):
        return False, False, 'UNSUPPORTED', 'No active operation', 0.0

    def close(self):
        self.stop()


def adapter_class_for(plugin):
    candidates = [ep for ep in entry_points(group='chaos.robot_adapters') if ep.name == plugin]
    if len(candidates) != 1:
        raise ValueError(f'Expected one installed chaos.robot_adapters entry point for {plugin!r}; found {len(candidates)}')
    adapter_class = candidates[0].load()
    if not issubclass(adapter_class, Adapter):
        raise TypeError(f'{plugin} must subclass chaos_runtime.adapters.base.Adapter')
    return adapter_class


def validate_asset(asset):
    """Preflight a resolved asset without constructing nodes or opening hardware."""
    from chaos_runtime.control import Limits
    Limits.from_dict(asset.get('limits', {}))
    adapter_class = adapter_class_for(asset['driver']['plugin'])
    adapter_class.validate_options(asset)
    unsupported = set(asset['capabilities']) - adapter_class.capabilities_for(asset)
    if unsupported:
        raise ValueError(f'Adapter {asset["driver"]["plugin"]} does not support {sorted(unsupported)}')
    return adapter_class


def load_adapter(plugin, node, asset, limits):
    return validate_asset(asset)(node, asset, limits)
