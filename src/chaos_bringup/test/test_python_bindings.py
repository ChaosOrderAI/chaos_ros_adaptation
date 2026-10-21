"""Tests for the thin Python binding of the native launch planner.

The binding must not contain business logic: every rejection has to come from
the native planner, and the launch glue must receive the same data as the
native command line tool.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

from chaos_assets import ConfigurationError
from chaos_bringup._chaos_bringup_core import (
    component_asset,
    plan_summary,
    prepare_plan,
    resolve_mqtt_executable,
)
from chaos_bringup.planning import prepare_edge_plan


def _share() -> Path:
    import ament_index_python

    return Path(ament_index_python.packages.get_package_share_directory('chaos_assets'))


def test_the_demo_plan_selects_two_assets():
    plan = prepare_plan(str(_share() / 'config/demo.yaml'), 'dev')
    assert len(plan) == 2
    assert len({item['asset']['namespace'] for item in plan}) == 2
    assert all(item['mqtt'] is None for item in plan)


def test_an_unknown_host_is_rejected_by_the_native_planner():
    with pytest.raises(ConfigurationError, match='unknown host_id'):
        prepare_plan(str(_share() / 'config/demo.yaml'), 'other')


def test_the_plan_is_immutable_despite_mutating_returned_data():
    plan = prepare_plan(str(_share() / 'config/demo.yaml'), 'dev')
    plan[0]['asset']['driver']['plugin'] = 'replaced'
    assert prepare_plan(str(_share() / 'config/demo.yaml'), 'dev')[0]['asset']['driver']['plugin'] == 'mock'


def test_the_summary_is_produced_by_the_native_planner():
    plan = prepare_plan(str(_share() / 'config/demo.yaml'), 'dev')
    summary = plan_summary(plan)
    assert len(summary) == 2
    assert summary[0]['id']
    assert summary[0]['mqtt'] is False


def test_the_component_asset_drops_the_expanded_description():
    plan = prepare_plan(str(_share() / 'config/description.demo.yaml'), 'dev')
    asset = plan[0]['asset']
    assert 'xml' in asset['description']
    component = component_asset(asset)
    assert 'xml' not in component['description']
    assert 'xml' in asset['description']


def test_the_edge_plan_rejects_a_mock_asset(tmp_path):
    # An edge deployment is bound to exactly one host and one enabled asset.
    # A mock asset of such a deployment is rejected, because the edge startup
    # never promotes a mock asset to the real mode.
    inventory = tmp_path / 'asset.yaml'
    inventory.write_text(yaml.safe_dump({
        'schema_version': 1,
        'hosts': {'onboard': {'ros_domain_id': 61}},
        'assets': [{'id': 'vehicle_001', 'host': 'onboard', 'mode': 'mock',
                    'model_file': str(_share() / 'models/lingao_vehicle/v1/model.yaml')}],
    }))
    with pytest.raises(ConfigurationError, match='real backend'):
        prepare_edge_plan(str(inventory), 'vehicle_001', enable_mqtt=False)


def test_the_edge_plan_rejects_a_development_inventory():
    # The developer inventory has two assets, so it is not an edge deployment.
    with pytest.raises(ConfigurationError, match='exactly one'):
        prepare_edge_plan(str(_share() / 'config/demo.yaml'), 'vehicle_001', enable_mqtt=False)


def test_the_mqtt_executable_of_a_missing_transport_is_rejected():
    with pytest.raises(ConfigurationError, match='not found or not executable'):
        resolve_mqtt_executable('/nonexistent/transport')


def test_the_native_command_produces_the_same_plan():
    import ament_index_python

    prefix = Path(ament_index_python.packages.get_package_prefix('chaos_bringup'))
    executable = prefix / 'lib' / 'chaos_bringup' / 'chaos_bringup_plan'
    if not executable.is_file():
        pytest.skip('the native planner executable is not installed')
    completed = subprocess.run(
        [str(executable), '--inventory', str(_share() / 'config/demo.yaml'), '--host', 'dev'],
        capture_output=True, text=True, check=True)
    native = json.loads(completed.stdout)
    binding = prepare_plan(str(_share() / 'config/demo.yaml'), 'dev')
    assert native == binding
