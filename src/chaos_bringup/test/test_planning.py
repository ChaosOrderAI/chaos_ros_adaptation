"""Planning contracts of the native planner.

The planning rules are implemented in C++. These tests drive the public
Python surface of the package and keep the assertion intent of the original
behaviour tests: the selection of the assets, the immutability of the
snapshot, the ownership rules and the resolution of the transport executable.
"""

import os
from copy import deepcopy
from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError
from chaos_bringup import planning
from chaos_bringup.planning import prepare_plan


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'


def _absolute_models(data):
    """The model files are referenced by an absolute path."""
    for asset in data['assets']:
        asset['model_file'] = str((ASSETS / 'config' / asset['model_file']).resolve())
    return data


def test_demo_plan_starts_two_independent_assets():
    plan = prepare_plan(ASSETS / 'config/demo.yaml', 'dev')
    assert len(plan) == 2
    assert len({item['asset']['namespace'] for item in plan}) == 2
    assert all(item['mqtt'] is None for item in plan)


def test_mqtt_is_opt_in_and_generated_per_asset():
    path = ASSETS / 'config/mqtt.example.yaml'
    assert all(p['mqtt'] is None for p in prepare_plan(path, 'dev'))
    plan = prepare_plan(path, 'dev', enable_mqtt=True, check_files=False)
    configs = [item['mqtt']['config'] for item in plan]
    assert len({c['mqtt']['client_id'] for c in configs}) == 2
    assert all(c['route_profile'] == 'custom' for c in configs)
    vehicle_routes = {r['command'] for r in configs[0]['routes']}
    assert 'flight.takeoff' not in vehicle_routes
    assert 'flight.takeoff' in {r['command'] for r in configs[1]['routes']}


def test_plan_is_immutable_despite_mutating_returned_nested_configuration():
    plan = prepare_plan(ASSETS / 'config/mqtt.example.yaml', 'dev',
                        enable_mqtt=True, check_files=False)
    first = plan[0]
    first['asset']['driver']['plugin'] = 'replaced'
    first['mqtt']['config']['mqtt']['client_id'] = 'replaced'
    assert plan[0]['asset']['driver']['plugin'] == 'mock'
    assert plan[0]['mqtt']['config']['mqtt']['client_id'] != 'replaced'


def test_shared_mqtt_identity_rejected_before_launch(tmp_path):
    data = _absolute_models(yaml.safe_load((ASSETS / 'config/mqtt.example.yaml').read_text()))
    for asset in data['assets']:
        asset['model_file'] = str((ASSETS / 'config' / asset['model_file']).resolve())
    data['transports']['edge_mqtt']['config']['mqtt']['client_id'] = 'same'
    path = tmp_path / 'bad.yaml'
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ConfigurationError, match='duplicate MQTT client_id'):
        prepare_plan(path, 'dev', enable_mqtt=True, check_files=False)


def test_two_real_assets_cannot_own_one_cmd_vel(tmp_path):
    data = yaml.safe_load((ASSETS / 'config/demo.yaml').read_text())
    first = data['assets'][0]
    first['model_file'] = str((ASSETS / 'config' / first['model_file']).resolve())
    second = deepcopy(first)
    second['id'] = 'vehicle_002'
    data['assets'] = [first, second]
    path = tmp_path / 'bad.yaml'
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ConfigurationError, match='same velocity output'):
        prepare_plan(path, 'dev', mode='real')


def test_mqtt_collision_across_hosts_is_rejected_even_with_asset_filter(tmp_path):
    data = yaml.safe_load((ASSETS / 'config/mqtt.example.yaml').read_text())
    for asset in data['assets']:
        asset['model_file'] = str((ASSETS / 'config' / asset['model_file']).resolve())
    data['hosts']['remote'] = {'ros_domain_id': 88}
    data['assets'][1]['host'] = 'remote'
    data['transports']['edge_mqtt']['config']['mqtt']['client_id'] = 'collision'
    path = tmp_path / 'bad.yaml'
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ConfigurationError, match='duplicate MQTT client_id'):
        prepare_plan(path, 'dev', asset_ids=['vehicle_001'], enable_mqtt=True, check_files=False)


def test_bundled_transport_is_selected_from_package_index_not_path(tmp_path, monkeypatch):
    import ament_index_python

    prefix = Path(ament_index_python.packages.get_package_prefix('chaos_mqtt'))
    binary = prefix / 'lib/chaos_mqtt/mqtt_transport'
    if not binary.is_file():
        pytest.skip('the chaos_mqtt package is not installed')
    # An unrelated legacy binary on PATH must not override the installed one.
    legacy = tmp_path / 'legacy'
    legacy.mkdir()
    (legacy / 'mqtt_ros2_adapter').write_text('#!/bin/sh\nexit 0\n')
    (legacy / 'mqtt_ros2_adapter').chmod(0o755)
    monkeypatch.setenv('PATH', f'{legacy}:{os.environ.get("PATH", "")}')
    data = yaml.safe_load((ASSETS / 'config/mqtt.example.yaml').read_text())
    for asset in data['assets']:
        asset['model_file'] = str((ASSETS / 'config' / asset['model_file']).resolve())
    mqtt = data['transports']['edge_mqtt']['config']['mqtt']
    for key in ('ca_file', 'client_certificate_file', 'client_key_file'):
        certificate = tmp_path / key
        certificate.touch()
        mqtt[key] = str(certificate)
    path = tmp_path / 'assets.yaml'
    path.write_text(yaml.safe_dump(data))
    plan = prepare_plan(path, 'dev', enable_mqtt=True)
    assert all(item['mqtt']['executable'] == str(binary) for item in plan)


def test_missing_bundled_package_fails_without_legacy_path_fallback(tmp_path, monkeypatch):
    legacy = tmp_path / 'legacy'
    legacy.mkdir()
    (legacy / 'mqtt_ros2_adapter').write_text('#!/bin/sh\nexit 0\n')
    (legacy / 'mqtt_ros2_adapter').chmod(0o755)
    monkeypatch.setenv('PATH', f'{legacy}:{os.environ.get("PATH", "")}')

    def missing(name):
        raise LookupError(name)

    with pytest.raises(ConfigurationError, match='build the workspace'):
        planning.resolve_mqtt_executable('mqtt_transport', prefix_resolver=missing)
    # Configuration inspection must still work before building/provisioning.
    assert len(prepare_plan(ASSETS / 'config/mqtt.example.yaml', 'dev',
                            enable_mqtt=True, check_files=False)) == 2


@pytest.mark.parametrize('mode', [None, 0o644])
def test_incomplete_bundled_install_fails_preflight(tmp_path, mode):
    prefix = tmp_path
    if mode is not None:
        binary = prefix / 'lib/chaos_mqtt/mqtt_transport'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!/bin/sh\nexit 0\n')
        binary.chmod(mode)
    with pytest.raises(ConfigurationError, match='Installed MQTT executable'):
        planning.resolve_mqtt_executable('mqtt_transport',
                                         prefix_resolver=lambda name: str(prefix))


def test_explicit_external_transport_override_is_preserved(tmp_path):
    binary = tmp_path / 'custom_bridge'
    binary.write_text('#!/bin/sh\nexit 0\n')
    binary.chmod(0o755)

    def unused(name):
        pytest.fail('an explicit override must not use the package index')

    assert planning.resolve_mqtt_executable(str(binary), prefix_resolver=unused) == str(binary)
    with pytest.raises(ConfigurationError, match='not found or not executable'):
        planning.resolve_mqtt_executable(str(tmp_path / 'absent'))
