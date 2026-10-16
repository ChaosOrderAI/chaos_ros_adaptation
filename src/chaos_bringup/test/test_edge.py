from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError
from chaos_bringup.planning import prepare_edge_plan


ASSETS = Path(__file__).resolve().parents[2] / 'chaos_assets'


def write_edge(tmp_path, mutate=lambda data: None):
    data = {
        'schema_version': 1,
        'hosts': {'onboard': {'ros_domain_id': 61}},
        'assets': [{'id': 'vehicle_001', 'host': 'onboard', 'mode': 'real',
                    'model_file': str(ASSETS / 'models/lingao_vehicle/v1/model.yaml')}],
    }
    mutate(data)
    path = tmp_path / 'asset.yaml'
    path.write_text(yaml.safe_dump(data))
    return path


def test_edge_selects_one_real_asset_without_promoting_mock(tmp_path):
    path = write_edge(tmp_path)
    plan = prepare_edge_plan(path, 'vehicle_001', enable_mqtt=False)
    assert len(plan) == 1
    assert plan[0]['asset']['driver']['plugin'] == 'lingao_ros2'
    assert plan[0]['asset']['host'] == 'onboard'
    assert plan[0]['mqtt'] is None


@pytest.mark.parametrize('mutate,asset_id,mqtt,match', [
    (lambda d: None, 'drone_001', False, 'asset_id must match'),
    (lambda d: None, '', False, 'asset_id must match'),
    (lambda d: d['assets'][0].update(mode='mock'), 'vehicle_001', False, 'real backend'),
    (lambda d: None, 'vehicle_001', True, 'no transport profile'),
    (lambda d: d['hosts'].update(remote={'ros_domain_id': 62}), 'vehicle_001', False, 'one host'),
    (lambda d: d['assets'][0].update(enabled=False), 'vehicle_001', False, 'exactly one'),
])
def test_invalid_deployment_never_returns_a_launch_plan(tmp_path, mutate, asset_id, mqtt, match):
    with pytest.raises(ConfigurationError, match=match):
        prepare_edge_plan(write_edge(tmp_path, mutate), asset_id, enable_mqtt=mqtt)


def test_development_multi_robot_inventory_is_not_an_edge_deployment():
    with pytest.raises(ConfigurationError, match='exactly one'):
        prepare_edge_plan(ASSETS / 'config/demo.yaml', 'vehicle_001', enable_mqtt=False)


def test_edge_rejects_working_directory_dependent_inventory():
    with pytest.raises(ConfigurationError, match='absolute path'):
        prepare_edge_plan('asset.yaml', 'vehicle_001', enable_mqtt=False)
