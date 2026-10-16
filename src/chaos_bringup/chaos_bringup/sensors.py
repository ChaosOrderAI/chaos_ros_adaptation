"""Launch independently supervised sensors from the resolved asset snapshot."""

import json
import os
from pathlib import Path
import re

import yaml

from ament_index_python.packages import get_package_prefix
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError
from chaos_assets.inventory import _UniqueLoader
from chaos_assets.sensors import _parameter_value


def enabled_sensors(asset):
    return [sensor for sensor in asset.get('sensors', {}).values() if sensor['enabled']]


def sensor_summary(asset):
    # Do not print driver parameters: device URLs can contain credentials.
    return [{'id': sensor['id'], 'kind': sensor['kind'],
             'mode': sensor['driver']['mode'], 'namespace': sensor['namespace'],
             'streams': {name: {'topic': stream['topic'], 'type': stream['type']}
                         for name, stream in sensor['streams'].items()}}
            for sensor in enabled_sensors(asset)]


def require_executable(package, executable):
    """Resolve a ROS executable without importing or invoking a vendor driver."""
    try:
        prefix = Path(get_package_prefix(package))
    except LookupError as error:
        raise ConfigurationError(
            f'sensor dependency {package!r} is unavailable; build and source its ROS workspace') from error
    path = prefix / 'lib' / package / executable
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ConfigurationError(f'sensor executable is unavailable: {package}/{executable}')


def preflight_sensors(plan):
    """Check the entire selection before any managed process can start."""
    dependencies = set()
    for item in plan:
        sensors = enabled_sensors(item['asset'])
        if sensors:
            dependencies.add(('chaos_sensors', 'sensor_monitor'))
        for sensor in sensors:
            driver = sensor['driver']
            if driver['mode'] == 'mock':
                dependencies.add(('chaos_sensors', 'mock_sensor'))
            elif driver['mode'] == 'node':
                dependencies.add((driver['package'], driver['executable']))
            if sensor.get('transform'):
                dependencies.add(('tf2_ros', 'static_transform_publisher'))
    for package, executable in sorted(dependencies):
        require_executable(package, executable)


def validate_parameter_file(content, node_name, source):
    """Accept ROS node/namespace selectors and finite, typed ROS parameters.

    Require a matching selector to avoid silently ignoring a vendor file that
    was written for another node name. The managed sensor node is named driver.
    """
    try:
        parsed = yaml.load(content, Loader=_UniqueLoader)
    except (yaml.YAMLError, UnicodeError, RecursionError) as error:
        raise ConfigurationError(f'invalid sensor parameter YAML: {source}') from error
    if not isinstance(parsed, dict) or not parsed:
        raise ConfigurationError(f'sensor parameter YAML must be a nonempty ROS mapping: {source}')

    def matches(pattern, parts):
        if not pattern:
            return not parts
        if pattern[0] == '**':
            return matches(pattern[1:], parts) or (bool(parts) and matches(pattern, parts[1:]))
        return (bool(parts) and (pattern[0] == '*' or pattern[0] == parts[0])
                and matches(pattern[1:], parts[1:]))

    def parameters(values):
        if not isinstance(values, dict):
            raise ConfigurationError(f'sensor parameter YAML ros__parameters must be a mapping: {source}')
        for key, value in values.items():
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*', key):
                raise ConfigurationError(f'invalid sensor parameter YAML parameter name: {key}')
            if isinstance(value, dict):
                parameters(value)
            else:
                _parameter_value(value, f'sensor parameter YAML {source}: {key}')

    found = []

    def visit(tree, path):
        if not isinstance(tree, dict):
            raise ConfigurationError(f'sensor parameter YAML requires ros__parameters under a node selector: {source}')
        for key, value in tree.items():
            if key == 'ros__parameters':
                if not path:
                    raise ConfigurationError(f'sensor parameter YAML requires a node selector: {source}')
                parameters(value)
                found.append(matches(path, node_name.strip('/').split('/')))
            else:
                parts = key.strip('/').split('/')
                if ('//' in key or key.endswith('/') or not all(
                        part in ('*', '**') or re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', part)
                        for part in parts)):
                    raise ConfigurationError(f'invalid sensor parameter YAML node selector: {key}')
                visit(value, path + parts)

    try:
        visit(parsed, [])
    except RecursionError as error:
        raise ConfigurationError(f'sensor parameter YAML nesting is too deep: {source}') from error
    if not any(found):
        raise ConfigurationError(f'sensor parameter YAML has no selector matching {node_name}: {source}; use /** or the full node name')


def snapshot_parameter_files(plan, root):
    """Copy explicit ROS parameter files before launch, with private permissions.

    This closes the gap between preflight and child startup; the driver reads
    the checked snapshot, even if a deployment replaces the original file.
    """
    snapshots = {}
    for item in plan:
        asset = item['asset']
        for sensor in enabled_sensors(asset):
            driver = sensor['driver']
            if driver['mode'] != 'node':
                continue
            paths = []
            for index, source in enumerate(driver.get('parameter_files', [])):
                try:
                    with Path(source).open('rb') as stream:
                        content = stream.read(1024 * 1024 + 1)
                    if len(content) > 1024 * 1024:
                        raise ConfigurationError(f'sensor parameter file exceeds 1 MiB: {source}')
                except OSError as error:
                    raise ConfigurationError(f'cannot read sensor parameter file: {source}') from error
                validate_parameter_file(content, sensor['namespace'] + '/driver', source)
                target = root / f'{asset["id"]}-{sensor["id"]}-{index}.yaml'
                target.write_bytes(content)
                target.chmod(0o600)
                paths.append(str(target))
            snapshots[(asset['id'], sensor['id'])] = paths
    return snapshots


def sensor_processes(asset, environment, parameter_files):
    """Return (label, process) pairs; caller applies the shared exit policy."""
    children = []
    for sensor in enabled_sensors(asset):
        driver = sensor['driver']
        node = None
        if driver['mode'] == 'mock':
            node = Node(package='chaos_sensors', executable='mock_sensor', name='driver',
                        namespace=sensor['namespace'], output='screen', additional_env=environment,
                        parameters=[{'sensor_json': ParameterValue(json.dumps(sensor), value_type=str)}])
        elif driver['mode'] == 'node':
            # Explicit types keep serial numbers such as "00123" as strings.
            parameters = {key: ParameterValue(value, value_type=str) if isinstance(value, str) else value
                          for key, value in driver.get('parameters', {}).items()}
            node = Node(package=driver['package'], executable=driver['executable'], name='driver',
                        namespace=sensor['namespace'], output='screen', additional_env=environment,
                        parameters=[*parameter_files.get((asset['id'], sensor['id']), []), parameters],
                        remappings=list(driver.get('remappings', {}).items()))
        if node is not None:
            children.append((f'Sensor {asset["id"]}/{sensor["id"]}', node))
        transform = sensor.get('transform')
        if transform:
            arguments = []
            for key, value in zip(('x', 'y', 'z', 'roll', 'pitch', 'yaw'),
                                  [*transform['xyz'], *transform['rpy']]):
                arguments.extend([f'--{key}', str(value)])
            arguments.extend(['--frame-id', transform['parent'], '--child-frame-id', sensor['frame_id']])
            children.append((f'Sensor transform {asset["id"]}/{sensor["id"]}',
                             Node(package='tf2_ros', executable='static_transform_publisher',
                                  name=f'{sensor["id"]}_static_tf', namespace=asset['namespace'],
                                  arguments=arguments, output='screen', additional_env=environment)))
    if enabled_sensors(asset):
        children.append((f'Sensor monitor {asset["id"]}',
                         Node(package='chaos_sensors', executable='sensor_monitor', name='sensor_monitor',
                              namespace=asset['namespace'], output='screen', additional_env=environment,
                              parameters=[{'asset_json': ParameterValue(json.dumps(asset), value_type=str)}])))
    return children
