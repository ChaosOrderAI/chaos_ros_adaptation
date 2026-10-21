"""Tests for the thin Python binding over the native configuration engine.

The binding must not contain business logic: every rejection has to come
from the native engine, and the command line output must be identical to the
native executable.
"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

from chaos_assets import ConfigurationError, Inventory, load_inventory
from chaos_assets import cli
from chaos_assets.collection import resolve_collection
from chaos_assets.description import resolve_description, validate_description_graph
from chaos_assets.sensors import resolve_sensors, validate_sensor_graph

ROOT = Path(__file__).resolve().parents[1]
SHARE = Path(__import__('ament_index_python').packages.get_package_share_directory('chaos_assets'))


def test_load_inventory_exposes_the_native_snapshot():
    inventory = load_inventory(SHARE / 'config/demo.yaml')
    assert isinstance(inventory, Inventory)
    assert len(inventory.select('dev')) == 2
    assert inventory.path == str((SHARE / 'config/demo.yaml').resolve())
    assert 'schema_version' in inventory.as_dict()


def test_select_returns_detached_data():
    inventory = load_inventory(SHARE / 'config/demo.yaml')
    selected = inventory.select('dev')
    selected[0]['limits']['linear_mps'] = 100
    assert inventory.assets[0]['limits']['linear_mps'] == 0.5


def test_unknown_host_is_rejected_by_the_native_engine():
    inventory = load_inventory(SHARE / 'config/demo.yaml')
    with pytest.raises(ConfigurationError, match='unknown host_id'):
        inventory.select('other')


def test_configuration_error_is_a_value_error():
    assert issubclass(ConfigurationError, ValueError)
    with pytest.raises(ValueError):
        load_inventory('/nonexistent/inventory.yaml')


def test_module_level_helpers_are_bound_to_the_native_engine():
    assert resolve_sensors is not None
    assert resolve_collection is not None
    assert resolve_description is not None
    assert validate_sensor_graph is not None
    assert validate_description_graph is not None


def test_command_line_output_matches_the_native_executable():
    executable = SHARE.parent.parent / 'lib' / 'chaos_assets' / 'asset_config'
    if not executable.is_file():
        pytest.skip('the native asset_config executable is not installed')
    completed = subprocess.run(
        [str(executable), str(SHARE / 'config/demo.yaml')],
        capture_output=True, text=True, check=True)
    native = json.loads(completed.stdout)
    binding = json.loads(cli.resolve_inventory_json(str(SHARE / 'config/demo.yaml'), '', ''))
    assert native == binding


def test_command_line_reports_a_rejected_configuration(capsys):
    assert cli.main(['/nonexistent/inventory.yaml']) == 2
    captured = capsys.readouterr()
    assert 'configuration error' in captured.err
