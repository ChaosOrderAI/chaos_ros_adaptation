from copy import deepcopy
from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError, load_inventory


ROOT = Path(__file__).resolve().parents[1]


def write_inventory(tmp_path, mutate):
    data = yaml.safe_load((ROOT / 'config/demo.yaml').read_text())
    for asset in data['assets']:
        asset['model_file'] = str((ROOT / 'config' / asset['model_file']).resolve())
    mutate(data)
    path = tmp_path / 'assets.yaml'
    path.write_text(yaml.safe_dump(data))
    return path


def test_demo_and_real_capabilities():
    demo = load_inventory(ROOT / 'config/demo.yaml')
    assert len(demo.select('dev')) == 2
    assert all(a['driver']['plugin'] == 'mock' for a in demo.assets)
    real = load_inventory(ROOT / 'config/demo.yaml', mode='real')
    drone = next(a for a in real.assets if a['kind'] == 'uav')
    assert drone['capabilities'] == ['state']
    assert drone['driver']['plugin'] == 'dji_psdk_legacy'
    assert demo.assets[1]['config_digest'] != real.assets[1]['config_digest']


@pytest.mark.parametrize('mutate,match', [
    (lambda d: d['assets'].append(deepcopy(d['assets'][0])), 'duplicate asset'),
    (lambda d: d['assets'][0].update(namespace='/bad-name'), 'namespace'),
    (lambda d: d['assets'][0].update(host='missing'), 'unknown host'),
    (lambda d: d['assets'][0].update(limits={'linear_mps': float('nan')}), 'finite'),
    (lambda d: d['assets'][0].update(limits={'linear_mps': True}), 'finite'),
    (lambda d: d['assets'][0].update(limits={'linear_mps': -1}), 'finite'),
    (lambda d: d['assets'][0].update(limts={}), 'unknown fields'),
    (lambda d: d['assets'][0].update(capabilities=['state', 'flight.takeoff']), 'unsupported'),
    (lambda d: d['hosts']['dev'].update(ros_domain_id=True), 'integer'),
    (lambda d: d['assets'][0].update(transport='missing'), 'unknown transport'),
    (lambda d: d['assets'][0].update(enabled='false'), 'boolean'),
])
def test_reject_bad_inventory(tmp_path, mutate, match):
    with pytest.raises(ConfigurationError, match=match):
        load_inventory(write_inventory(tmp_path, mutate))


def test_duplicate_yaml_keys_fail(tmp_path):
    path = tmp_path / 'duplicate.yaml'
    path.write_text('schema_version: 1\nschema_version: 1\n')
    with pytest.raises(ConfigurationError, match='duplicate YAML key'):
        load_inventory(path)


def test_per_host_filter_and_disabled_assets(tmp_path):
    def mutate(data):
        data['assets'][1]['enabled'] = False
    inventory = load_inventory(write_inventory(tmp_path, mutate))
    assert [a['id'] for a in inventory.select('dev')] == ['vehicle_001']
    with pytest.raises(ConfigurationError, match='unavailable'):
        inventory.select('dev', ['drone_001'])
    with pytest.raises(ConfigurationError, match='unknown host'):
        inventory.select('other')


def test_instance_options_and_paths_are_independent_of_cwd(tmp_path, monkeypatch):
    def mutate(data):
        data['assets'][0]['driver'] = {'options': {'odom_topic': '/custom/odom'}}
    path = write_inventory(tmp_path, mutate)
    monkeypatch.chdir(tmp_path)
    result = load_inventory(path, mode='real')
    assert result.assets[0]['driver']['options']['odom_topic'] == '/custom/odom'
    assert result.assets[0]['driver']['options']['cmd_vel_topic'] == '/cmd_vel'


def test_select_returns_detached_data():
    inventory = load_inventory(ROOT / 'config/demo.yaml')
    selected = inventory.select('dev')
    selected[0]['limits']['linear_mps'] = 100
    assert inventory.assets[0]['limits']['linear_mps'] == 0.5


def test_onboard_config_can_be_copied_outside_workspace(tmp_path, monkeypatch):
    packages = pytest.importorskip('ament_index_python.packages')
    monkeypatch.setattr(packages, 'get_package_share_directory', lambda name: str(ROOT))
    path = tmp_path / 'asset.yaml'
    path.write_text((ROOT / 'config/vehicle.onboard.example.yaml').read_text())
    monkeypatch.chdir(tmp_path)
    inventory = load_inventory(path)
    asset = inventory.select('onboard')[0]
    assert asset['model'] == 'lingao_vehicle'
    assert asset['mode'] == 'real'
    assert Path(asset['model_file']).is_file()


@pytest.mark.parametrize('uri', [
    'package://chaos_assets/../outside.yaml',
    'package://chaos_assets/',
    'package://chaos_assets/model.yaml?override=true',
    'package://invalid-package/model.yaml',
    'package://[invalid/model.yaml',
])
def test_package_uri_rejects_invalid_or_escaping_paths(tmp_path, uri):
    with pytest.raises(ConfigurationError, match='invalid package resource URI'):
        load_inventory(write_inventory(tmp_path, lambda d: d['assets'][0].update(model_file=uri)))
