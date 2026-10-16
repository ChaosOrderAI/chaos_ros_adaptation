"""Verify optional driver setup in the actual service entrypoint, using a fake ros2."""

import os
from pathlib import Path
import subprocess


SCRIPT = Path(__file__).resolve().parents[3] / 'deploy/start-edge.sh'


def environment(tmp_path):
    env = os.environ.copy()
    for key, label in [('CHAOS_ROS_SETUP', 'ros'), ('CHAOS_DRIVER_SETUP', 'driver'),
                       ('CHAOS_OVERLAY_SETUP', 'framework')]:
        path = tmp_path / f'{label}.bash'
        path.write_text(f'export CHAOS_TEST_ORDER="${{CHAOS_TEST_ORDER:-}}{label},"\n')
        env[key] = str(path)
    binary = tmp_path / 'ros2'
    binary.write_text('#!/bin/bash\nprintf "%s\\n" "$CHAOS_TEST_ORDER" "$@"\n')
    binary.chmod(0o755)
    inventory = tmp_path / 'asset.yaml'
    inventory.write_text('placeholder: true\n')
    env.update(PATH=f'{tmp_path}:{env["PATH"]}', CHAOS_INVENTORY=str(inventory),
               CHAOS_ASSET_ID='vehicle_001', CHAOS_ENABLE_MQTT='false', CHAOS_TEST_ORDER='')
    return env


def test_driver_environment_loaded_between_ros_and_framework(tmp_path):
    result = subprocess.run(['bash', str(SCRIPT), '--check'], env=environment(tmp_path),
                            text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines()[0] == 'ros,driver,framework,'
    assert 'dry_run:=true' in result.stdout


def test_driver_environment_remains_optional(tmp_path):
    env = environment(tmp_path)
    del env['CHAOS_DRIVER_SETUP']
    result = subprocess.run(['bash', str(SCRIPT), '--check'], env=env, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines()[0] == 'ros,framework,'


def test_configured_missing_driver_setup_fails_before_launch(tmp_path):
    env = environment(tmp_path)
    env['CHAOS_DRIVER_SETUP'] = str(tmp_path / 'missing.bash')
    result = subprocess.run(['bash', str(SCRIPT), '--check'], env=env, text=True, capture_output=True)
    assert result.returncode != 0
    assert 'not readable' in result.stderr
    assert result.stdout == ''
