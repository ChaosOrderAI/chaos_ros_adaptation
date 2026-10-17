from copy import deepcopy
from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError
from chaos_bringup.planning import prepare_plan
from chaos_bringup import planning


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'


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
    data = yaml.safe_load((ASSETS / 'config/mqtt.example.yaml').read_text())
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


@pytest.mark.parametrize('configured_executable', [None, 'mqtt_transport', 'mqtt_ros2_adapter'])
def test_bundled_transport_is_selected_from_package_index_not_path(tmp_path, monkeypatch, configured_executable):
    prefix = tmp_path / 'installed'
    binary = prefix / 'lib/chaos_mqtt/mqtt_transport'
    binary.parent.mkdir(parents=True)
    binary.write_text('#!/bin/sh\nexit 0\n')
    binary.chmod(0o755)
    def package_prefix(name):
        assert name == 'chaos_mqtt', 'the former transport ROS package must not be queried'
        return str(prefix)
    monkeypatch.setattr(planning, 'get_package_prefix', package_prefix)
    # An unrelated legacy binary on PATH must not override the installed one.
    monkeypatch.setattr(planning.shutil, 'which', lambda name: '/legacy/mqtt_ros2_adapter')
    data = yaml.safe_load((ASSETS / 'config/mqtt.example.yaml').read_text())
    if configured_executable is not None:
        data['transports']['edge_mqtt']['executable'] = configured_executable
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


def test_missing_bundled_package_fails_without_legacy_path_fallback(monkeypatch):
    def missing(name):
        raise planning.PackageNotFoundError(name)
    monkeypatch.setattr(planning, 'get_package_prefix', missing)
    monkeypatch.setattr(planning.shutil, 'which', lambda name: '/legacy/mqtt_ros2_adapter')
    with pytest.raises(ConfigurationError, match='build the workspace'):
        planning.resolve_mqtt_executable()
    # Configuration inspection must still work before building/provisioning.
    assert len(prepare_plan(ASSETS / 'config/mqtt.example.yaml', 'dev',
                            enable_mqtt=True, check_files=False)) == 2


@pytest.mark.parametrize('mode', [None, 0o644])
def test_incomplete_bundled_install_fails_preflight(tmp_path, monkeypatch, mode):
    monkeypatch.setattr(planning, 'get_package_prefix', lambda name: str(tmp_path))
    if mode is not None:
        binary = tmp_path / 'lib/chaos_mqtt/mqtt_transport'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!/bin/sh\nexit 0\n')
        binary.chmod(mode)
    with pytest.raises(ConfigurationError, match='Installed MQTT executable'):
        planning.resolve_mqtt_executable()


def test_explicit_external_transport_override_is_preserved(tmp_path, monkeypatch):
    binary = tmp_path / 'custom_bridge'
    binary.write_text('#!/bin/sh\nexit 0\n')
    binary.chmod(0o755)
    monkeypatch.setattr(planning, 'get_package_prefix',
                        lambda name: pytest.fail('explicit override must not use package index'))
    assert planning.resolve_mqtt_executable(str(binary)) == str(binary)
    with pytest.raises(ConfigurationError, match='not found or not executable'):
        planning.resolve_mqtt_executable(str(tmp_path / 'absent'))
