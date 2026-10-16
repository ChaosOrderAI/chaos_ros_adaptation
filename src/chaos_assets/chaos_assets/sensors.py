"""Resolve sensor declarations without importing ROS or opening hardware."""

from copy import deepcopy
import math
import re

from .inventory import ConfigurationError, _merge, _record, _resolve_path, _text


_IDENTIFIER = re.compile(r'^[A-Za-z][A-Za-z0-9_]*$')
_PACKAGE = re.compile(r'^[a-z][a-z0-9_]*$')
_EXECUTABLE = re.compile(r'^[A-Za-z0-9_][A-Za-z0-9_.-]*$')
_PARAMETER = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$')
_ROS_NAME = re.compile(r'^/?[A-Za-z_][A-Za-z0-9_]*(?:/[A-Za-z_][A-Za-z0-9_]*)*$')
_FRAME = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*(?:/[A-Za-z_][A-Za-z0-9_]*)*$')
_SENSOR_FIELDS = {'kind', 'enabled', 'frame_id', 'driver', 'streams', 'transform'}
_DRIVER_FIELDS = {'mode', 'package', 'executable', 'parameters', 'parameter_files', 'remappings'}
_STREAM_FIELDS = {'type', 'topic', 'frame_id', 'qos', 'rate_hz', 'timeout_s', 'monitor'}
_MESSAGE_TYPES = {
    'sensor_msgs/msg/Image', 'sensor_msgs/msg/CameraInfo',
    'sensor_msgs/msg/PointCloud2', 'sensor_msgs/msg/LaserScan', 'sensor_msgs/msg/Imu',
}
_PRIMARY_TYPES = {
    'camera': {'sensor_msgs/msg/Image', 'sensor_msgs/msg/CameraInfo', 'sensor_msgs/msg/PointCloud2'},
    'lidar': {'sensor_msgs/msg/PointCloud2', 'sensor_msgs/msg/LaserScan'},
    'imu': {'sensor_msgs/msg/Imu'},
}


def _positive(value, where, minimum, maximum):
    if (type(value) not in (int, float) or not minimum <= value <= maximum
            or not math.isfinite(value)):
        raise ConfigurationError(f'{where}: expected a positive finite number in [{minimum}, {maximum}]')
    return float(value)


def _topic(value, namespace, where):
    value = _text(value, where, _ROS_NAME)
    return value if value.startswith('/') else f'{namespace}/{value}'


def _parameter_value(value, where):
    def scalar(item):
        if type(item) is bool or isinstance(item, str):
            return
        if type(item) is int and -(2 ** 63) <= item < 2 ** 63:
            return
        if type(item) is float and math.isfinite(item):
            return
        raise ConfigurationError(f'{where}: expected a ROS scalar or homogeneous scalar array with finite numbers')

    if isinstance(value, list):
        if not value or any(type(item) is not type(value[0]) for item in value):
            raise ConfigurationError(f'{where}: expected a nonempty homogeneous ROS scalar array')
        for item in value:
            scalar(item)
    else:
        scalar(value)


def _declarations(value, base_path, where):
    """Resolve paths before model and instance dictionaries are merged."""
    result = deepcopy(_record(value, where))
    for sensor_id, declaration in result.items():
        _text(sensor_id, f'{where} sensor ID', _IDENTIFIER)
        prefix = f'{where}.{sensor_id}'
        _record(declaration, prefix, _SENSOR_FIELDS)
        if 'driver' in declaration:
            driver = _record(declaration['driver'], f'{prefix}.driver', _DRIVER_FIELDS)
            if 'parameter_files' in driver:
                paths = driver['parameter_files']
                if not isinstance(paths, list):
                    raise ConfigurationError(f'{prefix}.driver.parameter_files: expected a list')
                resolved = []
                for value in paths:
                    path = _resolve_path(value, base_path, f'{prefix}.driver.parameter_files')
                    if not path.is_file():
                        raise ConfigurationError(f'sensor parameter file does not exist: {path}')
                    resolved.append(str(path))
                driver['parameter_files'] = resolved
        if 'streams' in declaration:
            for stream_id, stream in _record(declaration['streams'], f'{prefix}.streams').items():
                _text(stream_id, f'{prefix} stream ID', _IDENTIFIER)
                _record(stream, f'{prefix}.streams.{stream_id}', _STREAM_FIELDS)
        if 'transform' in declaration:
            _record(declaration['transform'], f'{prefix}.transform', {'parent', 'xyz', 'rpy'})
    return result


def _driver(value, namespace, asset_mode, enabled, where):
    driver = deepcopy(_record(value, where, _DRIVER_FIELDS))
    mode = driver.get('mode')
    if mode not in ('mock', 'node', 'attach'):
        raise ConfigurationError(f'{where}.mode: expected mock, node or attach')
    if mode == 'attach' and set(driver) != {'mode'}:
        raise ConfigurationError(f'{where}: attach drivers accept only mode')
    for key, pattern in (('package', _PACKAGE), ('executable', _EXECUTABLE)):
        if mode == 'node' or key in driver:
            _text(driver.get(key), f'{where}.{key}', pattern)
    if 'parameters' in driver:
        for key, value in _record(driver['parameters'], f'{where}.parameters').items():
            _text(key, f'{where}.parameters key', _PARAMETER)
            _parameter_value(value, f'{where}.parameters.{key}')
    if 'remappings' in driver:
        mappings = _record(driver['remappings'], f'{where}.remappings')
        for source, target in mappings.items():
            _text(source, f'{where}.remappings source', _ROS_NAME)
            if source in ('__ns', '__node', '__name'):
                raise ConfigurationError(f'{where}.remappings: node and namespace remapping is not a topic mapping')
            mappings[source] = _topic(target, namespace, f'{where}.remappings.{source}')
    if enabled and asset_mode == 'real' and mode == 'mock':
        raise ConfigurationError(f'{where}: enabled sensors on a real asset cannot use a mock driver')
    driver['declared_mode'] = mode
    if asset_mode == 'mock':
        driver['mode'] = 'mock'
    return driver


def _vector(value, where):
    if not isinstance(value, list) or len(value) != 3:
        raise ConfigurationError(f'{where}: expected exactly three finite numbers')
    for item in value:
        try:
            valid = type(item) in (int, float) and math.isfinite(item)
        except OverflowError:
            valid = False
        if not valid:
            raise ConfigurationError(f'{where}: expected exactly three finite numbers')
    return [float(item) for item in value]


def resolve_sensors(model_sensors, instance_sensors, model_directory, inventory_directory, asset):
    """Validate and normalize declarations; mock assets always use mock sensors."""
    where = f'{asset["id"]}.sensors'
    model = _declarations(model_sensors, model_directory, f'{where} (model)')
    instance = _declarations(instance_sensors, inventory_directory, where)
    # An explicit backend change replaces its options. Otherwise a node ->
    # attach override would inherit an executable and become impossible to use.
    for sensor_id, declaration in instance.items():
        override_driver = declaration.get('driver', {})
        inherited_driver = model.get(sensor_id, {}).get('driver', {})
        if ('mode' in override_driver and inherited_driver
                and override_driver['mode'] != inherited_driver.get('mode')):
            model[sensor_id]['driver'] = {}
    result = _merge(model, instance)
    if len(result) > 64:
        raise ConfigurationError(f'{where}: expected at most 64 sensors per asset')
    for sensor_id, sensor in result.items():
        prefix = f'{where}.{sensor_id}'
        kind = sensor.get('kind')
        if not isinstance(kind, str) or kind not in _PRIMARY_TYPES:
            raise ConfigurationError(f'{prefix}.kind: expected camera, lidar or imu')
        enabled = sensor.get('enabled', True)
        if type(enabled) is not bool:
            raise ConfigurationError(f'{prefix}.enabled: expected boolean')
        namespace = f'{asset["namespace"]}/sensors/{sensor_id}'
        frame = _text(sensor.get('frame_id', f'{asset["id"]}/{sensor_id}_link'), f'{prefix}.frame_id', _FRAME)
        sensor.update(id=sensor_id, enabled=enabled, namespace=namespace, frame_id=frame)
        sensor['driver'] = _driver(sensor.get('driver'), namespace, asset['mode'], enabled, f'{prefix}.driver')
        streams = _record(sensor.get('streams'), f'{prefix}.streams')
        if not streams:
            raise ConfigurationError(f'{prefix}.streams: expected at least one stream')
        if len(streams) > 32:
            raise ConfigurationError(f'{prefix}.streams: expected at most 32 streams per sensor')
        for stream_id, stream in streams.items():
            stream_where = f'{prefix}.streams.{stream_id}'
            if not isinstance(stream.get('type'), str) or stream['type'] not in _MESSAGE_TYPES:
                raise ConfigurationError(f'{stream_where}.type: unsupported sensor message type')
            stream['topic'] = _topic(stream.get('topic'), namespace, f'{stream_where}.topic')
            stream['frame_id'] = _text(stream.get('frame_id', frame), f'{stream_where}.frame_id', _FRAME)
            qos = stream.get('qos', 'sensor_data')
            if qos not in ('sensor_data', 'reliable'):
                raise ConfigurationError(f'{stream_where}.qos: expected sensor_data or reliable')
            stream['qos'] = qos
            monitor = stream.get('monitor', True)
            if type(monitor) is not bool:
                raise ConfigurationError(f'{stream_where}.monitor: expected boolean')
            stream['monitor'] = monitor
            stream['rate_hz'] = _positive(stream.get('rate_hz', 10.0), f'{stream_where}.rate_hz', 0.01, 1000)
            stream['timeout_s'] = _positive(stream.get('timeout_s', 2.0), f'{stream_where}.timeout_s', 0.001, 300)
        if not {stream['type'] for stream in streams.values()} & _PRIMARY_TYPES[kind]:
            raise ConfigurationError(f'{prefix}: no primary stream for sensor kind {kind}')
        if 'transform' in sensor:
            transform = sensor['transform']
            parent = _text(transform.get('parent', asset['frames']['base']), f'{prefix}.transform.parent', _FRAME)
            if parent == frame:
                raise ConfigurationError(f'{prefix}.transform: parent and child frames must differ')
            sensor['transform'] = {
                'parent': parent,
                'xyz': _vector(transform.get('xyz'), f'{prefix}.transform.xyz'),
                'rpy': _vector(transform.get('rpy'), f'{prefix}.transform.rpy'),
            }
    return result


def validate_sensor_graph(assets):
    """Catch cross-asset collisions in each DDS domain before selecting a host."""
    frames = {}
    topics = {}
    transforms = {}
    for asset in assets:
        if asset['enabled']:
            # Robot localization owns these frames. A sensor must not claim an
            # odom/map child either: a base -> odom mounting transform could
            # conflict with an existing odom -> base localization transform.
            # Shared map/odom reservations across robots remain legal.
            for name, frame in asset['frames'].items():
                frames.setdefault((asset['ros_domain_id'], frame), f'{asset["id"]}.frames.{name}')
    for asset in assets:
        if not asset['enabled']:
            continue
        domain = asset['ros_domain_id']
        for sensor in asset['sensors'].values():
            if not sensor['enabled']:
                continue
            owner = f'{asset["id"]}.sensors.{sensor["id"]}'
            sensor_frames = {sensor['frame_id']} | {stream['frame_id'] for stream in sensor['streams'].values()}
            for frame in sensor_frames:
                key = (domain, frame)
                if key in frames:
                    raise ConfigurationError(f'duplicate sensor frame in ROS domain {domain}: {frame} ({frames[key]}, {owner})')
                frames[key] = owner
            if 'transform' in sensor:
                transforms[(domain, sensor['frame_id'])] = sensor['transform']['parent']
            managed = sensor['driver']['mode'] in ('mock', 'node')
            for stream_id, stream in sensor['streams'].items():
                key = (domain, stream['topic'])
                stream_owner = f'{owner}.streams.{stream_id}'
                previous = topics.get(key)
                if previous and previous['type'] != stream['type']:
                    raise ConfigurationError(f'inconsistent sensor topic types in ROS domain {domain}: {stream["topic"]}')
                if managed and previous and previous['publisher'] is not None:
                    raise ConfigurationError(f'duplicate sensor output topic in ROS domain {domain}: {stream["topic"]} ({previous["publisher"]}, {stream_owner})')
                if previous is None:
                    topics[key] = {'type': stream['type'], 'publisher': stream_owner if managed else None}
                elif managed:
                    previous['publisher'] = stream_owner
    for domain, child in transforms:
        seen = set()
        frame = child
        while (domain, frame) in transforms:
            if frame in seen:
                raise ConfigurationError(f'sensor transform cycle in ROS domain {domain}: {frame}')
            seen.add(frame)
            frame = transforms[(domain, frame)]
