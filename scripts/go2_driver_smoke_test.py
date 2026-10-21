#!/usr/bin/env python3
"""Exercise the native Go2 driver and its SDK boundary.

The pre-migration version of this script drove the Python Go2 driver with an
in-process fake SDK. The Python driver no longer exists, so the checks are
re-targeted at the native implementation and at its hardware-free interfaces.
No behaviour assertion was dropped; each one moved to the native surface
that now owns it:

* the vendor SDK must never be imported -> the native binary must not link
  a Python SDK and the capability query must answer without hardware;
* a real-mode driver without the SDK must refuse to start -> the explicit
  refusal of the native driver is asserted;
* the fake-SDK path (telemetry, command write, telemetry loss, an illegal
  sample, an SDK write fault, the shutdown handshake) -> the native
  ROS-level fake-SDK test owns it and is executed here.

Requires a built and sourced chaos_ws. Localhost ROS sockets only; no vendor
DDS participant, no robot connection, no MQTT and no hardware.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def find_native_driver():
    """Returns the installed native go2_driver executable.

    The ament index is the only supported lookup: the file must be the
    native ELF, never a Python console script.
    """
    from ament_index_python.packages import get_package_prefix
    prefix = Path(get_package_prefix('chaos_runtime'))
    candidate = prefix / 'lib' / 'chaos_runtime' / 'go2_driver'
    if not candidate.exists():
        raise AssertionError(f'The native go2_driver is missing: {candidate}')
    with candidate.open('rb') as handle:
        if handle.read(4) != b'\x7fELF':
            raise AssertionError(
                f'{candidate} is not an ELF binary; the Go2 driver must be the '
                'native executable, not a Python console script')
    return candidate


def find_native_fake_sdk_test():
    """Returns the native ROS-level fake-SDK test binary.

    The test binary lives in the colcon build tree, not in the install tree.
    The build tree is not derivable from the install prefix in general
    (colcon allows independent --build-base/--install-base), so the candidates
    are enumerated: an explicit override first, then the conventional
    ``<workspace>/build`` layout, then any sibling ``*-build`` tree next to
    the install prefix.
    """
    from ament_index_python.packages import get_package_prefix
    prefix = Path(get_package_prefix('chaos_runtime'))
    workspace = prefix.parent.parent
    roots = []
    override = os.environ.get('CHAOS_RUNTIME_BUILD_BASE')
    if override:
        roots.append(Path(override))
    roots.append(workspace / 'build')
    roots.extend(sorted(workspace.glob('*-build')))
    roots.append(workspace)
    for root in roots:
        candidate = root / 'chaos_runtime' / 'test_go2_driver_ros_cpp'
        if candidate.exists():
            return candidate
    searched = ', '.join(str(root) for root in roots)
    raise AssertionError(
        'The native fake-SDK test binary was not found. Searched: '
        f'{searched}. Build the package with -DBUILD_TESTING=ON, or point '
        'CHAOS_RUNTIME_BUILD_BASE at the build tree.')


def assert_vendor_sdk_absent(binary):
    """The native driver must not depend on a Python vendor SDK."""
    completed = subprocess.run(['ldd', str(binary)], capture_output=True,
                               text=True)
    assert 'unitree_sdk2py' not in completed.stdout, \
        'The native driver must never link the Python Unitree SDK'
    assert 'unitree_sdk2py' not in completed.stderr, \
        'The native driver must never link the Python Unitree SDK'


def run_capability_query(driver):
    """The capability query answers without ROS, hardware or the SDK runtime."""
    completed = subprocess.run([str(driver), '--build-capabilities'],
                               capture_output=True, text=True, timeout=30)
    assert completed.returncode == 0, \
        f'--build-capabilities failed: {completed.stderr}'
    payload = json.loads(completed.stdout)
    assert set(payload) == {'unitree_sdk2'}, \
        f'Unexpected capability payload: {payload}'
    assert isinstance(payload['unitree_sdk2'], bool)
    print(f"PASS: the native driver reports its build capability: {payload}")
    return payload['unitree_sdk2']


def run_real_mode_refusal(driver):
    """A real-mode driver without the SDK must refuse to start.

    This is the native form of the old "the fake SDK is never mistaken for
    hardware" check: the driver either has the real backend, or it refuses
    to run.
    """
    asset = {
        'id': 'go2_smoke', 'model': 'unitree-go2', 'kind': 'quadruped',
        'mode': 'real', 'namespace': '/go2_smoke', 'ros_domain_id': 185,
        'frames': {'base': 'base_link', 'odom': 'odom', 'map': 'map'},
        'capabilities': ['state', 'motion.velocity', 'motion.stop'],
        'limits': {'linear_mps': 1.0, 'angular_rps': 1.0,
                   'command_timeout_s': 0.5, 'state_timeout_s': 2.0,
                   'lease_max_s': 30.0, 'action_timeout_s': 120.0},
        'driver': {'plugin': 'unitree_go2', 'options': {
            'odom_topic': '/go2_smoke/odom', 'cmd_vel_topic': '/go2_smoke/cmd_vel',
            'network_interface': 'lo', 'dds_domain_id': 185,
            'sport_state_topic': 'rt/sportmodestate', 'velocity_frame': 'body'}},
    }
    with tempfile.TemporaryDirectory(prefix='chaos-go2-smoke-') as directory:
        parameter = Path(directory) / 'asset.yaml'
        parameter.write_text(
            '/**:\n  ros__parameters:\n    asset_json: ' +
            json.dumps(json.dumps(asset)) + '\n')
        environment = dict(os.environ)
        environment['ROS_DOMAIN_ID'] = '185'
        environment['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
        environment.pop('ROS_LOCALHOST_ONLY', None)
        completed = subprocess.run(
            [str(driver), '--ros-args', '--params-file', str(parameter)],
            capture_output=True, text=True, timeout=30, env=environment)
    assert completed.returncode != 0, \
        'A real-mode driver without the SDK backend must not start'
    assert 'Unitree SDK2 support' in (completed.stderr + completed.stdout), \
        f'Unexpected refusal message: {completed.stderr}'
    print('PASS: the native driver refuses real mode without the SDK backend')


def run_native_fake_sdk_test():
    """The native fake-SDK path: telemetry, command, expiry, fault, shutdown.

    This is the native replacement of the in-process fake SDK of the
    pre-migration script. The test drives the real driver node over real ROS
    messages with an injected fake backend; no hardware and no vendor DDS
    participant are involved.
    """
    binary = find_native_fake_sdk_test()
    environment = dict(os.environ)
    environment['ROS_DOMAIN_ID'] = '185'
    environment['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    environment.pop('ROS_LOCALHOST_ONLY', None)
    completed = subprocess.run([str(binary)], capture_output=True, text=True,
                               timeout=300, env=environment)
    assert completed.returncode == 0, \
        f'The native fake-SDK test failed:\n{completed.stdout}\n{completed.stderr}'
    print('PASS: the native fake-SDK ROS test passed '
          '(telemetry, command write, telemetry expiry, illegal sample, '
          'SDK write fault, shutdown handshake)')


def run():
    driver = find_native_driver()
    assert_vendor_sdk_absent(driver)
    run_capability_query(driver)
    run_real_mode_refusal(driver)
    run_native_fake_sdk_test()
    print('All Go2 driver checks passed: native driver, native interfaces, '
          'no hardware.')


if __name__ == '__main__':
    try:
        run()
    except AssertionError as failure:
        print(f'FAIL: {failure}', file=sys.stderr)
        raise SystemExit(1)
