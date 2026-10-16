import copy
import json

import pytest

from chaos_sensors.config import enabled_sensors, public_description, read_json, streams


def sample_asset():
    stream = {'type': 'sensor_msgs/msg/Image', 'topic': '/robot/camera/image',
              'frame_id': 'camera_optical_frame', 'rate_hz': 30.0,
              'timeout_s': 1.0, 'qos': 'sensor_data', 'monitor': False,
              'internal_path': '/must/not/leak'}
    return {'id': 'robot', 'namespace': '/robot', 'sensors': {
        'front': {'id': 'front', 'kind': 'camera', 'enabled': True,
                  'namespace': '/robot/camera', 'frame_id': 'camera_link',
                  'driver': {'mode': 'node', 'parameters': {'password': 'top_secret'},
                             'executable': '/must/not/leak'},
                  'streams': {'color': stream}},
        'disabled': {'enabled': False, 'streams': {}},
    }}


def test_sensor_directory_is_discoverable_without_parameters_or_other_secrets():
    result = public_description(sample_asset())
    assert result['asset_id'] == 'robot'
    assert list(result['sensors']) == ['front']
    sensor = result['sensors']['front']
    assert sensor['driver'] == {'mode': 'node'}
    assert sensor['streams']['color']['topic'] == '/robot/camera/image'
    assert sensor['streams']['color']['monitor'] is False
    encoded = json.dumps(result)
    assert 'top_secret' not in encoded
    assert '/must/not/leak' not in encoded


def test_public_directory_does_not_mutate_resolved_asset():
    asset = sample_asset()
    original = copy.deepcopy(asset)
    public_description(asset)
    assert asset == original


@pytest.mark.parametrize('key,value', [
    ('type', 'std_msgs/msg/String'), ('topic', 'relative'), ('frame_id', ''),
    ('rate_hz', 1001), ('rate_hz', True), ('rate_hz', float('nan')),
    ('rate_hz', 1e-300), ('rate_hz', 0.009999), ('rate_hz', 1000.000001),
    ('timeout_s', 0), ('timeout_s', 0.000999), ('timeout_s', 300.000001),
    ('timeout_s', float('inf')), ('qos', 'arbitrary'), ('monitor', 'false'),
])
def test_defensive_parameter_checks_reject_bad_streams(key, value):
    sensor = sample_asset()['sensors']['front']
    sensor['streams']['color'][key] = value
    with pytest.raises(ValueError):
        streams(sensor)


@pytest.mark.parametrize('rate_hz,timeout_s', [(0.01, 0.001), (1000.0, 300.0)])
def test_stream_numeric_boundaries_support_finite_ros_timer_periods(rate_hz, timeout_s):
    sensor = sample_asset()['sensors']['front']
    sensor['streams']['color'].update(rate_hz=rate_hz, timeout_s=timeout_s)
    assert len(streams(sensor)) == 1
    assert 0 < int(1_000_000_000 / rate_hz) < 2 ** 63


def test_sensor_and_stream_count_limits_are_enforced():
    asset = sample_asset()
    sensor = asset['sensors']['front']
    stream = sensor['streams']['color']
    sensor['streams'] = {f'stream_{index}': stream for index in range(32)}
    asset['sensors'] = {f'sensor_{index}': sensor for index in range(64)}
    assert len(enabled_sensors(asset)) == 64
    sensor['streams']['extra'] = stream
    with pytest.raises(ValueError, match='32'):
        enabled_sensors(asset)
    del sensor['streams']['extra']
    asset['sensors']['extra'] = sensor
    with pytest.raises(ValueError, match='64'):
        enabled_sensors(asset)


@pytest.mark.parametrize('value', ['[]', 'null', 'x', '[' * 1100, ' ' * 1_048_577])
def test_nonobject_invalid_deep_or_oversized_json_is_rejected(value):
    with pytest.raises(ValueError):
        read_json(value)


def test_disabled_sensors_are_not_validated_or_subscribed():
    asset = sample_asset()
    asset['sensors']['disabled']['streams'] = {'invalid': {}}
    assert [sensor_id for sensor_id, _ in enabled_sensors(asset)] == ['front']
