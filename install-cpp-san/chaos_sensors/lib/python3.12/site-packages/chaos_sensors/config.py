"""Small defensive checks for node parameters and a public sensor directory."""

import json
import math


STREAM_TYPES = (
    'sensor_msgs/msg/Image', 'sensor_msgs/msg/CameraInfo',
    'sensor_msgs/msg/PointCloud2', 'sensor_msgs/msg/LaserScan', 'sensor_msgs/msg/Imu',
)


def read_json(value: str) -> dict:
    if not isinstance(value, str) or len(value) > 1_048_576:
        raise ValueError('sensor configuration must be a JSON string of at most 1 MiB')
    try:
        result = json.loads(value)
    except (ValueError, RecursionError) as error:
        raise ValueError('sensor configuration is not valid JSON') from error
    if not isinstance(result, dict):
        raise ValueError('sensor configuration must be an object')
    return result


def streams(sensor: dict) -> list[tuple[str, dict]]:
    value = sensor.get('streams', {})
    if not isinstance(value, dict) or len(value) > 32:
        raise ValueError('sensor streams must be an object with at most 32 entries')
    entries = list(value.items())
    for stream_id, stream in entries:
        if not isinstance(stream_id, str) or not isinstance(stream, dict):
            raise ValueError('invalid stream entry')
        if stream.get('type') not in STREAM_TYPES:
            raise ValueError(f'unsupported message type for stream {stream_id}')
        topic = stream.get('topic')
        if not isinstance(topic, str) or not topic.startswith('/'):
            raise ValueError(f'stream {stream_id} requires an absolute topic')
        if not isinstance(stream.get('frame_id'), str) or not stream['frame_id']:
            raise ValueError(f'stream {stream_id} requires a frame_id')
        if stream.get('qos', 'sensor_data') not in ('sensor_data', 'reliable'):
            raise ValueError(f'invalid QoS for stream {stream_id}')
        if not isinstance(stream.get('monitor', True), bool):
            raise ValueError(f'invalid monitor flag for stream {stream_id}')
        for key, default, minimum, maximum in [('rate_hz', 10.0, 0.01, 1000.0),
                                                ('timeout_s', 2.0, 0.001, 300.0)]:
            numeric = stream.get(key, default)
            if (isinstance(numeric, bool) or not isinstance(numeric, (int, float)) or
                    not math.isfinite(numeric) or not minimum <= numeric <= maximum):
                raise ValueError(f'invalid {key} for stream {stream_id}')
    return entries


def enabled_sensors(asset: dict) -> list[tuple[str, dict]]:
    sensors = asset.get('sensors', {})
    if not isinstance(sensors, dict) or len(sensors) > 64:
        raise ValueError('sensors must be an object with at most 64 entries')
    result = []
    for sensor_id, sensor in sensors.items():
        if not isinstance(sensor, dict):
            raise ValueError('sensor entry must be an object')
        if sensor.get('enabled', True):
            streams(sensor)
            result.append((sensor_id, sensor))
    return result


def public_description(asset: dict) -> dict:
    """Publish only an allowlist; driver parameters may contain credentials."""
    catalog = {}
    for sensor_id, sensor in enabled_sensors(asset):
        entry = {key: sensor.get(key, '') for key in ('id', 'kind', 'namespace', 'frame_id')}
        entry['id'] = sensor_id
        entry['driver'] = {'mode': sensor.get('driver', {}).get('mode', '')}
        entry['streams'] = {
            stream_id: {key: stream.get(key) for key in (
                'type', 'topic', 'frame_id', 'qos', 'rate_hz', 'timeout_s')}
            for stream_id, stream in streams(sensor)
        }
        for stream_id, stream in streams(sensor):
            entry['streams'][stream_id]['monitor'] = stream.get('monitor', True)
        catalog[sensor_id] = entry
    return {'schema_version': 1, 'asset_id': asset['id'], 'sensors': catalog}
