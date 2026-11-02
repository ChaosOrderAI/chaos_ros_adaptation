#!/usr/bin/env python3
"""Real MuJoCo/ROS checks in a localhost-only sim domain; no robot SDK or drivers.

Run after sourcing ROS and this workspace. Models must already be explicitly fetched.
"""
import argparse
import math
import os
from pathlib import Path
import signal
import statistics
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model-root', type=Path, default=Path('~/.cache/chaos/mujoco'))
    parser.add_argument('--domain', type=int, default=197)
    parser.add_argument('--sample-seconds', type=float, default=10.0)
    args = parser.parse_args()
    if not 1 <= args.domain <= 232 or args.sample_seconds < 5:
        parser.error('use domain 1..232 and at least 5 resource sampling seconds')
    os.environ['ROS_DOMAIN_ID'] = str(args.domain)
    os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
    os.environ.pop('ROS_LOCALHOST_ONLY', None)

    from ament_index_python.packages import get_package_prefix
    from diagnostic_msgs.msg import DiagnosticArray
    from nav_msgs.msg import Odometry
    import rclpy
    from rclpy.node import Node
    from sensor_msgs.msg import JointState
    from std_srvs.srv import SetBool, Trigger
    import json

    root = args.model_root.expanduser().resolve()
    paths = {'go2': 'go2/unitree_robots/go2/scene.xml', 't2': 't2/robots/T2/T2_31dof/T2_31dof.xml'}
    worlds = []
    for robot, relative in paths.items():
        model = root / relative
        assert model.is_file(), f'missing official model: {model}'
        worlds.append({'name': robot, 'namespace': '/sim/' + robot,
                       'model_path': str(model), 'kp': 40.0, 'kd': 2.0})
    executable = Path(get_package_prefix('chaos_sim')) / 'lib/chaos_sim/simulator'
    process = node = None
    with tempfile.TemporaryDirectory(prefix='chaos-mujoco-smoke-') as directory:
        os.environ['ROS_LOG_DIR'] = str(Path(directory) / 'ros-log')
        invalid_parameters = Path(directory) / 'invalid-rate.params.yaml'
        invalid_parameters.write_text(json.dumps({'chaos_simulator': {'ros__parameters': {
            'publish_rate_hz': 1e-300, 'worlds_json': '[]'}}}))
        rejected_start = subprocess.run([str(executable), '--ros-args', '--params-file',
            str(invalid_parameters)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=8)
        assert rejected_start.returncode != 0, 'extremely small publication rate must fail startup'
        assert 'invalid publish_rate_hz' in rejected_start.stdout, rejected_start.stdout
        assert '[0.1,1000]' in rejected_start.stdout, rejected_start.stdout
        print('PASS invalid publication rate rejected before model loading')
        parameters = Path(directory) / 'simulator.params.yaml'
        parameters.write_text(json.dumps({'chaos_simulator': {'ros__parameters': {
            'worlds_json': json.dumps(worlds)}}}))
        with (Path(directory) / 'simulator.log').open('w+') as log:
            try:
                process = subprocess.Popen([str(executable), '--ros-args', '--params-file',
                    str(parameters)], stdout=log, stderr=subprocess.STDOUT,
                    start_new_session=True)
                rclpy.init()
                node = Node('chaos_mujoco_smoke')
                diagnostics, joints, odometry = {}, {}, {}
                received = {robot: 0 for robot in paths}
                subscriptions, publishers, resets = [], {}, {}

                def got_joints(robot, message):
                    joints[robot] = message
                    received[robot] += 1

                for robot in paths:
                    ns = '/sim/' + robot
                    subscriptions.append(node.create_subscription(DiagnosticArray, ns + '/diagnostics',
                        lambda message, robot=robot: diagnostics.__setitem__(robot,
                            {item.key: item.value for item in message.status[0].values}), 10))
                    subscriptions.append(node.create_subscription(JointState, ns + '/joint_states',
                        lambda message, robot=robot: got_joints(robot, message), 10))
                    subscriptions.append(node.create_subscription(Odometry, ns + '/odom',
                        lambda message, robot=robot: odometry.__setitem__(robot, message), 10))
                    publishers[robot] = node.create_publisher(JointState, ns + '/joint_targets', 1)
                    resets[robot] = node.create_client(Trigger, ns + '/reset')
                pause = node.create_client(SetBool, '/chaos_sim/pause')

                def spin_for(duration):
                    deadline = time.monotonic() + duration
                    while time.monotonic() < deadline:
                        assert process.poll() is None, f'simulator exited: {process.returncode}'
                        rclpy.spin_once(node, timeout_sec=0.02)

                def wait(predicate, label, timeout=15):
                    deadline = time.monotonic() + timeout
                    while not predicate():
                        assert process.poll() is None, f'simulator exited waiting for {label}: {process.returncode}'
                        assert time.monotonic() < deadline, f'timed out: {label}'
                        rclpy.spin_once(node, timeout_sec=0.02)

                def call(client, request):
                    wait(lambda: client.service_is_ready(), client.srv_name)
                    future = client.call_async(request)
                    wait(future.done, client.srv_name + ' response')
                    response = future.result()
                    assert response.success, response.message

                def pause_sim(value):
                    request = SetBool.Request()
                    request.data = value
                    call(pause, request)
                    wait(lambda: all(diagnostics[r]['paused'] == str(value).lower() for r in paths), 'pause diagnostics')

                def count(robot, key):
                    return int(diagnostics[robot][key])

                wait(lambda: all(r in diagnostics and r in joints and r in odometry for r in paths), 'both model observations')
                for robot, joint_count in [('go2', 12), ('t2', 31)]:
                    assert len(joints[robot].name) == joint_count
                    assert len(set(joints[robot].name)) == joint_count
                    assert odometry[robot].header.frame_id == robot + '/odom'
                    assert odometry[robot].child_frame_id == robot + '/base_link'
                start_time = {r: float(diagnostics[r]['sim_time']) for r in paths}
                start_count = received.copy()
                spin_for(1.0)
                for robot in paths:
                    assert float(diagnostics[robot]['sim_time']) > start_time[robot] + 0.05, 'physics time must advance'
                    assert 15 <= received[robot] - start_count[robot] <= 80, 'state should publish near configured 50Hz'
                    assert all(math.isfinite(x) for x in joints[robot].position + joints[robot].velocity + joints[robot].effort)
                    pose = odometry[robot].pose.pose
                    assert all(math.isfinite(x) for x in [pose.position.x, pose.position.y, pose.position.z,
                        pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w])
                    assert abs(sum(x*x for x in [pose.orientation.x, pose.orientation.y,
                        pose.orientation.z, pose.orientation.w]) - 1) < 1e-4

                def command(robot, names, positions, accepted, velocity=None, effort=None):
                    message = JointState()
                    message.name, message.position = names, positions
                    if velocity is not None:
                        message.velocity = velocity
                    if effort is not None:
                        message.effort = effort
                    key = 'accepted_count' if accepted else 'rejected_count'
                    before = count(robot, key)
                    old_accepted = count(robot, 'accepted_count')
                    wait(lambda: publishers[robot].get_subscription_count() == 1, 'target subscriber')
                    publishers[robot].publish(message)
                    wait(lambda: count(robot, key) == before + 1, key)
                    if not accepted:
                        assert count(robot, 'accepted_count') == old_accepted

                for robot in paths:
                    name = joints[robot].name[0]
                    command(robot, ['unknown_joint'], [0.0], False)
                    command(robot, [name, name], [0.0, 0.0], False)
                    command(robot, [name], [], False)
                    command(robot, [], [], False)
                    command(robot, [name], [float('nan')], False)
                    command(robot, [name], [1e6], False)
                    command(robot, [name], [0.0], False, velocity=[0.0])
                    command(robot, [name], [0.0], False, effort=[0.0])
                    before_timeout = count(robot, 'timeout_count')
                    command(robot, [name], [0.0], True)
                    wait(lambda: count(robot, 'timeout_count') == before_timeout + 1, 'steady command timeout')
                    spin_for(0.6)
                    assert count(robot, 'timeout_count') == before_timeout + 1, 'timeout must hold only once'

                # A real target must move the actual joint, beyond a diagnostic acknowledgement.
                pause_sim(True)
                call(resets['go2'], Trigger.Request())
                wait(lambda: float(diagnostics['go2']['sim_time']) == 0 and
                     max(abs(x) for x in joints['go2'].velocity) < 1e-8, 'Go2 home for motion check')
                hip = joints['go2'].name[0]
                initial_position = joints['go2'].position[0]
                initial_effort = joints['go2'].effort[0]
                pause_sim(False)
                command_started = time.monotonic()
                command('go2', [hip], [0.25], True)
                wait(lambda: joints['go2'].position[0] - initial_position > 0.02 and
                     abs(joints['go2'].effort[0] - initial_effort) > 0.1,
                     'Go2 target produces joint motion and torque within 0.2s',
                     timeout=max(0.001, 0.2 - (time.monotonic() - command_started)))
                print(f"PASS accepted target moves Go2 hip by {joints['go2'].position[0] - initial_position:.3f} rad "
                      f"with effort {joints['go2'].effort[0]:.3f} Nm")

                # Prepare a fresh home, then test pause/resume WITHOUT any intervening reset.
                pause_sim(True)
                call(resets['go2'], Trigger.Request())
                wait(lambda: float(diagnostics['go2']['sim_time']) == 0 and
                     max(abs(x) for x in joints['go2'].velocity) < 1e-8, 'Go2 home for stale-target check')
                pause_sim(False)
                old_target = 0.7
                command('go2', [hip], [old_target], True)
                pause_sim(True)
                spin_for(0.05)  # Drain observations published before the pause response.
                paused_position = joints['go2'].position[0]
                assert abs(old_target - paused_position) > 0.15, 'pause must precede reaching the old target'
                paused_time = {r: float(diagnostics[r]['sim_time']) for r in paths}
                paused_timeouts = {r: count(r, 'timeout_count') for r in paths}
                for robot in paths:
                    command(robot, [joints[robot].name[0]], [0.0], False)
                spin_for(0.7)
                assert all(float(diagnostics[r]['sim_time']) == paused_time[r] for r in paths), 'paused worlds must freeze'
                pause_sim(False)
                spin_for(0.15)
                resumed_position = joints['go2'].position[0]
                assert abs(resumed_position - paused_position) < abs(resumed_position - old_target), (
                    f'resume pursues stale target: paused={paused_position}, resumed={resumed_position}, target={old_target}')
                spin_for(0.6)
                assert all(count(r, 'timeout_count') == paused_timeouts[r] for r in paths), (
                    'pause/resume without reset must clear active commands and prevent timeout replay')
                print(f'PASS pause/resume without reset holds hip near {paused_position:.3f} rad '
                      f'(resumed {resumed_position:.3f}, discarded target {old_target:.3f})')

                # Reset is a separate operation, after stale-command behavior has already passed.
                pause_sim(True)
                for robot in paths:
                    call(resets[robot], Trigger.Request())
                wait(lambda: all(float(diagnostics[r]['sim_time']) == 0 for r in paths), 'reset while paused')
                wait(lambda: all(max(abs(x) for x in joints[r].velocity) < 1e-8 for r in paths), 'reset clears velocities')
                pause_sim(False)
                wait(lambda: all(float(diagnostics[r]['sim_time']) > 0.1 for r in paths), 'resume after reset')

                samples = []
                deadline = time.monotonic() + args.sample_seconds
                while time.monotonic() < deadline:
                    spin_for(0.5)
                    fields = {}
                    for line in Path(f'/proc/{process.pid}/status').read_text().splitlines():
                        if line.startswith(('VmRSS:', 'Threads:')):
                            key, value, *_ = line.split()
                            fields[key.rstrip(':')] = int(value)
                    samples.append(fields)
                assert max(s['Threads'] for s in samples) <= samples[0]['Threads'] + 2, 'thread count keeps increasing'
                rss_growth = statistics.median(s['VmRSS'] for s in samples[-3:]) - statistics.median(s['VmRSS'] for s in samples[:3])
                assert rss_growth <= 16 * 1024, f'RSS grew by {rss_growth} KiB after warmup'
                print(f"PASS both official models: ROS states, sim time, target validation, timeout, pause/reset/resume; "
                      f"{len(samples)} samples/{args.sample_seconds:g}s, threads {min(s['Threads'] for s in samples)}.."
                      f"{max(s['Threads'] for s in samples)}, RSS {min(s['VmRSS'] for s in samples)}.."
                      f"{max(s['VmRSS'] for s in samples)} KiB (median growth {rss_growth:g} KiB)")
                os.killpg(process.pid, signal.SIGINT)
                assert process.wait(timeout=8) == 0, f'unclean SIGINT exit: {process.returncode}'
                print('PASS SIGINT exits cleanly')
            except Exception:
                log.flush()
                log.seek(0)
                print(log.read()[-10000:])
                raise
            finally:
                if process is not None and process.poll() is None:
                    os.killpg(process.pid, signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=3)
                if node is not None:
                    node.destroy_node()
                if rclpy.ok():
                    rclpy.shutdown()


if __name__ == '__main__':
    main()
