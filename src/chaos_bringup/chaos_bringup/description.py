"""Launch robot state publishers from the validated, frozen URDF snapshot.

The description derivation, the summary and the dependency check are
performed by the native planner (``chaos_bringup._chaos_bringup_core``). This
module only turns a native process specification into a ROS action.
"""

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError  # noqa: F401  (shared error type)

from ._chaos_bringup_core import (
    component_asset as _component_asset,
    description_processes as _description_processes,
    description_summary as _description_summary,
    require_executable as _require_executable,
)


def require_executable(package, executable):
    """Resolve a ROS executable without importing or invoking a vendor driver."""
    return _require_executable(package, executable)


def enabled_description(asset):
    """The robot description of an asset, or None when it is disabled."""
    description = asset.get('description')
    return description if description and description.get('enabled', True) else None


def description_summary(asset):
    """Keep expanded XML out of dry-run logs and their potentially large output."""
    return _description_summary(asset)


def preflight_descriptions(plan):
    """Resolve the publisher once, before any process action is constructed."""
    from .planning import preflight as _preflight

    _preflight(plan, check_files=True)


def component_asset(asset):
    """Avoid copying megabytes of URDF into unrelated node parameter payloads.

    The full snapshot remains unchanged for robot_state_publisher and the
    private resolved-assets.json file. The other nodes retain description
    metadata.
    """
    return _component_asset(asset)


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


def description_processes(asset, environment):
    """Return supervised publishers; never read or expand the source file again."""
    return [(spec['label'], _node(spec, environment))
            for spec in _description_processes(asset)]
