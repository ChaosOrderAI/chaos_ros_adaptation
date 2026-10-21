"""Supervise hardware bridges that are part of this workspace.

The driver derivation is performed by the native planner
(``chaos_bringup._chaos_bringup_core``). This module only turns a native
process specification into a ROS action.
"""

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from ._chaos_bringup_core import driver_processes as _driver_processes


def managed_go2(asset):
    """Whether the asset is a real Go2 that needs the native driver backend."""
    return asset['mode'] == 'real' and asset['driver']['plugin'] == 'unitree_go2'


def preflight_drivers(plan):
    """Check the native driver backend before any process action is constructed.

    The availability of a backend is a property of the build of
    ``chaos_runtime``; the native planner receives it as an injected
    capability. Dry runs and mock builds never initialize a vendor DDS
    stack, so they do not need a backend.
    """
    from .planning import preflight as _preflight

    _preflight(plan, backend_available=_backend_available, check_files=True)


def _backend_available(plugin):
    """Ask the runtime whether it provides the native backend of a plugin.

    The answer comes from the capability report of the runtime, which is
    executed before any ROS node or hardware channel is opened. A runtime
    that cannot be reached denies the backend, so a real asset fails the
    preflight instead of starting without its driver.
    """
    from .planning import native_backend_available

    return bool(native_backend_available(plugin))


def _node(spec, environment):
    # The parameters keep the structure of the native specification: one
    # dictionary per entry, so a caller sees the same parameters as the
    # native planner derived them.
    parameters = []
    for entry in spec['parameters']:
        parameters.append({key: ParameterValue(value, value_type=str)
                           if isinstance(value, str) else value
                           for key, value in entry.items()})
    return Node(package=spec['package'], executable=spec['executable'], name=spec['name'],
                namespace=spec['namespace'], output='screen',
                arguments=list(spec['arguments']), parameters=parameters,
                remappings=[tuple(rule) for rule in spec['remappings']],
                additional_env=environment)


def driver_processes(asset, environment):
    """Return (label, process) pairs for the managed hardware bridge."""
    return [(spec['label'], _node(spec, environment))
            for spec in _driver_processes(asset)]
