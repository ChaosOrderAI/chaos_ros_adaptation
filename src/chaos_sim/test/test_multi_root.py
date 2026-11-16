"""Integration check: a multi-root model expands into one ROS instance per body.

Starts the compiled chaos_sim simulator with a two-root world and checks the
namespaces it publishes. Requires the built install tree and a ROS environment.
"""
import json
import os
from pathlib import Path
import subprocess
import time

import pytest

pytest.importorskip('rclpy')
import rclpy
from rclpy.node import Node


# The installed binary may be older than the one in the build tree; the CTest
# property below passes the just-built path explicitly.
import os
SIM = Path(os.environ.get('CHAOS_SIM_BINARY',
                          Path(__file__).resolve().parents[3] / 'install-sim/chaos_sim/lib/chaos_sim/simulator'))
FIXTURE = Path(__file__).parent / 'multi.xml'


@pytest.mark.skipif(not SIM.is_file(), reason='chaos_sim install tree not built')
def test_multi_root_expands_to_per_robot_namespaces(tmp_path):
    worlds = [{'name': 'alpha', 'namespace': '/cell/alpha',
               'model_path': str(FIXTURE), 'kp': 40.0, 'kd': 2.0}]
    params = tmp_path / 'params.yaml'
    params.write_text(json.dumps({'chaos_simulator': {'ros__parameters': {
        'worlds_json': json.dumps(worlds), 'discard_visual': True,
        'publish_rate_hz': 50.0, 'command_timeout_s': 0.5}}}))
    env = dict(os.environ, ROS_AUTOMATIC_DISCOVERY_RANGE='LOCALHOST')
    env.pop('ROS_LOCALHOST_ONLY', None)
    proc = subprocess.Popen([str(SIM), '--ros-args', '--params-file', str(params)],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    try:
        if not rclpy.ok():
            rclpy.init()
        node = Node('multi_root_test')
        deadline = time.monotonic() + 15
        topics = set()
        while time.monotonic() < deadline:
            topics = {t for t, _ in node.get_topic_names_and_types()}
            if '/cell/alpha/odom' in topics and '/cell/alpha/beta/odom' in topics:
                break
            rclpy.spin_once(node, timeout_sec=0.2)
        node.destroy_node()
        assert '/cell/alpha/odom' in topics, f'namespaces not expanded: {sorted(topics)}'
        assert '/cell/alpha/beta/odom' in topics
        assert '/sim/alpha/odom' not in topics and '/sim/beta/odom' not in topics
    finally:
        proc.terminate()
        proc.wait(timeout=10)
