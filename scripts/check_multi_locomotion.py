#!/usr/bin/env python3
"""Verify several Go2s on their own namespaces stay alive and separated.

Drives each robot forward in turn, then all at once, and checks that every
one moves, odom keeps coming, and the pair-wise XY distance stays above the
collision diameter. Reset all robots at the end.
"""
import argparse
import json
import math
from pathlib import Path
import time

import rclpy
from geometry_msgs.msg import Twist
from nav_msgs.msg import Odometry
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import JointState
from std_srvs.srv import Trigger


class Robot:
    def __init__(self, node, name, namespace):
        self.name = name
        self.ns = namespace
        self.publisher = node.create_publisher(Twist, namespace + '/cmd_vel', 1)
        self.reset = node.create_client(Trigger, namespace + '/reset')
        self.odom = None
        self.joints = None
        node.create_subscription(Odometry, namespace + '/odom', self.on_odom,
                                 qos_profile_sensor_data)
        node.create_subscription(JointState, namespace + '/joint_states', self.on_joints,
                                 qos_profile_sensor_data)

    def on_odom(self, msg):
        self.odom = msg

    def on_joints(self, msg):
        self.joints = msg

    def send(self, x=0., y=0., z=0.):
        m = Twist()
        m.linear.x, m.linear.y, m.angular.z = float(x), float(y), float(z)
        self.publisher.publish(m)

    def position(self):
        p = self.odom.pose.pose.position
        return (p.x, p.y)


class Check:
    def __init__(self, manifest_path):
        self.node = Node('go2_multi_locomotion_check')
        data = json.loads(manifest_path.read_text())
        robots = data['robots']
        if not 2 <= len(robots) <= 8:
            raise ValueError('manifest needs 2..8 robots')
        self.robots = [Robot(self.node, r['name'], r['namespace']) for r in robots]

    def spin(self, duration):
        end = time.monotonic() + duration
        while rclpy.ok() and time.monotonic() < end:
            rclpy.spin_once(self.node, timeout_sec=min(.01, end - time.monotonic()))

    def wait_ready(self):
        deadline = time.monotonic() + 30
        while rclpy.ok() and time.monotonic() < deadline:
            if all(r.odom is not None and r.joints is not None for r in self.robots):
                return
            self.spin(.05)
        raise TimeoutError('missing odom/joint_states on some namespace')

    def stage(self, name, duration, active, forward=.3):
        start = {r.name: r.position() for r in self.robots}
        end = time.monotonic() + duration
        while time.monotonic() < end:
            for r in self.robots:
                if r in active:
                    r.send(forward)
            self.spin(.05)
        for r in self.robots:
            r.send()
        self.spin(.3)
        moved = {r.name: math.dist(start[r.name], r.position()) for r in self.robots}
        return {'name': name, 'moved_m': moved}

    def collisions(self):
        for i, a in enumerate(self.robots):
            for b in self.robots[i + 1:]:
                dist = math.dist(a.position(), b.position())
                if dist < .35:
                    return (a.name, b.name, dist)
        return None

    def reset_all(self):
        for r in self.robots:
            r.send()
        self.spin(.2)
        for r in self.robots:
            if not r.reset.wait_for_service(timeout_sec=10):
                raise TimeoutError(r.name + ' reset unavailable')
            f = r.reset.call_async(Trigger.Request())
            rclpy.spin_until_future_complete(self.node, f, timeout_sec=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--solo-move', type=float, default=.08,
                        help='metres each solo-driven robot must travel')
    parser.add_argument('--solo-idle', type=float, default=.12,
                        help='max metres a non-driven robot may drift during a solo stage')
    args = parser.parse_args()
    rclpy.init()
    result = {'passed': False}
    try:
        check = Check(args.manifest)
        check.wait_ready()
        for i, r in enumerate(check.robots):
            out = check.stage(f'solo_{r.name}', 3., {r})
            if out['moved_m'][r.name] < args.solo_move:
                raise RuntimeError(f'{r.name} barely moved: {out["moved_m"]}')
            for other in check.robots:
                if other is not r and out['moved_m'][other.name] > args.solo_idle:
                    raise RuntimeError(f'{other.name} moved while only {r.name} was commanded')
        together = check.stage('together', 4., set(check.robots), forward=.3)
        collision = check.collisions()
        if collision:
            raise RuntimeError(f'robots overlapped: {collision}')
        result.update(passed=True, together=together)
        check.reset_all()
    except Exception as exc:
        result['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        if rclpy.ok():
            rclpy.shutdown()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
