"""Go2 preflight/ownership checks do not open SDK channels or launch hardware.

The capability of the driver plugin is a property of the build and is
reported by the runtime, so the test injects it instead of probing an SDK
package.
"""
import json
from pathlib import Path

import pytest
import yaml
from launch import LaunchContext
from launch.actions import ExecuteProcess, LogInfo
from launch.utilities import perform_substitutions

from chaos_assets import ConfigurationError
from chaos_bringup import launching, planning


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'


def inventory(tmp_path, mode='real', extra=None):
    data = {
        'schema_version': 1, 'hosts': {'dev': {'ros_domain_id': 87}},
        'assets': [{'id': 'dog_001', 'host': 'dev', 'mode': mode,
                    'model_file': str(ASSETS / 'models/unitree_go2/v1/model.yaml')}],
    }
    if extra:
        extra(data)
    path = tmp_path / 'dogs.yaml'
    path.write_text(yaml.safe_dump(data))
    return path


def test_go2_dry_run_does_not_require_or_import_sdk(tmp_path, monkeypatch, capabilities):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))

    def fail(*args):
        raise AssertionError('the dry run must never check the SDK or import a driver')

    capabilities['backend_available'] = fail
    capabilities['validate_asset'] = fail
    plan = planning.prepare_plan(inventory(tmp_path), 'dev')
    actions = launching.build_launch_actions(plan, dry_run=True, capabilities=capabilities)
    assert len(actions) == 1 and isinstance(actions[0], LogInfo)
    summary = json.loads(perform_substitutions(LaunchContext(), actions[0].msg))
    assert summary[0]['plugin'] == 'unitree_go2'
    assert 'navigation.pose' not in summary[0]['capabilities']


def test_missing_sdk_refuses_real_launch_before_process_construction(tmp_path, monkeypatch,
                                                                     capabilities):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))
    # The build of the runtime does not contain the native backend of the
    # plugin, so a real asset may not start.
    capabilities['backend_available'] = lambda plugin: False
    monkeypatch.setattr(launching, 'Node',
                        lambda **kwargs: pytest.fail('created a node before the preflight'))
    plan = planning.prepare_plan(inventory(tmp_path), 'dev')
    with pytest.raises(ConfigurationError, match='native backend'):
        launching.build_launch_actions(plan, capabilities=capabilities)


@pytest.mark.parametrize('mode,expected', [('mock', 1), ('real', 2)])
def test_only_real_go2_gets_managed_driver(tmp_path, monkeypatch, capabilities, mode, expected):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))
    directories = []
    original = launching.tempfile.TemporaryDirectory

    def track(*args, **kwargs):
        directory = original(*args, **kwargs)
        directories.append(directory)
        return directory

    monkeypatch.setattr(launching.tempfile, 'TemporaryDirectory', track)
    try:
        plan = planning.prepare_plan(inventory(tmp_path, mode), 'dev')
        actions = launching.build_launch_actions(plan, capabilities=capabilities)
        children = [a for a in actions if isinstance(a, ExecuteProcess)]
        assert len(children) == expected
        context = LaunchContext()
        for child in children:
            env = {perform_substitutions(context, key): perform_substitutions(context, value)
                   for key, value in child.additional_env}
            assert env['ROS_DOMAIN_ID'] == '87'
            assert env['ROS_AUTOMATIC_DISCOVERY_RANGE'] == 'LOCALHOST'
        if mode == 'real':
            assert children[-1].node_executable == 'go2_driver'
    finally:
        for directory in directories:
            directory.cleanup()


def test_two_assets_cannot_control_same_dds_robot_even_with_different_ros_topics(tmp_path):
    def second(data):
        asset = dict(data['assets'][0], id='dog_002', driver={'options': {
            'cmd_vel_topic': '/dog2/cmd_vel', 'odom_topic': '/dog2/odom'}})
        data['assets'].append(asset)
    with pytest.raises(ConfigurationError, match='same DDS connection'):
        planning.prepare_plan(inventory(tmp_path, extra=second), 'dev', asset_ids=['dog_001'])


def test_two_go2_drivers_cannot_publish_same_odometry(tmp_path):
    def second(data):
        asset = dict(data['assets'][0], id='dog_002', driver={'options': {
            'network_interface': 'eno2', 'cmd_vel_topic': '/dog2/cmd_vel'}})
        data['assets'].append(asset)
    with pytest.raises(ConfigurationError, match='same odometry output'):
        planning.prepare_plan(inventory(tmp_path, extra=second), 'dev', asset_ids=['dog_001'])


@pytest.mark.parametrize('reverse', [False, True])
def test_go2_odom_cannot_conflict_with_other_velocity_output(tmp_path, reverse):
    def second(data):
        asset = dict(data['assets'][0], id='dog_002', driver={'options': {
            'network_interface': 'eno2', 'cmd_vel_topic': '/go2/odom', 'odom_topic': '/dog2/odom'}})
        data['assets'].append(asset)
        if reverse:
            data['assets'].reverse()
    with pytest.raises(ConfigurationError, match='odometry output conflicts'):
        planning.prepare_plan(inventory(tmp_path, extra=second), 'dev')
