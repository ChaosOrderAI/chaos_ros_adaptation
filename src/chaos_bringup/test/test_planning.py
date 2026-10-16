from copy import deepcopy
from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError
from chaos_bringup.planning import prepare_plan


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
