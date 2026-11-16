#!/usr/bin/env python3
"""Verify real Go2 forward, watchdog stop, turning and reverse motion over ROS.

Run in the simulator/controller ROS domain. The robot is reset before and after
the check. Use --forward-only for the first integration pass.
"""
import argparse
import json
import math
from pathlib import Path
import time

import rclpy
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import Twist
from nav_msgs.msg import Odometry
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import JointState
from std_srvs.srv import Trigger


def stamp(message):
    return message.header.stamp.sec * 1_000_000_000 + message.header.stamp.nanosec


def angle_difference(end, start):
    return math.atan2(math.sin(end - start), math.cos(end - start))


class LocomotionCheck:
    def __init__(self, namespace):
        self.node = Node('go2_locomotion_check')
        self.started = time.monotonic()
        self.latest = {}
        self.received_at = {}
        self.joint_count = 0
        self.samples = []
        self.stages = []
        self.namespace = namespace.rstrip('/')
        self.publisher = self.node.create_publisher(Twist, self.namespace + '/cmd_vel', 1)
        self.reset = self.node.create_client(Trigger, self.namespace + '/reset')
        self.subscriptions = [
            self.node.create_subscription(Odometry, self.namespace + '/odom',
                                          lambda msg: self.receive('odom', msg), qos_profile_sensor_data),
            self.node.create_subscription(JointState, self.namespace + '/joint_states',
                                          lambda msg: self.receive('joints', msg), qos_profile_sensor_data),
            self.node.create_subscription(DiagnosticArray, self.namespace + '/diagnostics',
                                          lambda msg: self.receive('diag', msg), 1),
        ]

    def receive(self, name, message):
        self.latest[name] = message
        self.received_at[name] = time.monotonic()
        if name == 'joints':
            self.joint_count += 1

    def spin(self, duration):
        deadline = time.monotonic() + duration
        while rclpy.ok() and time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=min(.01, max(0., deadline - time.monotonic())))

    def wait_ready(self):
        deadline = time.monotonic() + 30
        while not ({'odom', 'joints', 'diag'} <= self.latest.keys()
                   and self.publisher.get_subscription_count() > 0):
            if time.monotonic() >= deadline:
                raise TimeoutError('Need Go2 state and a locomotion controller subscribed to cmd_vel')
            self.spin(.05)

    def reset_robot(self):
        if not self.reset.wait_for_service(timeout_sec=10):
            raise TimeoutError('Go2 reset service unavailable')
        future = self.reset.call_async(Trigger.Request())
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=10)
        if not future.done():
            raise TimeoutError('Go2 reset response timed out')
        response = future.result()
        if not response.success:
            raise RuntimeError('Go2 reset failed: ' + response.message)
        # Drain observations queued before the reset response.
        self.spin(.1)

    def send(self, forward=0., yaw=0., lateral=0.):
        message = Twist()
        message.linear.x = float(forward)
        message.linear.y = float(lateral)
        message.angular.z = float(yaw)
        self.publisher.publish(message)

    def observe(self, stage, validate=True):
        now = time.monotonic()
        if not {'odom', 'joints', 'diag'} <= self.latest.keys():
            raise RuntimeError('Incomplete Go2 feedback')
        age = {name: now - self.received_at[name] for name in ('odom', 'joints', 'diag')}
        if max(age.values()) > .2:
            raise RuntimeError('Go2 feedback stale for over 0.2 s: ' + str(age))
        odom, joints, diagnostics = (self.latest[name] for name in ('odom', 'joints', 'diag'))
        if abs(stamp(odom) - stamp(joints)) > 50_000_000:
            raise RuntimeError('Odometry and joints are more than 50 ms apart')
        if len(joints.name) != 12 or len(set(joints.name)) != 12 or len(joints.position) != 12:
            raise RuntimeError('Expected 12 distinct Go2 joint observations')
        p, q, twist = odom.pose.pose.position, odom.pose.pose.orientation, odom.twist.twist
        position = [p.x, p.y, p.z]
        quaternion = [q.w, q.x, q.y, q.z]
        velocity = [twist.linear.x, twist.linear.y, twist.linear.z]
        angular = [twist.angular.x, twist.angular.y, twist.angular.z]
        values = (position + quaternion + velocity + angular + list(joints.position)
                  + list(joints.velocity) + list(joints.effort))
        if not all(math.isfinite(value) for value in values):
            raise RuntimeError('Go2 observation contains non-finite values')
        if abs(sum(value * value for value in quaternion) - 1.) > .02:
            raise RuntimeError('Go2 orientation quaternion is not normalized')
        upright = 1. - 2. * (q.x * q.x + q.y * q.y)
        if validate and (upright < .8 or p.z < .15):
            raise RuntimeError(f'Go2 fell during {stage}: height={p.z:.3f}, upright={upright:.3f}')
        if not diagnostics.status:
            raise RuntimeError('Missing simulator diagnostics')
        diag = {entry.key: entry.value for entry in diagnostics.status[0].values}
        if validate and diag.get('paused') != 'false':
            raise RuntimeError('Simulator is paused during locomotion verification')
        sim_time = float(diag['sim_time'])
        if not math.isfinite(sim_time):
            raise RuntimeError('Simulator time is non-finite')
        observation = {
            'stage': stage, 'wall_elapsed_s': now - self.started,
            'sim_time_s': sim_time,
            'position': position, 'quaternion_wxyz': quaternion,
            'yaw_rad': math.atan2(2. * (q.w * q.z + q.x * q.y), 1. - 2. * (q.y * q.y + q.z * q.z)),
            'upright_cosine': upright, 'body_velocity': velocity, 'body_angular_velocity': angular,
            'feedback_age_s': age,
            'accepted_count': int(diag['accepted_count']), 'rejected_count': int(diag['rejected_count']),
            'timeout_count': int(diag['timeout_count']), 'dropped_ticks': int(diag['dropped_ticks']),
        }
        self.samples.append(observation)
        return observation

    def stage(self, name, duration, forward=0., yaw=0., silence=False, lateral=0.):
        start = self.observe(name)
        start_joint_count = self.joint_count
        started = time.monotonic()
        stage_samples = []
        previous_sim_time = start['sim_time_s']
        next_publish = started
        while (now := time.monotonic()) < started + duration:
            if now >= next_publish:
                if not silence:
                    self.send(forward, yaw, lateral)
                next_publish = now + .05
                current = self.observe(name)
                if current['sim_time_s'] < previous_sim_time - 1e-6:
                    raise RuntimeError('Simulator reset unexpectedly during ' + name)
                previous_sim_time = current['sim_time_s']
                stage_samples.append(current)
            self.spin(min(.01, max(0., next_publish - time.monotonic())))
        end = self.observe(name)
        delta = [end['position'][i] - start['position'][i] for i in range(3)]
        heading = start['yaw_rad']
        elapsed = time.monotonic() - started
        tail = [sample for sample in stage_samples if sample['wall_elapsed_s'] >= end['wall_elapsed_s'] - .5]
        metric = {
            'name': name, 'command': None if silence else {'forward_m_s': forward, 'lateral_m_s': lateral, 'yaw_rad_s': yaw},
            'wall_duration_s': elapsed, 'sim_duration_s': end['sim_time_s'] - start['sim_time_s'],
            'joint_state_hz': (self.joint_count - start_joint_count) / elapsed,
            'start_position': start['position'], 'end_position': end['position'],
            'forward_displacement_m': delta[0] * math.cos(heading) + delta[1] * math.sin(heading),
            'lateral_displacement_m': -delta[0] * math.sin(heading) + delta[1] * math.cos(heading),
            'yaw_change_rad': angle_difference(end['yaw_rad'], start['yaw_rad']),
            'min_height_m': min(sample['position'][2] for sample in stage_samples),
            'min_upright_cosine': min(sample['upright_cosine'] for sample in stage_samples),
            'tail_mean_planar_speed_m_s': sum(math.hypot(*sample['body_velocity'][:2]) for sample in tail) / len(tail),
            'tail_mean_abs_yaw_rate_rad_s': sum(abs(sample['body_angular_velocity'][2]) for sample in tail) / len(tail),
            'accepted_delta': end['accepted_count'] - start['accepted_count'],
            'rejected_delta': end['rejected_count'] - start['rejected_count'],
            'joint_target_timeout_delta': end['timeout_count'] - start['timeout_count'],
            'dropped_ticks_delta': end['dropped_ticks'] - start['dropped_ticks'],
        }
        self.stages.append(metric)
        print(json.dumps(metric), flush=True)
        if metric['rejected_delta'] or metric['joint_target_timeout_delta']:
            raise RuntimeError('Rejected or timed-out joint targets during ' + name)
        if metric['sim_duration_s'] < .7 * elapsed or metric['joint_state_hz'] < 35:
            raise RuntimeError('Simulation/feedback is too slow during ' + name)
        return metric

    def stop_check(self, name):
        # Deliberately stop publishing: the controller should expire cmd_vel
        # after 0.3 s, decelerate, and keep its balance policy running.
        metric = self.stage(name, 2., silence=True)
        if metric['tail_mean_planar_speed_m_s'] > .15 or metric['tail_mean_abs_yaw_rate_rad_s'] > .20:
            raise RuntimeError('Command watchdog did not stop Go2 during ' + name)

    def cleanup(self):
        try:
            for _ in range(10):
                self.send()
                self.spin(.05)
            self.reset_robot()
        finally:
            self.node.destroy_node()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('generated/pcd0_go2/locomotion_validation.json'))
    parser.add_argument('--namespace', default='/sim/go2')
    parser.add_argument('--forward-only', action='store_true')
    args = parser.parse_args()
    result = {'passed': False, 'forward_only': args.forward_only, 'stages': [], 'samples': []}
    rclpy.init()
    check = LocomotionCheck(args.namespace)
    error = None
    try:
        check.wait_ready()
        check.send()
        check.reset_robot()
        check.stage('settle', 3.)
        initial = check.observe('initial')
        forward = check.stage('forward', 5., forward=.3)
        if forward['forward_displacement_m'] < .5:
            raise RuntimeError('Forward travel was less than 0.5 m')
        if abs(forward['lateral_displacement_m']) > .35:
            raise RuntimeError('Forward motion drifted sideways by over 0.35 m')
        check.stop_check('forward_watchdog_stop')
        if not args.forward_only:
            turn = check.stage('turn_left', 3., yaw=.35)
            if turn['yaw_change_rad'] < .5:
                raise RuntimeError('Left turn was less than 0.5 rad')
            back = check.stage('turn_back', 3., yaw=-.35)
            if back['yaw_change_rad'] > -.5:
                raise RuntimeError('Return turn was less than 0.5 rad')
            check.stop_check('turn_watchdog_stop')
            before_reverse = check.observe('before_reverse')
            if abs(angle_difference(before_reverse['yaw_rad'], initial['yaw_rad'])) > .35:
                raise RuntimeError('Heading did not return within 0.35 rad of the initial corridor')
            reverse = check.stage('reverse', 3., forward=-.25)
            if reverse['forward_displacement_m'] > -.25:
                raise RuntimeError('Reverse travel was less than 0.25 m')
            check.stop_check('reverse_watchdog_stop')
        final = check.observe('final')
        delta = [final['position'][i] - initial['position'][i] for i in range(3)]
        result.update(passed=True, initial=initial, final=final,
                      net_displacement_xyz_m=delta, net_planar_displacement_m=math.hypot(*delta[:2]),
                      net_heading_change_rad=angle_difference(final['yaw_rad'], initial['yaw_rad']))
    except Exception as exc:
        error = exc
        result['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        try:
            check.cleanup()
            result['cleanup_reset_succeeded'] = True
        except Exception as exc:
            result['cleanup_reset_succeeded'] = False
            result['cleanup_error'] = f'{type(exc).__name__}: {exc}'
            result['passed'] = False
            if error is None:
                error = exc
        finally:
            if rclpy.ok():
                rclpy.shutdown()
        result['stages'], result['samples'] = check.stages, check.samples
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items() if key not in ('stages', 'samples')}, indent=2))
    if error is not None:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
