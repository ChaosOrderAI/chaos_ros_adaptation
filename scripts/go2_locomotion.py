#!/usr/bin/env python3
"""Run the Go2 velocity policy against the native ROS MuJoCo simulator."""
import argparse
import json
import re
import time

import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.executors import ExternalShutdownException
from rclpy.qos import qos_profile_sensor_data
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import Twist
from nav_msgs.msg import Odometry
from sensor_msgs.msg import JointState
from std_msgs.msg import String

from go2_policy import DEFAULT_POLICY, Go2Policy, CommandTracking, JOINT_NAMES, gravity_in_body


class Locomotion(Node):
    def __init__(self, args):
        node_name = 'go2_locomotion' if args.name == 'go2' else 'go2_locomotion_' + args.name
        super().__init__(node_name)
        self.policy = Go2Policy(args.policy)
        self.tracking = CommandTracking()
        self.ns = args.namespace.rstrip('/')
        self.name = args.name
        self.pending = {}
        self.state = None
        self.state_time = 0.
        self.sim_time = None
        self.command = np.zeros(3)
        self.applied = np.zeros(3)
        self.command_time = 0.
        self.fallen = False
        self.fault = ''
        self.paused = False
        self.active = False
        self.inference_ms = 0.
        self.next_log = 0.
        self.last_tick = time.monotonic()
        self.last_used_stamp = None
        self.pub = self.create_publisher(JointState, self.ns + '/joint_targets', 1)
        self.status_pub = self.create_publisher(String, self.ns + '/locomotion/status', 1)
        self.subscriptions_keep = [
            self.create_subscription(JointState, self.ns + '/joint_states',
                                     lambda m: self.receive('joints', m), qos_profile_sensor_data),
            self.create_subscription(Odometry, self.ns + '/odom',
                                     lambda m: self.receive('odom', m), qos_profile_sensor_data),
            self.create_subscription(DiagnosticArray, self.ns + '/diagnostics',
                                     lambda m: self.receive('diag', m), qos_profile_sensor_data),
            self.create_subscription(Twist, self.ns + '/cmd_vel', self.velocity_command, 1),
        ]
        self.timer = self.create_timer(.02, self.step)
        self.status_timer = self.create_timer(.2, self.publish_status)
        self.get_logger().info('Go2 policy ready: 50 Hz, PD 50/0.5 required; cmd_vel watchdog 0.30 s')

    def clear(self):
        self.policy.reset()
        self.tracking.reset()
        self.command[:] = 0
        self.applied[:] = 0
        self.command_time = 0.
        self.fallen = False
        self.fault = ''
        self.last_used_stamp = None

    def receive(self, kind, msg):
        stamp = (msg.header.stamp.sec, msg.header.stamp.nanosec)
        self.pending.setdefault(stamp, {})[kind] = msg
        if len(self.pending) > 8:
            del self.pending[min(self.pending)]
        snapshot = self.pending.get(stamp, {})
        if len(snapshot) != 3:
            return
        if self.state and stamp <= self.state['stamp']:
            return
        joints, odom, diag = (snapshot[k] for k in ('joints', 'odom', 'diag'))
        if (len(joints.name) != 12 or set(joints.name) != set(JOINT_NAMES) or
                len(joints.position) != 12 or len(joints.velocity) != 12 or
                odom.header.frame_id != self.name + '/odom' or
                odom.child_frame_id != self.name + '/base_link' or not diag.status):
            return
        order = [joints.name.index(n) for n in JOINT_NAMES]
        q = odom.pose.pose.orientation
        p = odom.pose.pose.position
        v, w = odom.twist.twist.linear, odom.twist.twist.angular
        values = np.r_[joints.position, joints.velocity, p.x, p.y, p.z,
                       q.x, q.y, q.z, q.w, v.x, v.y, v.z, w.x, w.y, w.z]
        if not np.isfinite(values).all() or np.linalg.norm([q.x, q.y, q.z, q.w]) < .1:
            return
        info = {v.key: v.value for v in diag.status[0].values}
        try:
            sim_time = float(info['sim_time'])
        except (KeyError, ValueError):
            return
        if not np.isfinite(sim_time) or info.get('paused') not in ('true', 'false'):
            return
        paused = info.get('paused') == 'true'
        if ((self.sim_time is not None and sim_time < self.sim_time - .001) or
                paused != self.paused):
            self.clear()
            self.get_logger().info('Simulator reset/pause changed; cleared policy and velocity command')
        self.sim_time, self.paused = sim_time, paused
        self.state = {'stamp': stamp, 'position': np.asarray(joints.position)[order],
                      'velocity': np.asarray(joints.velocity)[order],
                      'quaternion': [q.x, q.y, q.z, q.w],
                      'twist': [v.x, v.y, v.z, w.x, w.y, w.z],
                      'xyz': [p.x, p.y, p.z]}
        self.state_time = time.monotonic()

    def velocity_command(self, msg):
        values = np.array([msg.linear.x, msg.linear.y, msg.angular.z])
        if not np.isfinite(values).all() or self.fallen or self.fault or self.paused:
            return
        self.command = np.clip(values, [-.3, -.2, -.6], [.45, .2, .6])
        self.command_time = time.monotonic()

    def step(self):
        now = time.monotonic()
        dt = min(now - self.last_tick, .05)
        self.last_tick = now
        if self.state is None or now - self.state_time > .10 or self.paused:
            if self.active:
                self.clear()
                self.get_logger().warning('Feedback unavailable/paused; stopped publishing joint targets')
            self.active = False
            return
        if self.fallen or self.fault:
            return
        if -gravity_in_body(self.state['quaternion'])[2] < .65 or self.state['xyz'][2] < .13:
            self.fallen = True
            self.command[:] = 0
            self.applied[:] = 0
            self.get_logger().error('Robot fell; motion disabled until simulator reset (R)')
            return
        if self.last_used_stamp == self.state['stamp']:
            return
        self.active = True
        self.last_used_stamp = self.state['stamp']
        desired = self.command if now - self.command_time <= .30 else np.zeros(3)
        self.applied += np.clip(desired - self.applied, -np.array([.8, .6, 1.8])*dt,
                                np.array([.8, .6, 1.8])*dt)
        started = time.perf_counter()
        try:
            policy_command = self.tracking.update(self.applied, self.state['quaternion'],
                                                  self.state['twist'], dt)
            target = self.policy.target(self.state['position'], self.state['velocity'],
                                        self.state['twist'], self.state['quaternion'], policy_command)
        except Exception as error:
            self.fault = str(error)
            self.command[:] = 0
            self.applied[:] = 0
            self.get_logger().error('Policy failed; motion disabled until reset: ' + self.fault)
            return
        self.inference_ms = (time.perf_counter() - started) * 1000
        message = JointState()
        message.header.stamp = self.get_clock().now().to_msg()
        message.name = list(JOINT_NAMES)
        message.position = target.tolist()
        self.pub.publish(message)
        if now >= self.next_log:
            self.get_logger().info(f'Walking controller active; xyz={np.round(self.state["xyz"], 3)} '
                                   f'cmd={np.round(self.applied, 2)} inference={self.inference_ms:.2f} ms')
            self.next_log = now + 5

    def publish_status(self):
        msg = String()
        msg.data = json.dumps({'active': self.active and not self.fallen and not self.fault,
                               'fallen': self.fallen, 'paused': self.paused,
                               'fault': self.fault,
                               'command': self.applied.tolist(),
                               'command_timed_out': time.monotonic() - self.command_time > .30,
                               'inference_ms': self.inference_ms})
        self.status_pub.publish(msg)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', default=str(DEFAULT_POLICY))
    parser.add_argument('--namespace', default='/sim/go2')
    parser.add_argument('--name', default='go2')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.name):
        parser.error('--name must be a valid ROS name')
    rclpy.init()
    node = Locomotion(args)
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
