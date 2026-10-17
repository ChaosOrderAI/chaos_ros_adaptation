"""Collection preflight and supervision without clocks, ROS discovery or data writes."""

import json
from pathlib import Path
from types import SimpleNamespace

import pytest
from launch import LaunchContext
from launch.actions import ExecuteProcess, LogInfo
from launch.utilities import perform_substitutions
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError
from chaos_bringup import collection, launching
from chaos_bringup.planning import LaunchPlan, prepare_plan


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'


@pytest.fixture(autouse=True)
def private_launch_files(tmp_path, monkeypatch):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))
    generated = []
    constructor = launching.tempfile.TemporaryDirectory

    def track(*args, **kwargs):
        directory = constructor(*args, **kwargs)
        generated.append(directory)
        return directory

    monkeypatch.setattr(launching.tempfile, 'TemporaryDirectory', track)
    yield
    for directory in generated:
        directory.cleanup()


def configured_plan(tmp_path):
    items = list(prepare_plan(ASSETS / 'config/demo.yaml', 'dev'))
    for item in items:
        asset = item['asset']
        asset['data_collection'] = {
            'enabled': True, 'output_dir': str(tmp_path / 'recordings'), 'storage_id': 'mcap',
            'topics': [asset['namespace'] + '/state'],
            'time_sync': {'require_sync': True},
            'max_bagfile_duration_s': 300, 'max_bagfile_size_mb': 1024,
            'min_free_space_mb': 1024,
        }
    return LaunchPlan.from_items(items)


def test_dry_run_reports_collection_without_dependency_or_directory_checks(tmp_path, monkeypatch):
    plan = configured_plan(tmp_path)
    monkeypatch.setattr(launching, 'preflight_collection',
                        lambda *args: pytest.fail('dry run performed collection preflight'))
    actions = launching.build_launch_actions(plan, dry_run=True)
    assert len(actions) == 1 and isinstance(actions[0], LogInfo)
    data = json.loads(perform_substitutions(LaunchContext(), actions[0].msg))
    assert data[0]['data_collection']['time_sync'] == {'require_sync': True}
    assert data[0]['data_collection']['topics'] == ['/robots/vehicle_001/state']
    assert not (tmp_path / 'recordings').exists()


@pytest.mark.parametrize('configured', [None, {'enabled': False}])
def test_disabled_collection_has_no_dependency_or_process(monkeypatch, configured):
    asset = {'data_collection': configured}
    monkeypatch.setattr(collection, 'require_executable',
                        lambda *args: pytest.fail('checked disabled collection dependency'))
    collection.preflight_collection([{'asset': asset}])
    assert collection.collection_summary(asset) is None
    assert collection.collection_processes(asset, {}) == []


def test_missing_collector_fails_before_robot_node_construction(tmp_path, monkeypatch):
    plan = configured_plan(tmp_path)

    def missing(*args):
        raise ConfigurationError('missing executable')

    monkeypatch.setattr(collection, 'require_executable', missing)
    monkeypatch.setattr(launching, 'Node',
                        lambda **kwargs: pytest.fail('constructed a node before preflight'))
    with pytest.raises(ConfigurationError, match='chaos_data/collector'):
        launching.build_launch_actions(plan)


def test_storage_selection_checked_for_every_enabled_asset(tmp_path, monkeypatch):
    items = list(configured_plan(tmp_path))
    items[1]['asset']['data_collection']['storage_id'] = 'sqlite3'
    calls = []
    monkeypatch.setattr(collection, 'require_executable', lambda *args: calls.append(args))
    monkeypatch.setattr(collection, 'registered_writers', lambda: {'mcap'})
    monkeypatch.setattr(launching, 'Node',
                        lambda **kwargs: pytest.fail('constructed a node before complete preflight'))
    with pytest.raises(ConfigurationError, match='storage plugin is unavailable: sqlite3'):
        launching.build_launch_actions(LaunchPlan.from_items(items))
    assert calls == [('chaos_data', 'collector')]


def test_output_preflight_accepts_missing_directory_without_creating_it(tmp_path):
    path = tmp_path / 'new' / 'recordings'
    collection.check_output_directory(path)
    assert not (tmp_path / 'new').exists()


def test_output_preflight_rejects_file_ancestor(tmp_path):
    path = tmp_path / 'file'
    path.write_text('existing user data')
    with pytest.raises(ConfigurationError, match='cannot inspect|not a directory'):
        collection.check_output_directory(path / 'recordings')
    assert path.read_text() == 'existing user data'


def test_output_preflight_requires_write_and_traverse_access(tmp_path, monkeypatch):
    calls = []

    def denied(path, mode):
        calls.append((path, mode))
        return False

    monkeypatch.setattr(collection.os, 'access', denied)
    with pytest.raises(ConfigurationError, match='not writable'):
        collection.check_output_directory(tmp_path / 'recordings')
    assert calls == [(tmp_path, collection.os.W_OK | collection.os.X_OK)]


def test_collector_receives_frozen_asset_environment_and_shutdown_grace(tmp_path, monkeypatch):
    asset = configured_plan(tmp_path)[0]['asset']
    asset['description'] = {'xml': '<robot/>', 'sha256': 'digest', 'enabled': False}
    environment = {'ROS_DOMAIN_ID': '57', 'ROS_AUTOMATIC_DISCOVERY_RANGE': 'LOCALHOST'}
    captured = []

    def node(**kwargs):
        captured.append(kwargs)
        return SimpleNamespace(**kwargs)

    monkeypatch.setattr(collection, 'Node', node)
    children = collection.collection_processes(asset, environment)
    assert len(children) == 1
    options = captured[0]
    assert (options['package'], options['executable'], options['namespace']) == (
        'chaos_data', 'collector', asset['namespace'])
    assert options['additional_env'] == environment
    assert float(options['sigterm_timeout']) >= 40
    assert float(options['sigkill_timeout']) > 0
    parameter = options['parameters'][0]['asset_json']
    assert isinstance(parameter, ParameterValue)
    snapshot = json.loads(parameter.evaluate(LaunchContext()))
    assert snapshot['data_collection'] == asset['data_collection']
    assert snapshot['description'] == {'sha256': 'digest', 'enabled': False}
    assert asset['description']['xml'] == '<robot/>'
    assert options['parameters'][0]['use_sim_time'] is False


@pytest.mark.parametrize('returncode', [0, 1, -9])
def test_collection_exit_reports_without_stopping_robot(returncode, monkeypatch):
    messages = []
    monkeypatch.setattr(collection.launch.logging, 'get_logger',
                        lambda name: SimpleNamespace(error=messages.append))
    callback = collection.on_collection_exit('Data collection robot_a')
    result = callback(SimpleNamespace(returncode=returncode), SimpleNamespace(is_shutdown=False))
    assert result == []
    assert len(messages) == 1 and 'robot_a' in messages[0] and str(returncode) in messages[0]
    assert callback(SimpleNamespace(returncode=returncode), SimpleNamespace(is_shutdown=True)) == []
    assert len(messages) == 1


def test_launch_wires_auxiliary_exit_policy_for_collector(tmp_path, monkeypatch):
    plan = configured_plan(tmp_path)
    monkeypatch.setattr(collection, 'require_executable', lambda *args: None)
    monkeypatch.setattr(collection, 'registered_writers', lambda: {'mcap'})
    captured = []
    constructor = launching.OnProcessExit

    def handler(**kwargs):
        captured.append(kwargs)
        return constructor(**kwargs)

    monkeypatch.setattr(launching, 'OnProcessExit', handler)
    actions = launching.build_launch_actions(plan)
    assert sum(isinstance(action, ExecuteProcess) for action in actions) == 4
    auxiliary = [options for options in captured
                 if options['target_action'].node_executable == 'collector']
    assert len(auxiliary) == 2
    for options in auxiliary:
        result = options['on_exit'](SimpleNamespace(returncode=1), SimpleNamespace(is_shutdown=False))
        assert result == []
    assert not (tmp_path / 'recordings').exists()
