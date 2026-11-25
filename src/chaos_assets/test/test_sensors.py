from copy import deepcopy
import os
from pathlib import Path

import pytest
import yaml

import ament_index_python.packages

from chaos_assets import ConfigurationError, load_inventory


ROOT = Path(__file__).resolve().parents[1]


def imu(mode='mock', topic='data'):
    return {
        'kind': 'imu', 'driver': {'mode': mode},
        'streams': {'imu': {'type': 'sensor_msgs/msg/Imu', 'topic': topic}},
    }


def write_assets(tmp_path, sensors=None, *, mode='mock', model_sensors=None, mutate=None):
    model_dir = tmp_path / 'models'
    model_dir.mkdir(exist_ok=True)
    model_path = model_dir / 'model.yaml'
    model = yaml.safe_load((ROOT / 'models/lingao_vehicle/v1/model.yaml').read_text())
    if model_sensors is not None:
        model['sensors'] = model_sensors
    model_path.write_text(yaml.safe_dump(model))
    data = {
        'schema_version': 1, 'hosts': {'dev': {'ros_domain_id': 87}},
        'assets': [{'id': 'car', 'model_file': 'models/model.yaml', 'host': 'dev', 'mode': mode}],
    }
    if sensors is not None:
        data['assets'][0]['sensors'] = sensors
    if mutate:
        mutate(data)
    path = tmp_path / 'inventory.yaml'
    path.write_text(yaml.safe_dump(data))
    return path


def test_existing_assets_have_no_sensors():
    assert all(asset['sensors'] == {} for asset in load_inventory(ROOT / 'config/demo.yaml').assets)


def test_defaults_and_mock_override_validated_real_node(tmp_path):
    sensor = imu('node')
    sensor['driver'].update(package='vendor_driver', executable='imu_node', parameters={'serial_port': '/dev/ttyUSB0'})
    sensor['driver']['remappings'] = {'imu/data': 'data', '/vendor/status': '/health/imu'}
    asset = load_inventory(write_assets(tmp_path, {'body_imu': sensor})).assets[0]
    result = asset['sensors']['body_imu']
    assert result['id'] == 'body_imu'
    assert result['namespace'] == '/robots/car/sensors/body_imu'
    assert result['frame_id'] == 'car/body_imu_link'
    assert result['driver']['mode'] == 'mock'
    assert result['driver']['declared_mode'] == 'node'
    assert result['driver']['remappings'] == {
        'imu/data': '/robots/car/sensors/body_imu/data', '/vendor/status': '/health/imu',
    }
    assert result['streams']['imu'] == {
        'type': 'sensor_msgs/msg/Imu', 'topic': '/robots/car/sensors/body_imu/data',
        'frame_id': 'car/body_imu_link', 'qos': 'sensor_data', 'rate_hz': 10.0, 'timeout_s': 2.0, 'monitor': True,
    }
    assert 'transform' not in result


def test_model_instance_merge_paths_are_relative_to_declaration(tmp_path):
    (tmp_path / 'models').mkdir()
    (tmp_path / 'models/model_params.yaml').write_text('/**:\n  ros__parameters: {}\n')
    (tmp_path / 'instance_params.yaml').write_text('/**:\n  ros__parameters: {}\n')
    first = imu('node')
    first['driver'].update(package='vendor', executable='imu_node',
                           parameter_files=['model_params.yaml'], parameters={'baud': 115200, 'enabled': True})
    second = deepcopy(first)
    path = write_assets(tmp_path, {'first': {
        'driver': {'parameter_files': ['instance_params.yaml'], 'parameters': {'baud': 9600}},
        'streams': {'imu': {'rate_hz': 50}},
    }}, model_sensors={'first': first, 'second': second})
    sensors = load_inventory(path).assets[0]['sensors']
    assert sensors['first']['driver']['parameters'] == {'baud': 9600, 'enabled': True}
    assert sensors['first']['driver']['parameter_files'] == [str(tmp_path / 'instance_params.yaml')]
    assert sensors['second']['driver']['parameter_files'] == [str(tmp_path / 'models/model_params.yaml')]
    assert sensors['first']['streams']['imu']['rate_hz'] == 50.0
    assert sensors['first']['streams']['imu']['topic'].endswith('/first/data')


def test_sensor_parameter_files_accept_installed_package_uri(tmp_path):
    # A parameter file of an installed package is resolved through the
    # package index of the sourced ROS installation.
    share = Path(ament_index_python.packages.get_package_share_directory('chaos_assets'))
    sensor = imu('node')
    sensor['driver'].update(package='chaos_sensors', executable='imu_node',
                            parameter_files=['package://chaos_assets/config/demo.yaml'])
    result = load_inventory(write_assets(tmp_path, {'imu': sensor})).assets[0]['sensors']['imu']
    assert result['driver']['parameter_files'] == [
        os.path.realpath(share / 'config/demo.yaml')]


@pytest.mark.parametrize('mutate,match', [
    (lambda s: s.update(typo=True), 'unknown fields'),
    (lambda s: s.update(kind='gps'), 'kind'),
    (lambda s: s.update(kind=[]), 'kind'),
    (lambda s: s.update(enabled='true'), 'boolean'),
    (lambda s: s.update(frame_id='/imu'), 'frame_id'),
    (lambda s: s.update(frame_id='car//imu'), 'frame_id'),
    (lambda s: s.update(driver={}), 'mode'),
    (lambda s: s.update(driver={'mode': 'shell'}), 'mode'),
    (lambda s: s.update(driver={'mode': 'node', 'package': 'driver', 'executable': '../node'}), 'executable'),
    (lambda s: s.update(driver={'mode': 'node', 'package': 'driver', 'executable': 'node --flag'}), 'executable'),
    (lambda s: s.update(driver={'mode': 'node', 'package': 'bad-package', 'executable': 'node'}), 'package'),
    (lambda s: s.update(driver={'mode': 'node', 'package': 'driver'}), 'executable'),
    (lambda s: s.update(driver={'mode': 'attach', 'parameters': {}}), 'attach'),
    (lambda s: s['driver'].update(parameters={'value': float('nan')}), 'finite'),
    (lambda s: s['driver'].update(parameters={'value': float('inf')}), 'finite'),
    (lambda s: s['driver'].update(parameters={'value': 2 ** 63}), 'ROS scalar|64-bit range'),
    (lambda s: s['driver'].update(parameters={'value': [1, 1.0]}), 'homogeneous'),
    (lambda s: s['driver'].update(parameters={'value': []}), 'homogeneous'),
    (lambda s: s['driver'].update(parameters={'value': {'nested': 1}}), 'ROS scalar'),
    (lambda s: s['driver'].update(parameters={'bad/key': 1}), 'parameters key'),
    (lambda s: s['driver'].update(parameter_files='params.yaml'), 'expected a list'),
    (lambda s: s['driver'].update(parameter_files=['missing.yaml']), 'does not exist'),
    (lambda s: s['driver'].update(remappings={'x': '../data'}), 'remappings'),
    (lambda s: s['driver'].update(remappings={'__ns': '/other'}), 'namespace remapping'),
    (lambda s: s['driver'].update(remappings={'__node': 'other'}), 'namespace remapping'),
    (lambda s: s['driver'].update(remappings={'__name': 'other'}), 'namespace remapping'),
    (lambda s: s.update(streams={}), 'at least one stream'),
    (lambda s: s['streams']['imu'].update(type='std_msgs/msg/String'), 'unsupported'),
    (lambda s: s['streams']['imu'].update(type=[]), 'unsupported'),
    (lambda s: s['streams']['imu'].update(type='sensor_msgs/msg/Image'), 'primary stream'),
    (lambda s: s['streams']['imu'].update(topic='~data'), 'topic'),
    (lambda s: s['streams']['imu'].update(topic='/data/'), 'topic'),
    (lambda s: s['streams']['imu'].update(qos='best_effort'), 'qos'),
    (lambda s: s['streams']['imu'].update(monitor='false'), 'monitor'),
    (lambda s: s['streams']['imu'].update(rate_hz=0), 'positive finite'),
    (lambda s: s['streams']['imu'].update(rate_hz=1e-300), '0.01'),
    (lambda s: s['streams']['imu'].update(rate_hz=1001), 'positive finite'),
    (lambda s: s['streams']['imu'].update(timeout_s=float('nan')), 'positive finite'),
    (lambda s: s['streams']['imu'].update(timeout_s=True), 'positive finite'),
    (lambda s: s['streams']['imu'].update(timeout_s=1e-300), '0.001'),
    (lambda s: s.update(transform={'parent': 'car/imu_link', 'xyz': [0, 0, 0], 'rpy': [0, 0, 0]}), 'must differ'),
    (lambda s: s.update(transform={'xyz': [0, 0], 'rpy': [0, 0, 0]}), 'three finite'),
    (lambda s: s.update(transform={'xyz': [0, 0, 0], 'rpy': [0, 0, float('inf')]}), 'three finite'),
])
def test_reject_invalid_sensor_before_mock_override(tmp_path, mutate, match):
    sensor = imu()
    mutate(sensor)
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_assets(tmp_path, {'imu': sensor}))


def test_real_asset_forbids_enabled_mock_sensor(tmp_path):
    with pytest.raises(ConfigurationError, match='real asset cannot use a mock'):
        load_inventory(write_assets(tmp_path, {'imu': imu()}, mode='real'))
    sensor = imu()
    sensor['enabled'] = False
    assert not load_inventory(write_assets(tmp_path, {'imu': sensor}, mode='real')).assets[0]['sensors']['imu']['enabled']


def test_disabled_sensor_still_validates_declared_driver(tmp_path):
    sensor = imu('node')
    sensor['enabled'] = False
    with pytest.raises(ConfigurationError, match='package'):
        load_inventory(write_assets(tmp_path, {'imu': sensor}))


def test_attach_preserved_in_real_and_overridden_in_mock(tmp_path):
    path = write_assets(tmp_path, {'imu': imu('attach', '/chassis/imu')}, mode='real')
    real = load_inventory(path).assets[0]
    mock = load_inventory(path, mode='mock').assets[0]
    assert real['sensors']['imu']['driver'] == {'mode': 'attach', 'declared_mode': 'attach'}
    assert mock['sensors']['imu']['driver']['mode'] == 'mock'
    assert mock['config_digest'] != real['config_digest']


def test_switching_driver_mode_replaces_inherited_backend_options(tmp_path):
    sensor = imu('node')
    sensor['driver'].update(package='vendor', executable='imu_node', parameters={'baud': 115200})
    path = write_assets(tmp_path, {'imu': {'driver': {'mode': 'attach'}}}, mode='real', model_sensors={'imu': sensor})
    result = load_inventory(path).assets[0]['sensors']['imu']
    assert result['driver'] == {'mode': 'attach', 'declared_mode': 'attach'}
    assert result['streams']['imu']['type'] == 'sensor_msgs/msg/Imu'


def test_switching_to_node_requires_new_executable(tmp_path):
    sensor = imu('attach')
    path = write_assets(tmp_path, {'imu': {'driver': {'mode': 'node'}}}, mode='real', model_sensors={'imu': sensor})
    with pytest.raises(ConfigurationError, match='package'):
        load_inventory(path)


def test_recursive_yaml_alias_reports_configuration_error(tmp_path):
    path = tmp_path / 'inventory.yaml'
    path.write_text('schema_version: 1\nhosts: &recursive\n  dev: *recursive\n')
    with pytest.raises(ConfigurationError):
        load_inventory(path)


def test_excessively_nested_yaml_reports_configuration_error(tmp_path):
    path = tmp_path / 'inventory.yaml'
    path.write_text('value: ' + '[' * 1500 + '0' + ']' * 1500)
    with pytest.raises(ConfigurationError):
        load_inventory(path)


def test_multi_stream_sensor_shares_frames_and_accepts_lidar_imu(tmp_path):
    sensor = {'kind': 'lidar', 'driver': {'mode': 'mock'}, 'streams': {
        'points': {'type': 'sensor_msgs/msg/PointCloud2', 'topic': 'points'},
        'imu': {'type': 'sensor_msgs/msg/Imu', 'topic': 'imu'},
    }}
    result = load_inventory(write_assets(tmp_path, {'lidar': sensor})).assets[0]['sensors']['lidar']
    assert {stream['frame_id'] for stream in result['streams'].values()} == {'car/lidar_link'}


def test_transforms_use_explicit_mount_measurements(tmp_path):
    sensor = imu()
    sensor['transform'] = {'xyz': [0.1, 0, 0.2], 'rpy': [0, 0, 0]}
    result = load_inventory(write_assets(tmp_path, {'imu': sensor})).assets[0]['sensors']['imu']
    assert result['transform']['parent'] == 'car/base_link'
    assert result['transform']['xyz'] == [0.1, 0.0, 0.2]


@pytest.mark.parametrize('collision', ['base', 'sensor', 'stream'])
def test_frame_collisions_across_sensors_or_base_fail(tmp_path, collision):
    first, second = imu(), imu()
    if collision == 'base':
        first['frame_id'] = 'car/base_link'
    elif collision == 'sensor':
        second['frame_id'] = 'car/first_link'
    else:
        second['streams']['imu']['frame_id'] = 'car/first_link'
    with pytest.raises(ConfigurationError, match='duplicate sensor frame'):
        load_inventory(write_assets(tmp_path, {'first': first, 'second': second}))


@pytest.mark.parametrize('robot_frame', ['base_link', 'odom', 'map'])
@pytest.mark.parametrize('claim', ['sensor', 'stream'])
def test_sensor_cannot_claim_reserved_robot_localization_frames(tmp_path, robot_frame, claim):
    sensor = imu()
    if claim == 'sensor':
        sensor['frame_id'] = f'car/{robot_frame}'
    else:
        sensor['streams']['imu']['frame_id'] = f'car/{robot_frame}'
    sensor['transform'] = {'xyz': [0, 0, 0], 'rpy': [0, 0, 0]}
    # Sensor == base fails already at the direct self-edge check. All other
    # claims, including stream-only claims, fail the domain-wide reservation.
    with pytest.raises(ConfigurationError, match='duplicate sensor frame|parent and child frames must differ'):
        load_inventory(write_assets(tmp_path, {'imu': sensor}))


@pytest.mark.parametrize('robot_frame', ['base', 'odom', 'map'])
def test_sensor_cannot_claim_other_assets_localization_frame(tmp_path, robot_frame):
    sensor = imu()
    sensor['frame_id'] = 'reserved_frame'

    def add_asset(data):
        data['assets'].append({
            'id': 'other_car', 'model_file': 'models/model.yaml', 'host': 'dev', 'mode': 'mock',
            'frames': {robot_frame: 'reserved_frame'},
        })

    with pytest.raises(ConfigurationError, match='duplicate sensor frame'):
        load_inventory(write_assets(tmp_path, {'imu': sensor}, mutate=add_asset))


def test_assets_may_share_map_and_odom_without_sensor_collisions(tmp_path):
    def add_asset(data):
        data['assets'][0]['frames'] = {'odom': 'fleet/odom', 'map': 'fleet/map'}
        second = deepcopy(data['assets'][0])
        second['id'] = 'other_car'
        data['assets'].append(second)

    assets = load_inventory(write_assets(tmp_path, {'imu': imu()}, mutate=add_asset)).assets
    assert len(assets) == 2
    assert all(asset['frames']['odom'] == 'fleet/odom' for asset in assets)
    assert all(asset['frames']['map'] == 'fleet/map' for asset in assets)


def test_sensor_transform_cycle_fails(tmp_path):
    first, second = imu(), imu()
    first['transform'] = {'parent': 'car/second_link', 'xyz': [0, 0, 0], 'rpy': [0, 0, 0]}
    second['transform'] = {'parent': 'car/first_link', 'xyz': [0, 0, 0], 'rpy': [0, 0, 0]}
    with pytest.raises(ConfigurationError, match='transform cycle'):
        load_inventory(write_assets(tmp_path, {'first': first, 'second': second}))


def test_duplicate_managed_topics_fail(tmp_path):
    with pytest.raises(ConfigurationError, match='duplicate sensor output topic'):
        load_inventory(write_assets(tmp_path, {'first': imu(topic='/imu'), 'second': imu(topic='/imu')}))


def test_multiple_attach_subscriptions_allow_same_topic(tmp_path):
    result = load_inventory(write_assets(tmp_path, {
        'first': imu('attach', '/imu'), 'second': imu('attach', '/imu'),
    }, mode='real'))
    assert len(result.assets[0]['sensors']) == 2


def test_managed_publisher_and_attach_observer_allow_same_topic(tmp_path):
    driver = imu('node', '/imu')
    driver['driver'].update(package='vendor', executable='imu_node')
    result = load_inventory(write_assets(tmp_path, {'first': driver, 'second': imu('attach', '/imu')}, mode='real'))
    assert len(result.assets[0]['sensors']) == 2


def test_type_mismatch_on_shared_attach_topic_fails(tmp_path):
    camera = {'kind': 'camera', 'driver': {'mode': 'attach'}, 'streams': {
        'image': {'type': 'sensor_msgs/msg/Image', 'topic': '/data'},
    }}
    with pytest.raises(ConfigurationError, match='inconsistent sensor topic types'):
        load_inventory(write_assets(tmp_path, {'imu': imu('attach', '/data'), 'camera': camera}, mode='real'))


def test_sensor_conflicts_checked_before_host_selection(tmp_path):
    def add_asset(data):
        data['hosts']['second'] = {'ros_domain_id': 87}
        second = deepcopy(data['assets'][0])
        second.update(id='other_car', host='second')
        data['assets'].append(second)
    with pytest.raises(ConfigurationError, match='duplicate sensor output topic'):
        load_inventory(write_assets(tmp_path, {'imu': imu(topic='/imu')}, mutate=add_asset))


def test_separate_domains_may_share_sensor_topics_and_frames(tmp_path):
    def add_asset(data):
        data['hosts']['second'] = {'ros_domain_id': 88}
        second = deepcopy(data['assets'][0])
        second.update(id='other_car', host='second')
        data['assets'].append(second)
    sensor = imu(topic='/imu')
    sensor['frame_id'] = 'shared/imu'
    inventory = load_inventory(write_assets(tmp_path, {'imu': sensor}, mutate=add_asset))
    assert len(inventory.assets) == 2


def test_disabled_sensors_do_not_reserve_topics_or_frames(tmp_path):
    first, second = imu(topic='/imu'), imu(topic='/imu')
    second.update(enabled=False, frame_id='car/first_link')
    assert load_inventory(write_assets(tmp_path, {'first': first, 'second': second}))


def test_sensor_count_is_bounded_after_model_instance_merge(tmp_path):
    model = {f'imu_{i}': imu() for i in range(64)}
    assert len(load_inventory(write_assets(tmp_path, model_sensors=model)).assets[0]['sensors']) == 64
    with pytest.raises(ConfigurationError, match='at most 64 sensors'):
        load_inventory(write_assets(tmp_path, {'extra': imu()}, model_sensors=model))


def test_stream_count_is_bounded(tmp_path):
    sensor = imu()
    sensor['streams'] = {f'imu_{i}': {'type': 'sensor_msgs/msg/Imu', 'topic': f'data_{i}'} for i in range(32)}
    assert len(load_inventory(write_assets(tmp_path, {'imu': sensor})).assets[0]['sensors']['imu']['streams']) == 32
    sensor['streams']['extra'] = {'type': 'sensor_msgs/msg/Imu', 'topic': 'extra'}
    with pytest.raises(ConfigurationError, match='at most 32 streams'):
        load_inventory(write_assets(tmp_path, {'imu': sensor}))


def test_monitor_disable_changes_digest_and_preserves_stream(tmp_path):
    sensor = imu()
    first = load_inventory(write_assets(tmp_path, {'imu': sensor})).assets[0]
    sensor['streams']['imu']['monitor'] = False
    second = load_inventory(write_assets(tmp_path, {'imu': sensor})).assets[0]
    assert second['sensors']['imu']['streams']['imu']['monitor'] is False
    assert first['config_digest'] != second['config_digest']


def test_sensors_demo_resolves_after_install(tmp_path, monkeypatch):
    packages = pytest.importorskip('ament_index_python.packages')
    monkeypatch.setattr(packages, 'get_package_share_directory', lambda name: str(ROOT))
    path = tmp_path / 'sensors.yaml'
    path.write_text((ROOT / 'config/sensors.demo.yaml').read_text())
    assets = load_inventory(path).assets
    assert len(assets) == 1
    sensors = assets[0]['sensors']
    assert set(sensors) == {'front_camera', 'lidar', 'body_imu'}
    assert sum(len(sensor['streams']) for sensor in sensors.values()) == 5


def camera_with_time_sync(**time_sync):
    return {
        'kind': 'camera', 'driver': {'mode': 'mock'},
        'streams': {'image': {'type': 'sensor_msgs/msg/Image', 'topic': 'image_raw'}},
        'time_sync': time_sync,
    }


def test_time_sync_normalizes_defaults(tmp_path):
    asset = load_inventory(write_assets(tmp_path, {'front_camera': camera_with_time_sync(
        source='board_trigger', board_channel='CAM_1', trigger_hz=10)})).assets[0]
    assert asset['sensors']['front_camera']['time_sync'] == {
        'source': 'board_trigger', 'board_channel': 'CAM_1', 'clock_domain': 'host_utc',
        'requires': 'none', 'max_age_ms': 50.0, 'exposure_offset_us': 0, 'trigger_hz': 10.0,
        'image_stream': 'image',
    }


def test_time_sync_device_timestamp_defaults_to_device_clock(tmp_path):
    asset = load_inventory(write_assets(tmp_path, {'lidar': {
        'kind': 'lidar', 'driver': {'mode': 'mock'},
        'streams': {'points': {'type': 'sensor_msgs/msg/PointCloud2', 'topic': 'points'}},
        'time_sync': {'source': 'device_timestamp', 'board_channel': 'LIDAR_1', 'requires': 'gps_sync'},
    }})).assets[0]
    ts = asset['sensors']['lidar']['time_sync']
    assert ts['clock_domain'] == 'device'
    assert ts['requires'] == 'gps_sync'
    assert ts['board_channel'] == 'LIDAR_1'
    assert 'trigger_hz' not in ts


@pytest.mark.parametrize('time_sync, match', [
    ({'source': 'board_trigger'}, 'CAM_n or IMU_2'),
    ({'source': 'board_trigger', 'board_channel': 'LIDAR_1'}, 'CAM_n or IMU_2'),
    ({'source': 'host_arrival', 'board_channel': 'CAM_1'}, 'cannot claim'),
    ({'source': 'device_timestamp', 'board_channel': 'CAM_1'}, 'LIDAR_n'),
    ({'source': 'gps'}, 'expected board_trigger'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_9'}, 'unknown sync board channel'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_1', 'clock_domain': 'utc'}, 'clock_domain'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_1', 'requires': 'ntp'}, 'requires'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_1', 'extra': 1}, 'unknown fields'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_1', 'max_age_ms': 0}, 'positive finite'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_1', 'exposure_offset_us': 2000000}, 'expected integer'),
])
def test_time_sync_rejects_invalid(tmp_path, time_sync, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_assets(tmp_path, {'front_camera': camera_with_time_sync(**time_sync)}))


def real_camera(**time_sync):
    return {
        'kind': 'camera',
        'driver': {'mode': 'node', 'package': 'camera_aravis2',
                   'executable': 'camera_driver_gv'},
        'streams': {'image': {'type': 'sensor_msgs/msg/Image', 'topic': 'image_raw'}},
        'time_sync': time_sync,
    }


def test_real_board_trigger_requires_host_board(tmp_path):
    sensor = real_camera(source='board_trigger', board_channel='CAM_1')
    with pytest.raises(ConfigurationError, match="no 'board' block"):
        load_inventory(write_assets(tmp_path, {'front_camera': sensor}, mode='real'))
    path = write_assets(tmp_path, {'front_camera': sensor}, mode='real',
                        mutate=lambda d: d['hosts']['dev'].update(board={'ip': '192.168.123.188'}))
    asset = load_inventory(path).assets[0]
    assert asset['sensors']['front_camera']['time_sync']['source'] == 'board_trigger'


def test_mock_board_trigger_needs_no_host_board(tmp_path):
    sensor = camera_with_time_sync(source='board_trigger', board_channel='CAM_1')
    assert load_inventory(write_assets(tmp_path, {'front_camera': sensor})).assets[0]


def test_real_device_timestamp_needs_no_host_board(tmp_path):
    sensor = real_camera(source='device_timestamp')
    assert load_inventory(write_assets(tmp_path, {'front_camera': sensor}, mode='real')).assets[0]


def test_board_trigger_camera_needs_image_stream(tmp_path):
    sensor = {
        'kind': 'camera', 'driver': {'mode': 'mock'},
        'streams': {'info': {'type': 'sensor_msgs/msg/CameraInfo', 'topic': 'camera_info'}},
        'time_sync': {'source': 'board_trigger', 'board_channel': 'CAM_1'},
    }
    with pytest.raises(ConfigurationError, match='Image stream'):
        load_inventory(write_assets(tmp_path, {'front_camera': sensor}))


def test_board_trigger_camera_selects_the_named_image_stream(tmp_path):
    sensor = camera_with_time_sync(source='board_trigger', board_channel='CAM_1',
                                   image_stream='thermal')
    sensor['streams'] = {
        'thermal': {'type': 'sensor_msgs/msg/Image', 'topic': 'thermal/image_raw'},
        'visible': {'type': 'sensor_msgs/msg/Image', 'topic': 'visible/image_raw'},
    }
    ts = load_inventory(write_assets(tmp_path, {'front_camera': sensor})).assets[0][
        'sensors']['front_camera']['time_sync']
    assert ts['image_stream'] == 'thermal'


def test_board_trigger_camera_with_several_image_streams_needs_a_selector(tmp_path):
    sensor = camera_with_time_sync(source='board_trigger', board_channel='CAM_1')
    sensor['streams'] = {
        'thermal': {'type': 'sensor_msgs/msg/Image', 'topic': 'thermal/image_raw'},
        'visible': {'type': 'sensor_msgs/msg/Image', 'topic': 'visible/image_raw'},
    }
    with pytest.raises(ConfigurationError, match='name the one to pair'):
        load_inventory(write_assets(tmp_path, {'front_camera': sensor}))


def test_board_trigger_camera_rejects_a_non_image_selector(tmp_path):
    sensor = camera_with_time_sync(source='board_trigger', board_channel='CAM_1',
                                   image_stream='camera_info')
    sensor['streams'] = {
        'image': {'type': 'sensor_msgs/msg/Image', 'topic': 'image_raw'},
        'camera_info': {'type': 'sensor_msgs/msg/CameraInfo', 'topic': 'camera_info'},
    }
    with pytest.raises(ConfigurationError, match='not an Image stream'):
        load_inventory(write_assets(tmp_path, {'front_camera': sensor}))


@pytest.mark.parametrize('time_sync, match', [
    ({'source': 'host_arrival', 'clock_domain': 'board'}, 'host UTC'),
    ({'source': 'board_trigger', 'board_channel': 'CAM_1', 'clock_domain': 'device'},
     'board/host clock'),
])
def test_time_sync_rejects_contradictory_clock_domain(tmp_path, time_sync, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_assets(tmp_path, {'front_camera': camera_with_time_sync(**time_sync)}))
