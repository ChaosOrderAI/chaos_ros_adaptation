"""Launch independently supervised sensors from the resolved asset snapshot.

The sensor derivation, the summaries, the dependency check and the parameter
file handling are performed by the native planner
(``chaos_bringup._chaos_bringup_core``). This module only turns a native
process specification into a ROS action.
"""

import json

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError  # noqa: F401  (shared error type)

from ._chaos_bringup_core import (
    require_executable as _require_executable,
    sensor_processes as _sensor_processes,
    sensor_summary as _sensor_summary,
    snapshot_parameter_files as _snapshot_parameter_files,
    validate_parameter_file as _validate_parameter_file,
)


def require_executable(package, executable):
    """Resolve a ROS executable without importing or invoking a vendor driver."""
    return _require_executable(package, executable)


def enabled_sensors(asset):
    """The enabled sensors of an asset, in the order of the snapshot."""
    return [sensor for sensor in asset.get('sensors', {}).values() if sensor['enabled']]


def sensor_summary(asset):
    """The sensor summary of an asset.

    Driver parameters are omitted: device URLs can contain credentials.
    """
    return _sensor_summary(asset)


def preflight_sensors(plan):
    """Check the entire selection before any managed process can start."""
    from .planning import preflight as _preflight

    _preflight(plan, check_files=True)


def validate_parameter_file(content, node_name, source):
    """Accept ROS node/namespace selectors and finite, typed ROS parameters."""
    return _validate_parameter_file(content, node_name, source)


def snapshot_parameter_files(plan, root):
    """Copy explicit ROS parameter files before launch, with private permissions.

    This closes the gap between preflight and child startup; the driver reads
    the checked snapshot, even if a deployment replaces the original file.
    """
    return _snapshot_parameter_files(plan, root)


def _node(spec, environment):
    """Turn a native process specification into a ROS node action."""
    parameters = []
    for entry in spec['parameters']:
        if '__parameter_file' in entry:
            parameters.append(entry['__parameter_file'])
            continue
        # The parameters keep the structure of the native specification: one
        # dictionary per entry, so a caller sees the same parameters as the
        # native planner derived them.
        parameters.append({key: ParameterValue(value, value_type=str)
                           if isinstance(value, str) else value
                           for key, value in entry.items()})
    return Node(package=spec['package'], executable=spec['executable'], name=spec['name'],
                namespace=spec['namespace'], output='screen',
                arguments=list(spec['arguments']), parameters=parameters,
                remappings=[tuple(rule) for rule in spec['remappings']],
                additional_env=environment)


def sensor_processes(asset, environment, parameter_files=None):
    """Return (label, process) pairs; caller applies the shared exit policy."""
    if parameter_files is None:
        parameter_files = {}
    return [(spec['label'], _node(spec, environment))
            for spec in _sensor_processes(asset, parameter_files)]
