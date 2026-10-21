from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError, load_inventory


ROOT = Path(__file__).resolve().parents[1]
DEMO = ROOT / 'config/go2.demo.yaml'


def write_go2_inventory(tmp_path, **overrides):
    data = yaml.safe_load(DEMO.read_text())
    asset = data['assets'][0]
    asset['model_file'] = str((DEMO.parent / asset['model_file']).resolve())
    asset.update(overrides)
    path = tmp_path / 'go2.yaml'
    path.write_text(yaml.safe_dump(data))
    return path


def test_go2_demo_is_independent_and_real_does_not_advertise_navigation():
    default = load_inventory(ROOT / 'config/demo.yaml')
    assert [a['id'] for a in default.select('dev')] == ['vehicle_001', 'drone_001']

    mock = load_inventory(DEMO).select('dev')[0]
    real = load_inventory(DEMO, mode='real').select('dev')[0]
    assert mock['id'] == real['id'] == 'dog_001'
    assert mock['kind'] == real['kind'] == 'quadruped'
    assert mock['driver'] == {'plugin': 'mock', 'options': {'kind': 'quadruped'}}
    assert 'navigation.pose' in mock['capabilities']
    assert real['driver']['plugin'] == 'unitree_go2'
    assert set(real['capabilities']) == {'state', 'motion.velocity', 'motion.stop'}
    assert mock['config_digest'] != real['config_digest']
    assert real['limits']['command_timeout_s'] == 0.3
    assert real['limits']['state_timeout_s'] == 1.0


def test_go2_real_cannot_enable_mock_navigation(tmp_path):
    path = write_go2_inventory(tmp_path, mode='real', capabilities=['state', 'navigation.pose'])
    with pytest.raises(ConfigurationError, match='unsupported by real backend'):
        load_inventory(path)


def test_go2_instance_can_override_network_without_losing_driver_defaults(tmp_path):
    path = write_go2_inventory(tmp_path, mode='real', driver={'options': {
        'network_interface': 'enp2s0', 'cmd_vel_topic': '/test_go2/cmd_vel',
    }})
    asset = load_inventory(path).select('dev')[0]
    assert asset['driver']['options']['network_interface'] == 'enp2s0'
    assert asset['driver']['options']['cmd_vel_topic'] == '/test_go2/cmd_vel'
    assert asset['driver']['options']['dds_domain_id'] == 0
    assert asset['driver']['options']['sport_state_topic'] == 'rt/sportmodestate'
    assert asset['driver']['options']['velocity_frame'] == 'odom'
    assert asset['frames']['base'] == 'dog_001/base_link'


def test_go2_onboard_resolves_outside_source_and_keeps_domains_independent(tmp_path, monkeypatch):
    packages = pytest.importorskip('ament_index_python.packages')
    monkeypatch.setattr(packages, 'get_package_share_directory', lambda name: str(ROOT))
    path = tmp_path / 'asset.yaml'
    path.write_text((ROOT / 'config/go2.onboard.example.yaml').read_text())
    monkeypatch.chdir(tmp_path)
    inventory = load_inventory(path)
    asset = inventory.select('onboard')[0]
    assert len(inventory.assets) == 1
    assert inventory.hosts['onboard']['ros_domain_id'] == 55
    assert asset['model'] == 'unitree_go2'
    assert asset['mode'] == 'real'
    assert Path(asset['model_file']).is_file()
    assert asset['driver']['options']['dds_domain_id'] == 0
    assert asset['frames'] == {
        'base': 'dog_001/base_link', 'odom': 'dog_001/odom', 'map': 'dog_001/map',
    }
    assert asset['transport'] == 'platform'
    assert inventory.transports['platform']['executable'] == 'mqtt_transport'
