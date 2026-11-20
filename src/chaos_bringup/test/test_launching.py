"""Launch regressions that never execute a node, driver or transport process.

The launch entry points and the action assembly are the Python boundary of
the package. The capabilities of the build are injected into the preflight,
so a test states the deployment it simulates instead of replacing internals
of the package.
"""

import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace

import pytest
import yaml

from launch import LaunchContext
from launch.actions import ExecuteProcess, LogInfo, UnsetEnvironmentVariable
from launch.utilities import perform_substitutions

from chaos_bringup import launching, planning


PACKAGE = Path(__file__).resolve().parents[1]
ASSETS = PACKAGE.parent / 'chaos_assets'


@pytest.fixture(autouse=True)
def launch_logs_in_test_directory(tmp_path, monkeypatch):
    monkeypatch.setenv('ROS_LOG_DIR', str(tmp_path / 'logs'))


def load_entry(name):
    spec = importlib.util.spec_from_file_location(
        f'chaos_{name}_launch_test', PACKAGE / 'launch' / f'{name}.launch.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize('entry', ['edge', 'system'])
def test_entrypoint_uses_one_snapshot_even_if_inventory_changes(tmp_path, monkeypatch, entry):
    data = {
        'schema_version': 1,
        'hosts': {'onboard': {'ros_domain_id': 61}},
        'assets': [{'id': 'vehicle_001', 'host': 'onboard', 'mode': 'real',
                    'model_file': str(ASSETS / 'models/lingao_vehicle/v1/model.yaml')}],
    }
    path = tmp_path / 'asset.yaml'
    path.write_text(yaml.safe_dump(data))
    context = LaunchContext()
    context.launch_configurations.update({
        'inventory': str(path), 'asset_id': 'vehicle_001', 'host_id': 'onboard',
        'assets': 'vehicle_001', 'mode': '', 'enable_mqtt': 'false',
        'dry_run': 'true', 'discovery_range': 'LOCALHOST',
    })
    actions = load_entry(entry)._start(context)
    assert len(actions) == 1 and isinstance(actions[0], LogInfo)
    summary = json.loads(perform_substitutions(context, actions[0].msg))
    assert summary[0]['mode'] == 'real'
    assert summary[0]['plugin'] == 'lingao_ros2'
    # A configuration deployment changes the file after the launch was built.
    data['assets'][0]['mode'] = 'mock'
    path.write_text(yaml.safe_dump(data))
    # The launch keeps the snapshot it read: the summary of the built actions
    # is the one of the read, not the one of the changed file.
    assert json.loads(perform_substitutions(context, actions[0].msg)) == summary
    # A new read of the inventory sees the change, so the file really changed.
    assert planning.prepare_plan(path, 'onboard', mode='', asset_ids=['vehicle_001'],
                                  check_files=False)[0]['asset']['mode'] == 'mock'


def test_every_process_obeys_explicit_discovery_and_removes_legacy_override(monkeypatch,
                                                                             capabilities):
    monkeypatch.setenv('ROS_LOCALHOST_ONLY', '1')
    monkeypatch.setenv('ROS_AUTOMATIC_DISCOVERY_RANGE', 'LOCALHOST')
    generated = []
    temporary_directory = launching.tempfile.TemporaryDirectory

    def track_directory(*args, **kwargs):
        directory = temporary_directory(*args, **kwargs)
        generated.append(directory)
        return directory

    monkeypatch.setattr(launching.tempfile, 'TemporaryDirectory', track_directory)
    # Only the parser subprocess is replaced; no process actions are executed.
    monkeypatch.setattr(launching.subprocess, 'run',
                        lambda *a, **k: SimpleNamespace(returncode=0))
    plan = planning.prepare_plan(ASSETS / 'config/mqtt.example.yaml', 'dev',
                                 enable_mqtt=True, check_files=False)
    try:
        actions = launching.build_launch_actions(plan, discovery='SUBNET',
                                                 capabilities=capabilities)
        context = LaunchContext()
        children = []
        for action in actions:
            if isinstance(action, UnsetEnvironmentVariable):
                action.execute(context)
            elif isinstance(action, ExecuteProcess):
                assert 'ROS_LOCALHOST_ONLY' not in context.environment
                env = dict(context.environment)
                env.update({perform_substitutions(context, key): perform_substitutions(context, value)
                            for key, value in action.additional_env})
                assert env['ROS_AUTOMATIC_DISCOVERY_RANGE'] == 'SUBNET'
                assert env['ROS_DOMAIN_ID'] == '87'
                children.append(action)
        # Runtime + gateway + transport for each of the two assets, plus the
        # skill server: the MQTT capability commands reach the platform
        # through the skill layer, so it runs whenever MQTT is enabled.
        assert len(children) == 8
    finally:
        for directory in generated:
            directory.cleanup()
