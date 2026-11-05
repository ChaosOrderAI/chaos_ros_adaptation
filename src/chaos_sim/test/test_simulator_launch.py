import importlib.util
import json
from pathlib import Path

import pytest


LAUNCH_PATH = Path(__file__).parents[1] / 'launch' / 'simulator.launch.py'
SPEC = importlib.util.spec_from_file_location('chaos_simulator_launch', LAUNCH_PATH)
LAUNCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LAUNCH)


def model_root(tmp_path):
    for relative in LAUNCH.MODELS.values():
        model = tmp_path / relative
        model.parent.mkdir(parents=True, exist_ok=True)
        model.touch()
    return tmp_path


def test_legacy_robot_names_remain_unchanged(tmp_path):
    worlds = LAUNCH.make_worlds('go2,t2', model_root(tmp_path))
    assert [world['name'] for world in worlds] == ['go2', 't2']
    assert [world['namespace'] for world in worlds] == ['/sim/go2', '/sim/t2']


def test_repeated_models_get_stable_unique_names(tmp_path):
    worlds = LAUNCH.make_worlds('go2,go2,t2,go2,t2', model_root(tmp_path))
    assert [world['name'] for world in worlds] == ['go2_1', 'go2_2', 't2_1', 'go2_3', 't2_2']
    assert len({world['namespace'] for world in worlds}) == len(worlds)
    assert worlds[0]['model_path'] == worlds[1]['model_path'] == worlds[3]['model_path']
    json.dumps(worlds)


def test_invalid_or_excessive_robot_lists_are_rejected(tmp_path):
    root = model_root(tmp_path)
    for robots in ('', 'go2,', 'go2,unknown', 'go2, go2', ','.join(['go2'] * 33)):
        with pytest.raises(ValueError):
            LAUNCH.make_worlds(robots, root)


def test_scene_options_are_applied_to_every_robot(tmp_path):
    scene = {'scene_path': '/maps/room.ply', 'scene_voxel_size': 0.2,
             'scene_point_radius': 0.04, 'scene_max_geoms': 5000, 'scene_collision': False}
    worlds = LAUNCH.make_worlds('go2,go2', model_root(tmp_path), scene)
    assert len(worlds) == 2
    assert all(all(world[key] == value for key, value in scene.items()) for world in worlds)
    json.dumps(worlds)


def test_models_must_be_prepared_explicitly(tmp_path):
    with pytest.raises(FileNotFoundError):
        LAUNCH.make_worlds('go2,go2', tmp_path)
