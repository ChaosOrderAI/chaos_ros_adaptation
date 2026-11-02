"""URDF startup contracts without running publishers or opening robot hardware.

The description of an asset, the process specification of the publisher and
the dependency check are derived by the native planner. The capabilities of
the build are injected, so a test states the deployment it simulates instead
of replacing internals of the package.
"""

import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

import pytest
from launch import LaunchContext
from launch.actions import ExecuteProcess, LogInfo, RegisterEventHandler
from launch.utilities import perform_substitutions
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError
from chaos_bringup import description, launching
from chaos_bringup.planning import LaunchPlan, prepare_plan


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'
XML = '<robot name="example"><link name="base_link"/></robot>'


@pytest.fixture(autouse=True)
def private_launch_logs_and_snapshots(tmp_path, monkeypatch):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))
    generated = []
    make_directory = launching.tempfile.TemporaryDirectory

    def track_directory(*args, **kwargs):
        directory = make_directory(*args, **kwargs)
        generated.append(directory)
        return directory

    monkeypatch.setattr(launching.tempfile, 'TemporaryDirectory', track_directory)
    yield generated
    for directory in generated:
        directory.cleanup()


def with_descriptions(tmp_path, *, mqtt=False):
    items = list(prepare_plan(ASSETS / 'config' / ('mqtt.example.yaml' if mqtt else 'demo.yaml'),
                              'dev', enable_mqtt=mqtt, check_files=False))
    source = tmp_path / 'example.urdf'
    source.write_text(XML)
    for item in items:
        asset = item['asset']
        prefix = asset['id'] + '/'
        asset['description'] = {
            'enabled': True, 'file': str(source), 'format': 'urdf', 'arguments': {},
            'xml': XML, 'frame_prefix': prefix, 'publish_frequency': 30,
            'use_sim_time': False, 'joint_states_topic': asset['namespace'] + '/joint_states',
            'root': prefix + 'base_link', 'links': [prefix + 'base_link'], 'joints': [],
            'sha256': hashlib.sha256(XML.encode()).hexdigest(),
        }
    return LaunchPlan.from_items(items), source


def record_nodes(monkeypatch):
    recorded = []
    node_constructor = launching.Node

    def make_node(**kwargs):
        recorded.append(kwargs)
        return node_constructor(**kwargs)

    monkeypatch.setattr('chaos_bringup.nodes.Node', make_node)
    monkeypatch.setattr(launching, 'Node', make_node)
    return recorded


def evaluated_parameters(options):
    return {key: value.evaluate(LaunchContext()) if isinstance(value, ParameterValue) else value
            for values in options['parameters'] for key, value in values.items()}


def test_dry_run_has_only_description_summary_without_publisher_dependency(tmp_path,
                                                                          capabilities):
    plan, _ = with_descriptions(tmp_path)

    def forbidden(*args):
        pytest.fail('the dry run checked a publisher binary')

    capabilities['executable_available'] = forbidden
    capabilities['validate_asset'] = forbidden
    actions = launching.build_launch_actions(plan, dry_run=True, capabilities=capabilities)
    assert len(actions) == 1 and isinstance(actions[0], LogInfo)
    output = perform_substitutions(LaunchContext(), actions[0].msg)
    summary = json.loads(output)[0]['description']
    assert summary == {
        'file': str(tmp_path / 'example.urdf'), 'format': 'urdf',
        'root': 'vehicle_001/base_link', 'frame_prefix': 'vehicle_001/',
        'link_count': 1, 'joint_count': 0, 'sha256': hashlib.sha256(XML.encode()).hexdigest(),
    }
    assert XML not in output


@pytest.mark.parametrize('source_change', ['replace', 'delete'])
def test_publishers_use_frozen_xml_after_source_changes(tmp_path, monkeypatch, source_change,
                                                         capabilities):
    plan, source = with_descriptions(tmp_path)
    items = list(plan)
    items[1]['asset']['ros_domain_id'] = 88
    plan = LaunchPlan.from_items(items)
    if source_change == 'delete':
        source.unlink()
    else:
        source.write_text('invalid replacement!')
    calls = []
    capabilities['executable_available'] = lambda package, executable: calls.append(
        (package, executable)) or True
    recorded = record_nodes(monkeypatch)
    actions = launching.build_launch_actions(plan, discovery='SUBNET', capabilities=capabilities)
    publishers = [options for options in recorded if options['package'] == 'robot_state_publisher']
    assert calls == [('robot_state_publisher', 'robot_state_publisher')]
    assert len(publishers) == len(plan) == 2
    for item, options in zip(plan, publishers):
        asset = item['asset']
        assert options['namespace'] == asset['namespace']
        assert options['name'] == 'robot_state_publisher'
        assert options['additional_env'] == {
            'ROS_DOMAIN_ID': str(asset['ros_domain_id']), 'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SUBNET',
        }
        assert options['remappings'] == [('joint_states', asset['namespace'] + '/joint_states'),
                                         ('tf', '/tf'), ('tf_static', '/tf_static')]
        assert evaluated_parameters(options) == {
            'robot_description': XML, 'frame_prefix': asset['id'] + '/',
            'publish_frequency': 30.0, 'use_sim_time': False,
        }
        assert isinstance(options['parameters'][0]['robot_description'], ParameterValue)
        assert isinstance(options['parameters'][0]['frame_prefix'], ParameterValue)
    assert all(options['package'] != 'joint_state_publisher' for options in recorded)
    # Each of the four processes has an exit handler, plus shared cleanup.
    assert sum(isinstance(action, ExecuteProcess) for action in actions) == 4
    assert sum(isinstance(action, RegisterEventHandler) for action in actions) == 5


def test_missing_publisher_rejected_before_any_process_is_constructed(tmp_path, monkeypatch,
                                                                     capabilities):
    plan, _ = with_descriptions(tmp_path)

    def unavailable(package, executable):
        return (package, executable) != ('robot_state_publisher', 'robot_state_publisher')

    capabilities['executable_available'] = unavailable
    monkeypatch.setattr(launching, 'Node',
                        lambda **kwargs: pytest.fail('constructed runtime before preflight'))
    monkeypatch.setattr('chaos_bringup.nodes.Node',
                        lambda **kwargs: pytest.fail('constructed publisher before preflight'))
    with pytest.raises(ConfigurationError, match='robot_state_publisher'):
        launching.build_launch_actions(plan, capabilities=capabilities)


@pytest.mark.parametrize('configured', [None, {'enabled': False}])
def test_absent_or_disabled_description_has_no_dependency_or_publisher(tmp_path, monkeypatch,
                                                                      capabilities, configured):
    items = list(prepare_plan(ASSETS / 'config/demo.yaml', 'dev'))
    for item in items:
        item['asset']['description'] = configured
    asked = []
    capabilities['executable_available'] = lambda package, executable: asked.append(
        (package, executable)) or True
    recorded = record_nodes(monkeypatch)
    launching.build_launch_actions(LaunchPlan.from_items(items), capabilities=capabilities)
    # A disabled description is not a dependency of the launch.
    assert ('robot_state_publisher', 'robot_state_publisher') not in asked
    assert len(recorded) == 2
    assert {options['package'] for options in recorded} == {'chaos_runtime'}


def test_nonpublisher_nodes_receive_metadata_without_expanded_xml(tmp_path, monkeypatch,
                                                               private_launch_logs_and_snapshots,
                                                                  capabilities):
    plan, _ = with_descriptions(tmp_path, mqtt=True)
    monkeypatch.setattr(launching.subprocess, 'run', lambda *args, **kwargs: SimpleNamespace(returncode=0))
    sensor_assets = []

    def record_sensors(asset, *args):
        sensor_assets.append(asset)
        return []

    monkeypatch.setattr(launching, 'sensor_processes', record_sensors)
    recorded = record_nodes(monkeypatch)
    launching.build_launch_actions(plan, capabilities=capabilities)
    component_options = [options for options in recorded
                         if options['package'] in ('chaos_runtime', 'chaos_mqtt')]
    assert len(component_options) == 4
    assets = sensor_assets + [json.loads(evaluated_parameters(options)['asset_json'])
                             for options in component_options]
    for asset in assets:
        assert 'xml' not in asset['description']
        assert asset['description']['sha256'] == hashlib.sha256(XML.encode()).hexdigest()
    assert all(item['asset']['description']['xml'] == XML for item in plan)
    directory = Path(private_launch_logs_and_snapshots[0].name)
    snapshot = directory / 'resolved-assets.json'
    assert snapshot.stat().st_mode & 0o777 == 0o600
    assert all(asset['description']['xml'] == XML for asset in json.loads(snapshot.read_text()))


def test_explicit_empty_frame_prefix_remains_a_string(tmp_path, monkeypatch):
    plan, _ = with_descriptions(tmp_path)
    asset = plan[0]['asset']
    asset['description']['frame_prefix'] = ''
    asset['description']['use_sim_time'] = True
    asset['description']['joint_states_topic'] = '/driver/measured_joint_states'
    captured = []
    monkeypatch.setattr('chaos_bringup.nodes.Node', lambda **kwargs: captured.append(kwargs))
    children = description.description_processes(asset, {})
    assert len(children) == 1
    parameters = evaluated_parameters(captured[0])
    assert parameters['frame_prefix'] == ''
    assert parameters['use_sim_time'] is True
    assert captured[0]['remappings'][0] == ('joint_states', '/driver/measured_joint_states')
