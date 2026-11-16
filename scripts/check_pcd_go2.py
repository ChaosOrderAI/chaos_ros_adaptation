#!/usr/bin/env python3
"""Check standing, ROS state and a small squat in the running Go2 scene.

Run with the simulator's ROS domain/environment. Resets Go2 before and after.
"""
import argparse
import json
import math
from pathlib import Path
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from nav_msgs.msg import Odometry
from sensor_msgs.msg import JointState
from diagnostic_msgs.msg import DiagnosticArray
from std_srvs.srv import Trigger


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rclpy.init()
    node = Node('pcd_go2_scene_check')
    latest, count = {}, [0]

    def joints(msg):
        latest['joints'] = msg
        count[0] += 1

    subscriptions = [
        node.create_subscription(JointState, '/sim/go2/joint_states', joints, qos_profile_sensor_data),
        node.create_subscription(Odometry, '/sim/go2/odom', lambda m: latest.update(odom=m), qos_profile_sensor_data),
        node.create_subscription(DiagnosticArray, '/sim/go2/diagnostics',
                                 lambda m: latest.update(diag={v.key: v.value for v in m.status[0].values}), 10)]
    publisher = node.create_publisher(JointState, '/sim/go2/joint_targets', 1)
    reset = node.create_client(Trigger, '/sim/go2/reset')

    def spin(duration):
        end = time.monotonic() + duration
        while time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=.02)

    def reset_robot():
        if not reset.wait_for_service(timeout_sec=10):
            raise RuntimeError('Go2 reset service unavailable')
        future = reset.call_async(Trigger.Request())
        rclpy.spin_until_future_complete(node, future, timeout_sec=10)
        if not future.done() or not future.result().success:
            raise RuntimeError('Go2 reset failed')

    def pose():
        p = latest['odom'].pose.pose
        upright = 1 - 2 * (p.orientation.x ** 2 + p.orientation.y ** 2)
        values = [p.position.x, p.position.y, p.position.z,
                  p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z]
        if not all(math.isfinite(x) for x in values) or upright < .97 or not .15 < p.position.z < .5:
            raise RuntimeError(f'Go2 is not standing upright: {values}')
        return {'position': values[:3], 'quaternion_wxyz': values[3:], 'upright_cosine': upright}

    try:
        deadline = time.monotonic() + 30
        while not {'joints', 'odom', 'diag'} <= latest.keys():
            if time.monotonic() > deadline:
                raise TimeoutError('No complete Go2 state received')
            spin(.1)
        reset_robot()
        spin(3)
        initial = pose()
        state = latest['joints']
        if len(state.name) != 12:
            raise RuntimeError('Expected 12 Go2 joints')
        baseline = list(state.position)
        start_count, start_time = count[0], time.monotonic()
        sim_start = float(latest['diag']['sim_time'])
        accepted_start = int(latest['diag']['accepted_count'])
        max_delta = 0.
        min_height = initial['position'][2]
        command = JointState()
        command.name = list(state.name)
        while (elapsed := time.monotonic() - start_time) < 4.:
            # Gentle symmetric crouch, then return: 0.06 rad hip / 0.12 rad knee.
            amplitude = .5 * (1 - math.cos(2 * math.pi * elapsed / 4.))
            command.position = [value + amplitude * (.06 if 'thigh' in name else -.12 if 'calf' in name else 0)
                                for name, value in zip(command.name, baseline)]
            publisher.publish(command)
            spin(.04)
            current = pose()
            min_height = min(min_height, current['position'][2])
            max_delta = max(max_delta, max(abs(a - b) for a, b in zip(latest['joints'].position, baseline)))
        spin(1.)
        seconds = time.monotonic() - start_time
        rate = (count[0] - start_count) / seconds
        sim_delta = float(latest['diag']['sim_time']) - sim_start
        accepted = int(latest['diag']['accepted_count']) - accepted_start
        if max_delta < .03 or accepted < 20 or sim_delta < 1 or not 35 < rate < 65:
            raise RuntimeError(f'Motion/state check failed: delta={max_delta}, accepted={accepted}, '
                               f'sim_delta={sim_delta}, rate={rate}')
        reset_robot()
        spin(3)
        result = {'passed': True, 'standing_before': initial, 'standing_after_reset': pose(),
                  'joint_count': 12, 'joint_state_hz': rate, 'sim_time_advanced_s': sim_delta,
                  'wall_sample_s': seconds, 'max_joint_motion_rad': max_delta,
                  'min_crouch_height_m': min_height, 'accepted_commands': accepted,
                  'diagnostics': latest['diag']}
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
    finally:
        try:
            if reset.service_is_ready():
                reset_robot()
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
