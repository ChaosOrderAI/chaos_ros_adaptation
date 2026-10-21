"""Sensor startup validation without importing or running any hardware driver.

The sensor declaration, the process specification and the parameter file
snapshot are derived by the native planner. The capabilities of the build are
injected, so a test states the deployment it simulates instead of replacing
internals of the package.
"""

import json
from pathlib import Path

import pytest
import yaml
from launch import LaunchContext
from launch.actions import ExecuteProcess, LogInfo
from launch.utilities import perform_substitutions

from chaos_assets import ConfigurationError
from chaos_bringup import launching, sensors
from chaos_bringup.planning import LaunchPlan, prepare_plan


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'


@pytest.fixture(autouse=True)
def private_launch_log(tmp_path, monkeypatch):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))


def inventory(tmp_path, *, mode='real', driver_mode='node', parameter_files=None):
    sensor = {
        'kind': 'camera', 'driver': {'mode': driver_mode},
        'streams': {'image': {'type': 'sensor_msgs/msg/Image', 'topic': 'image_raw'}},
    }
    if driver_mode == 'node':
        sensor['driver'].update(package='vendor_camera', executable='camera_node',
                                parameters={'serial_number': '00123'})
        if parameter_files:
            sensor['driver']['parameter_files'] = parameter_files
    data = {'schema_version': 1, 'hosts': {'local': {'ros_domain_id': 87}},
            'assets': [{'id': 'robot_a', 'host': 'local', 'mode': mode,
                        'model_file': str(ASSETS / 'models/lingao_vehicle/v1/model.yaml'),
                        'sensors': {'front': sensor}}]}
    path = tmp_path / 'asset.yaml'
    path.write_text(yaml.safe_dump(data))
    return prepare_plan(path, 'local')


def test_dry_run_shows_sensor_plan_without_vendor_dependencies(tmp_path, capabilities):
    plan = inventory(tmp_path)

    def forbidden(*args):
        pytest.fail('the dry run preflighted binaries')

    capabilities['executable_available'] = forbidden
    capabilities['validate_asset'] = forbidden
    actions = launching.build_launch_actions(plan, dry_run=True, capabilities=capabilities)
    assert len(actions) == 1 and isinstance(actions[0], LogInfo)
    summary = json.loads(perform_substitutions(LaunchContext(), actions[0].msg))[0]['sensors'][0]
    assert summary['mode'] == 'node'
    assert summary['streams']['image']['topic'] == '/robots/robot_a/sensors/front/image_raw'
    assert '00123' not in str(summary)


def test_preflight_rejects_missing_driver_before_constructing_any_process(tmp_path, capabilities,
                                                                         monkeypatch):
    plan = inventory(tmp_path)

    def missing(package, executable):
        # Everything of the platform is available, the vendor driver is not.
        return (package, executable) != ('vendor_camera', 'camera_node')

    capabilities['executable_available'] = missing
    monkeypatch.setattr(launching, 'Node',
                        lambda **kwargs: pytest.fail('created node before preflight'))
    with pytest.raises(ConfigurationError, match='vendor_camera'):
        launching.build_launch_actions(plan, capabilities=capabilities)


def test_mock_override_does_not_require_vendor_driver(tmp_path, capabilities):
    plan = inventory(tmp_path, mode='mock')
    dependencies = []
    capabilities['executable_available'] = lambda package, executable: dependencies.append(
        (package, executable)) or True
    launching.build_launch_actions(plan, capabilities=capabilities)
    assert set(dependencies) == {('chaos_sensors', 'mock_sensor'), ('chaos_sensors', 'sensor_monitor')}
    children = sensors.sensor_processes(plan[0]['asset'], {}, {})
    assert len(children) == 2
    assert all(isinstance(child, ExecuteProcess) for _, child in children)


def test_attach_only_starts_monitor(tmp_path, capabilities):
    plan = inventory(tmp_path, driver_mode='attach')
    dependencies = []
    capabilities['executable_available'] = lambda package, executable: dependencies.append(
        (package, executable)) or True
    launching.build_launch_actions(plan, capabilities=capabilities)
    assert dependencies == [('chaos_sensors', 'sensor_monitor')]
    children = sensors.sensor_processes(plan[0]['asset'], {}, {})
    assert [label for label, _ in children] == ['Sensor monitor robot_a']


def test_disabled_sensor_has_no_process_or_dependency(tmp_path, capabilities):
    plan = inventory(tmp_path)
    items = list(plan)
    items[0]['asset']['sensors']['front']['enabled'] = False
    dependencies = []
    capabilities['executable_available'] = lambda package, executable: dependencies.append(
        (package, executable)) or True
    launching.build_launch_actions(LaunchPlan.from_items(items), capabilities=capabilities)
    assert dependencies == []
    assert sensors.sensor_processes(items[0]['asset'], {}, {}) == []


def test_parameter_snapshot_is_private_and_survives_source_replacement(tmp_path):
    source = tmp_path / 'vendor.yaml'
    content = '/**:\n  ros__parameters:\n    serial_number: "00123"\n'
    source.write_text(content)
    plan = inventory(tmp_path, parameter_files=[str(source)])
    root = tmp_path / 'snapshots'
    root.mkdir()
    snapshots = sensors.snapshot_parameter_files(plan, root)
    source.write_text('replaced: true\n')
    copied = Path(snapshots['robot_a/front'][0])
    assert copied.read_text() == content
    assert copied.stat().st_mode & 0o777 == 0o600


@pytest.mark.parametrize('content', ['[', '[]', '', 'serial_number: "00123"',
                                   'orbbec_camera_node:\n  ros__parameters:\n    serial_number: "00123"',
                                   '/**:\n  ros__parameters:\n    frequency: .inf'])
def test_malformed_parameter_file_fails_before_startup(tmp_path, content):
    source = tmp_path / 'vendor.yaml'
    source.write_text(content)
    plan = inventory(tmp_path, parameter_files=[str(source)])
    with pytest.raises(ConfigurationError, match='parameter YAML'):
        sensors.snapshot_parameter_files(plan, tmp_path)


@pytest.mark.parametrize('selector', ['/**', '/robots/*/sensors/**/driver',
                                     '/robots/robot_a/sensors/front/driver'])
def test_parameter_selectors_match_managed_node(selector):
    content = yaml.safe_dump({selector: {'ros__parameters': {'serial_number': '00123'}}})
    sensors.validate_parameter_file(content, '/robots/robot_a/sensors/front/driver', 'test.yaml')


def test_nested_namespace_parameter_file():
    content = yaml.safe_dump({'/robots': {'robot_a': {'sensors': {'front': {'driver': {
        'ros__parameters': {'camera': {'enabled': True}}}}}}}})
    sensors.validate_parameter_file(content, '/robots/robot_a/sensors/front/driver', 'test.yaml')


def test_duplicate_parameter_values_rejected():
    content = '/**:\n  ros__parameters:\n    serial: "one"\n    serial: "two"\n'
    with pytest.raises(ConfigurationError, match='duplicate YAML key'):
        sensors.validate_parameter_file(content, '/robots/robot_a/sensors/front/driver', 'test.yaml')


def test_gemini335_template_can_run_as_mock_without_orbbec_binary(capabilities):
    plan = prepare_plan(ASSETS / 'config/orbbec_gemini335.example.yaml', 'onboard', mode='mock')
    camera = plan[0]['asset']['sensors']['front_camera']
    assert camera['driver']['mode'] == 'mock'
    assert len(camera['streams']) == 5
    assert camera['streams']['color_image']['monitor'] is False
    assert camera['streams']['imu']['frame_id'].endswith('_accel_gyro_optical_frame')
    dependencies = []
    capabilities['executable_available'] = lambda package, executable: dependencies.append(
        (package, executable)) or True
    launching.build_launch_actions(plan, capabilities=capabilities)
    assert all(package != 'orbbec_camera' for package, _ in dependencies)
