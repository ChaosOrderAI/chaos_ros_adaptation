"""Turn a native process specification into one ROS launch action.

The derivation of each process lives in the native planner. These modules only
differ in whether a parameter entry may be a snapshotted file, and in how long
a collector is allowed to flush after SIGTERM.
"""

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def node_action(spec, environment, *, parameter_files=False,
                sigterm_timeout=None, sigkill_timeout=None):
    """Build a Node from a native process specification.

    String parameters stay strings. When ``parameter_files`` is set, an entry
    that carries ``__parameter_file`` is passed through as a parameter file
    rather than wrapped as a mapping.
    """
    parameters = []
    for entry in spec['parameters']:
        if parameter_files and '__parameter_file' in entry:
            parameters.append(entry['__parameter_file'])
            continue
        parameters.append({key: ParameterValue(value, value_type=str)
                           if isinstance(value, str) else value
                           for key, value in entry.items()})
    extras = {}
    if sigterm_timeout is not None:
        extras['sigterm_timeout'] = sigterm_timeout
    if sigkill_timeout is not None:
        extras['sigkill_timeout'] = sigkill_timeout
    return Node(package=spec['package'], executable=spec['executable'], name=spec['name'],
                namespace=spec['namespace'], output='screen',
                arguments=list(spec['arguments']), parameters=parameters,
                remappings=[tuple(rule) for rule in spec['remappings']],
                additional_env=environment, **extras)
