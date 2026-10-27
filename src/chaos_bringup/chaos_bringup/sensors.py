"""Launch independently supervised sensors from the resolved asset snapshot.

The sensor derivation, the summaries, the dependency check and the parameter
file handling are performed by the native planner
(``chaos_bringup._chaos_bringup_core``). This module only turns a native
process specification into a ROS action.
"""

from ._chaos_bringup_core import (
    sensor_processes as _sensor_processes,
    snapshot_parameter_files as _snapshot_parameter_files,
    validate_parameter_file as _validate_parameter_file,
)


def validate_parameter_file(content, node_name, source):
    """Accept ROS node/namespace selectors and finite, typed ROS parameters."""
    return _validate_parameter_file(content, node_name, source)


def snapshot_parameter_files(plan, root):
    """Copy explicit ROS parameter files before launch, with private permissions.

    This closes the gap between preflight and child startup; the driver reads
    the checked snapshot, even if a deployment replaces the original file.
    """
    return _snapshot_parameter_files(plan, root)


def sensor_processes(asset, environment, parameter_files=None):
    """Return (label, process) pairs; caller applies the shared exit policy."""
    from .nodes import node_action

    if parameter_files is None:
        parameter_files = {}
    return [(spec['label'], node_action(spec, environment, parameter_files=True))
            for spec in _sensor_processes(asset, parameter_files)]
