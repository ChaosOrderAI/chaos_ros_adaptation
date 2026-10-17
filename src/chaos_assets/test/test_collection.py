from copy import deepcopy
from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError, load_inventory
from chaos_assets.collection import resolve_collection


ROOT = Path(__file__).resolve().parents[1]


def asset():
    return {
        'id': 'unit', 'namespace': '/robots/unit',
        'description': {'enabled': True, 'use_sim_time': False,
                        'joint_states_topic': '/robots/unit/joint_states'},
        'sensors': {
            'imu': {'enabled': True, 'streams': {
                'data': {'topic': '/robots/unit/sensors/imu/data'}}},
            'disabled': {'enabled': False, 'streams': {
                'data': {'topic': '/unused'}}},
        },
    }


def resolve(value, tmp_path, **kwargs):
    return resolve_collection(kwargs.get('model'), value, tmp_path / 'model', tmp_path,
                              kwargs.get('asset', asset()))


def test_existing_config_opt_out_and_demo_topics():
    assert all(row['data_collection'] is None for row in load_inventory(ROOT / 'config/demo.yaml').assets)
    robot = load_inventory(ROOT / 'config/data_collection.demo.yaml').assets[0]
    config = robot['data_collection']
    assert config['enabled'] and not config['time_sync']['require_sync']
    assert '/robots/demo_001/sensors/body_imu/data' in config['topics']
    assert '/tf_static' in config['topics']
    assert '/robots/demo_001/robot_description' in config['topics']


def test_defaults_merge_paths_and_selected_sensor_topics(tmp_path):
    model = {'enabled': True, 'output_dir': 'model-bags', 'time_sync': {'stable_samples': 5}}
    before = deepcopy(model)
    config = resolve({'max_bagfile_duration_s': 60}, tmp_path, model=model)
    assert model == before
    assert config['output_dir'] == str(tmp_path / 'model/model-bags')
    assert config['time_sync']['stable_samples'] == 5
    assert config['time_sync']['require_sync'] is True
    assert config['topics'] == [
        '/robots/unit/state', '/robots/unit/command_results', '/robots/unit/control_trace',
        '/robots/unit/sensors/imu/data',
        '/robots/unit/sensors/description', '/robots/unit/sensors/diagnostics', '/tf', '/tf_static',
        '/robots/unit/robot_description', '/robots/unit/joint_states', '/robots/unit/data_collection/status',
    ]
    assert resolve({'output_dir': 'instance-bags'}, tmp_path, model=model)['output_dir'] == str(tmp_path / 'instance-bags')
    assert not (tmp_path / 'instance-bags').exists()


def test_explicit_topics_scope_and_status_without_auto_topics(tmp_path):
    config = resolve({'topics': ['state', '/shared/points']}, tmp_path)
    assert config['topics'] == ['/robots/unit/state', '/shared/points', '/robots/unit/data_collection/status']


@pytest.mark.parametrize('value,match', [
    ({'typo': True}, 'unknown fields'), ({'enabled': 'true'}, 'boolean'),
    ({'storage_id': []}, 'storage_id'), ({'output_dir': ''}, 'directory'),
    ({'output_dir': 'https://example.invalid'}, 'directory'),
    ({'max_cache_size_mb': True}, 'integer'), ({'min_free_space_mb': 1}, 'reserve'),
    ({'max_bagfile_duration_s': 0, 'max_bagfile_size_mb': 0}, 'splitting'),
    ({'time_sync': None}, 'mapping'), ({'time_sync': {'typo': 1}}, 'unknown fields'),
    ({'time_sync': {'require_sync': 1}}, 'boolean'),
    ({'time_sync': {'stable_samples': False}}, 'integer'),
    ({'time_sync': {'max_uncertainty_ms': float('nan')}}, 'finite'),
    ({'time_sync': {'max_uncertainty_ms': 10 ** 500}}, 'finite'),
    ({'time_sync': {'poll_interval_s': .1, 'probe_timeout_s': .2}}, 'timeout'),
    ({'topics': []}, 'nonempty'), ({'topics': ['state', '/robots/unit/state']}, 'duplicate'),
    ({'topics': ['bad-topic']}, 'invalid'), ({'topics': ['/']}, 'invalid'),
    ({'topics': [123]}, 'strings'),
])
def test_invalid_policies_rejected_before_start(tmp_path, value, match):
    with pytest.raises(ConfigurationError, match=match):
        resolve(value, tmp_path)


def test_simulated_clock_is_not_silently_mixed_with_ntp(tmp_path):
    robot = asset()
    robot['description']['use_sim_time'] = True
    with pytest.raises(ConfigurationError, match='system time'):
        resolve({'enabled': True}, tmp_path, asset=robot)


def test_explicit_managed_sensor_simulation_time_is_rejected(tmp_path):
    robot = asset()
    robot['sensors']['imu']['driver'] = {'mode': 'node', 'parameters': {'use_sim_time': True}}
    with pytest.raises(ConfigurationError, match='managed sensor use_sim_time'):
        resolve({'enabled': True}, tmp_path, asset=robot)
    robot['sensors']['imu']['enabled'] = False
    assert resolve({'enabled': True}, tmp_path, asset=robot)['enabled']


@pytest.mark.parametrize('value', [None, [], False, 'yes'])
def test_null_and_non_mapping_inventory_policy_rejected(tmp_path, value):
    data = yaml.safe_load((ROOT / 'config/demo.yaml').read_text())
    data['assets'] = data['assets'][:1]
    data['assets'][0]['model_file'] = str(ROOT / 'models/lingao_vehicle/v1/model.yaml')
    data['assets'][0]['data_collection'] = value
    path = tmp_path / 'assets.yaml'
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ConfigurationError, match='mapping'):
        load_inventory(path)
