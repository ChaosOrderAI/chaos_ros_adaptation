#!/usr/bin/env python3
"""Draw a dense RGB point cloud on the GPU alongside observed Go2 ROS states.

The cloud is a packed 16-byte NumPy record array: xyz float32[3], rgb uint8[4].
It must use the same local coordinates as the supplied MuJoCo XML. Displayed
poses come from observations; the viewer never steps physics. In live ROS mode,
held movement keys publish bounded velocity commands to the simulated Go2.
"""

import argparse
import ctypes
import json
import math
from pathlib import Path
import re
import struct
import sys
import time
import zlib

import glfw
import mujoco
import numpy as np
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader


VERTEX_SHADER = """#version 330 compatibility
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 3) in float radius;
uniform mat4 projection;
uniform mat4 view;
uniform float radius_scale;
uniform float framebuffer_height;
uniform float maximum_size;
out vec4 point_color;
void main() {
    vec4 eye = view * vec4(position, 1.0);
    gl_Position = projection * eye;
    float diameter = framebuffer_height * abs(projection[1][1]) * radius * radius_scale;
    gl_PointSize = clamp(diameter / max(0.001, -eye.z), 1.25, maximum_size);
    point_color = color;
}
"""

FRAGMENT_SHADER = """#version 330 compatibility
in vec4 point_color;
out vec4 fragment_color;
void main() {
    vec2 offset = gl_PointCoord * 2.0 - 1.0;
    float squared_radius = dot(offset, offset);
    if (squared_radius >= 1.0) discard;
    float coverage = 1.0 - smoothstep(0.72, 1.0, squared_radius);
    fragment_color = vec4(point_color.rgb, coverage);
}
"""


SURFEL_VERTEX_SHADER = """#version 330 compatibility
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in float radius;
uniform mat4 view;
out VertexData {
    vec3 center;
    vec3 normal;
    vec4 color;
    float radius;
} vertex;
void main() {
    vec4 eye = view * vec4(position, 1.0);
    gl_Position = eye;
    vertex.center = eye.xyz;
    vertex.normal = normalize(mat3(view) * normal);
    vertex.color = color;
    vertex.radius = radius;
}
"""

SURFEL_GEOMETRY_SHADER = """#version 330 compatibility
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in VertexData {
    vec3 center;
    vec3 normal;
    vec4 color;
    float radius;
} vertices[];
uniform mat4 projection;
uniform float radius_scale;
uniform float framebuffer_height;
out vec2 surfel_uv;
out vec4 point_color;
void main() {
    vec3 center = vertices[0].center;
    vec3 normal = vertices[0].normal;
    float radius = max(vertices[0].radius * radius_scale,
                       -center.z / (framebuffer_height * abs(projection[1][1])));
    vec4 clip_center = projection * vec4(center, 1.0);
    // Conservative frustum rejection before expanding four vertices per point.
    if (clip_center.w < -radius ||
        abs(clip_center.x) > clip_center.w + radius * (abs(projection[0][0]) + 1.0) ||
        abs(clip_center.y) > clip_center.w + radius * (abs(projection[1][1]) + 1.0)) return;
    vec3 reference = abs(normal.z) < 0.9 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 tangent = normalize(cross(normal, reference));
    vec3 bitangent = cross(normal, tangent);
    for (int i = 0; i < 4; ++i) {
        vec2 corner = vec2((i & 1) == 0 ? -1.0 : 1.0, i < 2 ? -1.0 : 1.0);
        vec3 position = center + radius * (corner.x * tangent + corner.y * bitangent);
        gl_Position = projection * vec4(position, 1.0);
        surfel_uv = corner;
        point_color = vertices[0].color;
        EmitVertex();
    }
    EndPrimitive();
}
"""

SURFEL_FRAGMENT_SHADER = """#version 330 compatibility
in vec2 surfel_uv;
in vec4 point_color;
out vec4 fragment_color;
void main() {
    float squared_radius = dot(surfel_uv, surfel_uv);
    if (squared_radius >= 1.0) discard;
    float coverage = 1.0 - smoothstep(0.72, 1.0, squared_radius);
    fragment_color = vec4(point_color.rgb, coverage);
}
"""


def positive_float(value):
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError('must be positive and finite')
    return result


def finite_float(value):
    result = float(value)
    if not math.isfinite(result):
        raise argparse.ArgumentTypeError('must be finite')
    return result


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cloud', required=True, type=Path)
    parser.add_argument('--model-path', required=True, type=Path)
    normal_source = parser.add_mutually_exclusive_group()
    normal_source.add_argument('--normals', type=Path,
                               help='Float32 Nx3 normals; default: <cloud_stem>_normals.npy if present')
    normal_source.add_argument('--billboard', action='store_true',
                               help='Use camera-facing splats instead of oriented surface splats')
    radius_source = parser.add_mutually_exclusive_group()
    radius_source.add_argument('--radii', type=Path,
                               help='Float32 per-point radii; default: <cloud_stem>_radii.npy if present')
    radius_source.add_argument('--no-adaptive-radius', action='store_true',
                               help='Use a fixed splat radius even when a per-point radius buffer exists')
    parser.add_argument('--name', default='go2')
    parser.add_argument('--namespaces', type=Path,
                        help='JSON mapping of robot name to ROS namespace for a multi-robot scene '
                             '(default: robots.json next to --model-path)')
    parser.add_argument('--namespace', help='ROS observation namespace; default: /sim/<name>')
    parser.add_argument('--state-timeout', type=positive_float, default=120.,
                        help='Seconds to wait for initial ROS state while a large map compiles')
    parser.add_argument('--static-qpos', type=Path,
                        help='Load this .npy pose instead of connecting to ROS (render verification)')
    parser.add_argument('--point-radius', type=positive_float,
                        help='Nominal radius in metres; scales adaptive radii relative to cloud metadata '
                             '(default: metadata, else 0.035); +/- adjust')
    parser.add_argument('--camera-distance', type=positive_float, default=7.5)
    parser.add_argument('--camera-azimuth', type=finite_float, default=135.0)
    parser.add_argument('--camera-elevation', type=finite_float, default=-25.0)
    parser.add_argument('--camera-lookat', type=finite_float, nargs=3)
    parser.add_argument('--overview', action='store_true', help='Start with the full cloud in view')
    parser.add_argument('--width', type=int, default=1600)
    parser.add_argument('--height', type=int, default=1000)
    parser.add_argument('--samples', type=int, choices=(0, 2, 4, 8), default=4)
    parser.add_argument('--no-vsync', action='store_true')
    parser.add_argument('--render-mode', choices=('surfel', 'gaussian'), default='surfel',
                        help='Opaque surfels or depth-aware Gaussian surface blending (G toggles)')
    parser.add_argument('--hidden', action='store_true', help='Hide the GLFW window (still requires DISPLAY)')
    parser.add_argument('--allow-software', action='store_true',
                        help='Explicitly allow software GL for diagnostics; default rejects it')
    parser.add_argument('--screenshot', type=Path, help='Save the final rendered frame as PNG')
    parser.add_argument('--frames', type=int, help='Exit after this many frames')
    parser.add_argument('--duration', type=positive_float, help='Exit after this many rendering seconds')
    args, ros_args = parser.parse_known_args()
    if min(args.width, args.height) <= 0:
        parser.error('--width and --height must be positive')
    if args.frames is not None and args.frames <= 0:
        parser.error('--frames must be positive')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.name):
        parser.error('--name must be a valid ROS name')
    if args.static_qpos and ros_args:
        parser.error('unrecognized arguments: ' + ' '.join(ros_args))
    if args.screenshot and args.frames is None and args.duration is None:
        args.frames = 60
    if args.hidden and args.frames is None and args.duration is None:
        parser.error('--hidden requires --frames, --duration or --screenshot')
    manifest = args.namespaces
    if manifest is None:
        candidate = args.model_path.expanduser().resolve().with_name('robots.json')
        manifest = candidate if candidate.is_file() else None
    args.namespaces = {}
    if manifest is not None:
        try:
            data = json.loads(manifest.read_text())
            args.namespaces = {str(robot['name']): str(robot['namespace'])
                               for robot in data['robots']}
        except (OSError, ValueError, TypeError, KeyError) as error:
            parser.error(f'invalid robot manifest {manifest}: {error}')
    metadata_path = args.cloud.expanduser().with_suffix('.json')
    try:
        metadata = json.loads(metadata_path.read_text()) if metadata_path.is_file() else {}
        if not isinstance(metadata, dict):
            raise ValueError('expected a JSON object')
        args.radius_reference = positive_float(metadata.get('point_radius_m', 0.035))
    except (OSError, ValueError, TypeError, argparse.ArgumentTypeError) as error:
        parser.error(f'invalid point radius in {metadata_path}: {error}')
    if args.point_radius is None:
        args.point_radius = args.radius_reference
    return args, ros_args


def load_cloud(path):
    cloud = np.load(path.expanduser().resolve(), mmap_mode='r', allow_pickle=False)
    expected = np.dtype([('xyz', '<f4', (3,)), ('rgb', 'u1', (4,))])
    if cloud.ndim != 1 or cloud.dtype != expected or not len(cloud):
        raise ValueError(f'{path}: expected nonempty packed xyz float32[3], rgb uint8[4] array')
    low = np.full(3, np.inf)
    high = np.full(3, -np.inf)
    for start in range(0, len(cloud), 1_000_000):
        positions = cloud['xyz'][start:start + 1_000_000]
        if not np.isfinite(positions).all():
            raise ValueError(f'{path}: cloud contains non-finite coordinates')
        low = np.minimum(low, positions.min(axis=0))
        high = np.maximum(high, positions.max(axis=0))
    return cloud, low, high


def load_normals(args, count):
    if args.billboard:
        return None
    cloud_path = args.cloud.expanduser()
    path = args.normals or cloud_path.with_name(cloud_path.stem + '_normals.npy')
    path = path.expanduser().resolve()
    if not path.exists() and args.normals is None:
        print('No normals buffer found; using camera-facing point splats.', flush=True)
        return None
    normals = np.load(path, mmap_mode='r', allow_pickle=False)
    if normals.shape != (count, 3) or normals.dtype != np.dtype('<f4'):
        raise ValueError(f'{path}: expected float32 normal array with shape ({count}, 3)')
    for start in range(0, count, 1_000_000):
        values = normals[start:start + 1_000_000]
        if not np.isfinite(values).all() or np.any(np.linalg.norm(values, axis=1) < 1e-6):
            raise ValueError(f'{path}: normals must be finite and nonzero')
    print(f'Oriented surface splats: {path}; normal buffer {normals.nbytes / 2**20:.1f} MiB', flush=True)
    return normals


def load_radii(args, count):
    if args.no_adaptive_radius:
        return None
    cloud_path = args.cloud.expanduser()
    path = args.radii or cloud_path.with_name(cloud_path.stem + '_radii.npy')
    path = path.expanduser().resolve()
    if not path.exists() and args.radii is None:
        return None
    radii = np.load(path, mmap_mode='r', allow_pickle=False)
    if radii.shape != (count,) or radii.dtype != np.dtype('<f4'):
        raise ValueError(f'{path}: expected float32 radius array with shape ({count},)')
    for start in range(0, count, 1_000_000):
        values = radii[start:start + 1_000_000]
        if not np.isfinite(values).all() or np.any(values <= 0):
            raise ValueError(f'{path}: radii must be finite and positive')
    print(f'Adaptive surface radius: {path}; GPU buffer {radii.nbytes / 2**20:.1f} MiB', flush=True)
    return radii


MOVEMENT_KEYS = frozenset((glfw.KEY_W, glfw.KEY_S, glfw.KEY_A,
                           glfw.KEY_D, glfw.KEY_Q, glfw.KEY_E))


def movement_command(keys):
    """Body-frame forward/left/yaw speeds; opposing keys cancel each other."""
    forward = int(glfw.KEY_W in keys) - int(glfw.KEY_S in keys)
    return ((0.45 if forward > 0 else 0.3) * forward,
            0.2 * (int(glfw.KEY_Q in keys) - int(glfw.KEY_E in keys)),
            0.6 * (int(glfw.KEY_A in keys) - int(glfw.KEY_D in keys)))


class RosObservation:
    """Validated state subscription and bounded, explicitly owned keyboard commands."""

    def __init__(self, model, args, ros_args, node=None, robot_name=None, root_joint=None):
        import rclpy
        from rclpy.node import Node
        from rclpy.qos import qos_profile_sensor_data
        from nav_msgs.msg import Odometry
        from sensor_msgs.msg import JointState
        from geometry_msgs.msg import Twist
        from std_msgs.msg import String
        from std_srvs.srv import Trigger

        self.rclpy = rclpy
        self.owns_context = node is None
        if node is None:
            self.rclpy.init(args=ros_args)
            self.node = Node('chaos_gpu_viewer_' + args.name)
        else:
            self.node = node
        self.instance_name = robot_name or args.name
        if robot_name:
            self.namespace = args.namespaces.get(robot_name, '/sim/' + robot_name)
        else:
            self.namespace = (args.namespace or '/sim/' + args.name).rstrip('/')
        if root_joint is None:
            found = np.flatnonzero(model.jnt_type == int(mujoco.mjtJoint.mjJNT_FREE))
            if len(found) != 1:
                raise ValueError('viewer requires exactly one floating base')
            root_joint = int(found[0])
        self.root_address = int(model.jnt_qposadr[int(root_joint)])
        prefix = (robot_name + '/') if robot_name else ''
        self.addresses = {}
        for index in range(model.nu):
            joint = int(model.actuator_trnid[index, 0])
            kind = int(model.jnt_type[joint])
            name = mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_JOINT, joint)
            if not name or kind not in (int(mujoco.mjtJoint.mjJNT_HINGE),
                                       int(mujoco.mjtJoint.mjJNT_SLIDE)):
                raise ValueError('viewer requires named scalar motor joints')
            if prefix and not name.startswith(prefix):
                continue
            self.addresses[name[len(prefix):]] = int(model.jnt_qposadr[joint])
        if not self.addresses:
            raise ValueError(self.instance_name + ' has no actuated joints')
        self.joints = None
        self.pose = None
        self.joints_time = 0.0
        self.odom_time = 0.0
        self.changed = False
        self.twist_type = Twist
        self.trigger_type = Trigger
        self.command_publisher = self.node.create_publisher(Twist, self.namespace + '/cmd_vel', 1)
        self.reset_client = self.node.create_client(Trigger, self.namespace + '/reset')
        self.command = (0.0, 0.0, 0.0)
        self.command_sent_at = 0.0
        self.reset_future = None
        self.reset_deadline = 0.0
        self.locomotion_state = None
        self.locomotion_time = 0.0
        self.locomotion_error = ''
        self.subscriptions = [
            self.node.create_subscription(JointState, self.namespace + '/joint_states',
                                          self.on_joints, qos_profile_sensor_data),
            self.node.create_subscription(Odometry, self.namespace + '/odom',
                                          self.on_odom, qos_profile_sensor_data),
            self.node.create_subscription(String, self.namespace + '/locomotion/status',
                                          self.on_locomotion_status, 1),
        ]

    def on_joints(self, message):
        if len(message.name) != len(set(message.name)) or set(message.name) != set(self.addresses):
            raise ValueError('joint_states names do not match the selected model')
        if len(message.position) != len(message.name) or not np.isfinite(message.position).all():
            raise ValueError('joint_states positions are incomplete or non-finite')
        self.joints = [(self.addresses[name], value)
                       for name, value in zip(message.name, message.position)]
        self.joints_time = time.monotonic()
        self.changed = True

    def on_odom(self, message):
        if (message.header.frame_id != self.instance_name + '/odom' or
                message.child_frame_id != self.instance_name + '/base_link'):
            raise ValueError('odom frames do not match selected instance')
        pose = message.pose.pose
        values = np.array([pose.position.x, pose.position.y, pose.position.z,
                           pose.orientation.w, pose.orientation.x,
                           pose.orientation.y, pose.orientation.z], dtype=np.float64)
        norm = np.linalg.norm(values[3:])
        if not np.isfinite(values).all() or norm < 1e-8:
            raise ValueError('odom contains a non-finite pose or invalid quaternion')
        values[3:] /= norm
        self.pose = values
        self.odom_time = time.monotonic()
        self.changed = True

    def on_locomotion_status(self, message):
        try:
            state = json.loads(message.data)
            if (not isinstance(state, dict) or
                    any(not isinstance(state.get(key), bool) for key in ('active', 'fallen', 'paused')) or
                    not isinstance(state.get('fault'), str)):
                raise ValueError('expected active/fallen/paused booleans and fault string')
        except (ValueError, TypeError) as error:
            detail = str(error)
            if detail != self.locomotion_error:
                print('Invalid locomotion status: ' + detail, flush=True)
                self.locomotion_error = detail
            return
        previous_fault = self.locomotion_state.get('fault', '') if self.locomotion_state else ''
        if state['fault'] and state['fault'] != previous_fault:
            print('Locomotion fault: ' + state['fault'], flush=True)
        elif previous_fault and not state['fault']:
            print('Locomotion fault cleared.', flush=True)
        self.locomotion_state = state
        self.locomotion_time = time.monotonic()
        self.locomotion_error = ''

    def motion_status(self):
        if self.locomotion_state is None or time.monotonic() - self.locomotion_time > 1.0:
            return 'motion offline'
        if self.locomotion_state['fault']:
            return 'motion fault: R reset'
        if self.locomotion_state['fallen']:
            return 'fallen: R reset'
        if self.locomotion_state['paused']:
            return 'motion paused'
        return 'motion ready' if self.locomotion_state['active'] else 'motion waiting'

    def focus_address(self):
        return self.root_address

    def update(self, data, *, spin=True):
        # Drain a bounded number so high-rate ROS traffic cannot starve drawing.
        if spin:
            for _ in range(4):
                self.rclpy.spin_once(self.node, timeout_sec=0)
        if self.reset_future is not None and time.monotonic() > self.reset_deadline:
            future = self.reset_future
            self.reset_future = None
            self.reset_client.remove_pending_request(future)
            future.cancel()
            print('Reset timed out after 5 seconds.', flush=True)
        if self.changed and self.joints is not None and self.pose is not None:
            for joint_address, value in self.joints:
                data.qpos[joint_address] = value
            data.qpos[self.root_address:self.root_address + 7] = self.pose
            self.changed = False
            return True
        return False

    def command_velocity(self, command, force=False):
        """Publish changes immediately and repeat only active commands at 20 Hz.

        Idle viewers publish nothing, leaving external ROS controllers in charge.
        A release sends one zero; the locomotion controller additionally enforces
        its own command watchdog if rendering or the desktop stops responding.
        """
        if not self.rclpy.ok():
            return
        values = np.asarray(command, dtype=np.float64)
        if values.shape != (3,) or not np.isfinite(values).all():
            raise ValueError('velocity command must contain three finite values')
        target = tuple(float(value) for value in np.clip(values, (-0.3, -0.2, -0.6), (0.45, 0.2, 0.6)))
        if self.reset_future is not None:
            target = (0.0, 0.0, 0.0)
        now = time.monotonic()
        if (force or target != self.command or
                (any(target) and now - self.command_sent_at >= 0.05)):
            message = self.twist_type()
            message.linear.x, message.linear.y, message.angular.z = target
            self.command_publisher.publish(message)
            self.command = target
            self.command_sent_at = now

    def stop(self, force=False):
        self.command_velocity((0.0, 0.0, 0.0), force=force)

    def reset(self):
        self.stop(force=True)
        if self.reset_future is not None:
            print('Reset already pending.', flush=True)
            return
        if not self.reset_client.service_is_ready():
            print('Reset service unavailable: ' + self.namespace + '/reset', flush=True)
            return
        self.reset_future = self.reset_client.call_async(self.trigger_type.Request())
        self.reset_deadline = time.monotonic() + 5.0
        self.reset_future.add_done_callback(self.on_reset_complete)
        print('Reset requested: ' + self.namespace + '/reset', flush=True)

    def on_reset_complete(self, future):
        if future is not self.reset_future:
            return
        self.reset_future = None
        try:
            response = future.result()
            print(('Reset complete: ' if response.success else 'Reset failed: ') + response.message,
                  flush=True)
        except Exception as error:
            print(f'Reset failed: {error}', flush=True)

    def status(self):
        if self.joints is None or self.pose is None:
            return 'waiting for ROS'
        age = time.monotonic() - min(self.joints_time, self.odom_time)
        return 'ROS live' if age < 1.0 else f'ROS stale {age:.1f}s'

    def close(self):
        self.stop()
        if self.reset_future is not None:
            future = self.reset_future
            self.reset_future = None
            self.reset_client.remove_pending_request(future)
            future.cancel()
        if self.owns_context:
            self.node.destroy_node()
            if self.rclpy.ok():
                self.rclpy.shutdown()


class FleetObservation:
    """One ROS node for several robots that share a compiled scene."""

    def __init__(self, model, args, ros_args, roots):
        import rclpy
        from rclpy.node import Node

        self.rclpy = rclpy
        self.rclpy.init(args=ros_args)
        self.node = Node('chaos_gpu_viewer')
        self.robots = []
        for joint in roots:
            body = int(model.jnt_bodyid[int(joint)])
            name = mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_BODY, body)
            if not name or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
                raise ValueError('each floating body needs a ROS-compatible name')
            self.robots.append(RosObservation(
                model, args, None, node=self.node, robot_name=name, root_joint=int(joint)))
        self.selected = 0
        self.namespace = ', '.join(robot.namespace for robot in self.robots)

    def targets(self):
        if self.selected is None:
            return self.robots
        return [self.robots[self.selected]]

    def select(self, index):
        if index is not None and not 0 <= index < len(self.robots):
            print(f'No robot {index + 1}; this scene has {len(self.robots)}.', flush=True)
            return
        if index == self.selected:
            return
        previous = self.targets()
        self.selected = index
        for robot in previous:
            if robot not in self.targets():
                robot.stop(force=True)
        if index is None:
            print('Controlling every robot.', flush=True)
        else:
            print('Controlling ' + self.robots[index].instance_name + ' only.', flush=True)

    def focus_address(self):
        robot = self.robots[0 if self.selected is None else self.selected]
        return robot.focus_address()

    def update(self, data):
        for _ in range(4):
            self.rclpy.spin_once(self.node, timeout_sec=0)
        ready = True
        for robot in self.robots:
            robot.update(data, spin=False)
            ready = ready and robot.joints is not None and robot.pose is not None
        return ready

    def command_velocity(self, command, force=False):
        for robot in self.targets():
            robot.command_velocity(command, force=force)

    def stop(self, force=False):
        for robot in self.targets():
            robot.stop(force=force)

    def reset(self):
        for robot in self.targets():
            robot.reset()

    @property
    def command(self):
        return self.targets()[0].command

    def status(self):
        label = 'all' if self.selected is None else self.robots[self.selected].instance_name
        label = 'robot ' + label if self.selected is not None else label
        fresh = all(robot.joints is not None and robot.pose is not None and
                    time.monotonic() - min(robot.joints_time, robot.odom_time) < 1.0
                    for robot in self.robots)
        return f'control {label} | ' + ('ROS live' if fresh else 'ROS stale')

    def motion_status(self):
        states = [robot.motion_status() for robot in self.targets()]
        for state in states:
            if state != 'motion ready':
                return state
        return 'motion ready'

    def close(self):
        for robot in self.robots:
            robot.close()
        self.node.destroy_node()
        if self.rclpy.ok():
            self.rclpy.shutdown()


class PointRenderer:
    def __init__(self, cloud, normals=None, radii=None, radius_reference=0.035):
        self.count = len(cloud)
        self.radius_reference = radius_reference
        self.oriented = normals is not None
        if self.oriented:
            self.program = compileProgram(
                compileShader(SURFEL_VERTEX_SHADER, GL.GL_VERTEX_SHADER),
                compileShader(SURFEL_GEOMETRY_SHADER, GL.GL_GEOMETRY_SHADER),
                compileShader(SURFEL_FRAGMENT_SHADER, GL.GL_FRAGMENT_SHADER))
        else:
            self.program = compileProgram(compileShader(VERTEX_SHADER, GL.GL_VERTEX_SHADER),
                                          compileShader(FRAGMENT_SHADER, GL.GL_FRAGMENT_SHADER))
        self.uniforms = {name: GL.glGetUniformLocation(self.program, name) for name in (
            'projection', 'view', 'radius_scale', 'framebuffer_height', 'maximum_size')}
        self.vao = GL.glGenVertexArrays(1)
        self.vbo = GL.glGenBuffers(1)
        GL.glBindVertexArray(self.vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.vbo)
        # Upload packed records once; each frame only changes camera uniforms.
        GL.glBufferData(GL.GL_ARRAY_BUFFER, cloud.nbytes, cloud, GL.GL_STATIC_DRAW)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, GL.GL_FALSE, 16, ctypes.c_void_p(0))
        GL.glEnableVertexAttribArray(1)
        GL.glVertexAttribPointer(1, 4, GL.GL_UNSIGNED_BYTE, GL.GL_TRUE, 16, ctypes.c_void_p(12))
        self.normal_vbo = None
        if normals is not None:
            self.normal_vbo = GL.glGenBuffers(1)
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.normal_vbo)
            GL.glBufferData(GL.GL_ARRAY_BUFFER, normals.nbytes, normals, GL.GL_STATIC_DRAW)
            GL.glEnableVertexAttribArray(2)
            GL.glVertexAttribPointer(2, 3, GL.GL_FLOAT, GL.GL_FALSE, 12, ctypes.c_void_p(0))
        self.radius_vbo = None
        if radii is not None:
            self.radius_vbo = GL.glGenBuffers(1)
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.radius_vbo)
            GL.glBufferData(GL.GL_ARRAY_BUFFER, radii.nbytes, radii, GL.GL_STATIC_DRAW)
            GL.glEnableVertexAttribArray(3)
            GL.glVertexAttribPointer(3, 1, GL.GL_FLOAT, GL.GL_FALSE, 4, ctypes.c_void_p(0))
        GL.glBindVertexArray(0)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, 0)
        self.max_size = float(GL.glGetFloatv(GL.GL_ALIASED_POINT_SIZE_RANGE)[1])
        self.samples = int(GL.glGetIntegerv(GL.GL_SAMPLES))
        self.mode = 'surfel'
        self.gaussian = None
        self.lod_directory = None

    def set_mode(self, mode):
        if mode == 'gaussian' and not self.oriented:
            raise ValueError('Gaussian surface rendering requires normals; omit --billboard')
        if mode == 'gaussian' and self.gaussian is None:
            lod = self.lod_directory
            if (lod is not None and (lod / 'gaussians.npy').is_file()
                    and (lod / 'gaussian_chunks.npy').is_file()
                    and (lod / 'cloud_chunk_index.npy').is_file()):
                from gaussian_lod_renderer import GaussianLodRenderer
                self.gaussian = GaussianLodRenderer(self, SURFEL_VERTEX_SHADER, lod)
            else:
                from gaussian_surface_renderer import GaussianSurfaceRenderer
                self.gaussian = GaussianSurfaceRenderer(self, SURFEL_VERTEX_SHADER)
        self.mode = mode

    def render(self, height, radius):
        if self.mode == 'gaussian':
            self.gaussian.render(height, radius)
            return
        # mjr_render leaves the active mono camera matrices and reversed-Z depth
        # convention in this compatibility context. Reuse them exactly, including
        # clip-control and frustum, so cloud and robot have identical occlusion.
        projection = GL.glGetFloatv(GL.GL_PROJECTION_MATRIX)
        view = GL.glGetFloatv(GL.GL_MODELVIEW_MATRIX)
        GL.glPushAttrib(GL.GL_ENABLE_BIT | GL.GL_DEPTH_BUFFER_BIT | GL.GL_COLOR_BUFFER_BIT)
        GL.glDisable(GL.GL_LIGHTING)
        GL.glDisable(GL.GL_FOG)
        GL.glDisable(GL.GL_BLEND)
        GL.glDisable(GL.GL_ALPHA_TEST)
        GL.glDisable(GL.GL_CULL_FACE)
        GL.glEnable(GL.GL_DEPTH_TEST)
        GL.glDepthMask(GL.GL_TRUE)
        GL.glEnable(GL.GL_PROGRAM_POINT_SIZE)
        GL.glEnable(GL.GL_POINT_SPRITE)
        if self.samples:
            GL.glEnable(GL.GL_MULTISAMPLE)
            GL.glEnable(GL.GL_SAMPLE_ALPHA_TO_COVERAGE)
        GL.glUseProgram(self.program)
        GL.glUniformMatrix4fv(self.uniforms['projection'], 1, GL.GL_FALSE, projection)
        GL.glUniformMatrix4fv(self.uniforms['view'], 1, GL.GL_FALSE, view)
        GL.glUniform1f(self.uniforms['radius_scale'], radius / self.radius_reference)
        GL.glUniform1f(self.uniforms['framebuffer_height'], height)
        GL.glUniform1f(self.uniforms['maximum_size'], min(128.0, self.max_size))
        GL.glBindVertexArray(self.vao)
        if self.radius_vbo is None:
            GL.glVertexAttrib1f(3, self.radius_reference)
        GL.glDrawArrays(GL.GL_POINTS, 0, self.count)
        GL.glBindVertexArray(0)
        GL.glUseProgram(0)
        GL.glPopAttrib()

    def close(self):
        if self.gaussian:
            self.gaussian.close()
        GL.glDeleteBuffers(1, [self.vbo])
        if self.normal_vbo is not None:
            GL.glDeleteBuffers(1, [self.normal_vbo])
        if self.radius_vbo is not None:
            GL.glDeleteBuffers(1, [self.radius_vbo])
        GL.glDeleteVertexArrays(1, [self.vao])
        GL.glDeleteProgram(self.program)


def save_png(path, width, height):
    """Write the current back buffer without an extra imaging dependency."""
    GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, 1)
    raw = GL.glReadPixels(0, 0, width, height, GL.GL_RGB, GL.GL_UNSIGNED_BYTE)
    pixels = np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3)[::-1]

    def chunk(kind, payload):
        return (struct.pack('!I', len(payload)) + kind + payload +
                struct.pack('!I', zlib.crc32(kind + payload) & 0xffffffff))

    scanlines = b''.join(b'\0' + row.tobytes() for row in pixels)
    output = (b'\x89PNG\r\n\x1a\n' +
              chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0)) +
              chunk(b'IDAT', zlib.compress(scanlines, 6)) + chunk(b'IEND', b''))
    path = path.expanduser().resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)
    print(f'Screenshot: {path}', flush=True)


def update_kinematics(model, data):
    # Full mj_forward would run collision detection against tens of thousands of
    # invisible proxies; observations only need body, camera, and light transforms.
    mujoco.mj_kinematics(model, data)
    mujoco.mj_comPos(model, data)
    mujoco.mj_camlight(model, data)


def main():
    args, ros_args = arguments()
    cloud, low, high = load_cloud(args.cloud)
    normals = load_normals(args, len(cloud))
    radii = load_radii(args, len(cloud))
    print(f'Cloud: {len(cloud):,} RGB points; GPU vertex buffer {cloud.nbytes / 2**20:.1f} MiB',
          flush=True)
    mujoco.mj_setCacheCapacity(mujoco.mj_getCache(), 0)
    model = mujoco.MjModel.from_xml_path(str(args.model_path.expanduser().resolve()))
    data = mujoco.MjData(model)
    roots = np.flatnonzero(model.jnt_type == int(mujoco.mjtJoint.mjJNT_FREE))
    if len(roots) < 1 or len(roots) > 8:
        raise ValueError('viewer requires 1 to 8 floating bases')
    root_address = int(model.jnt_qposadr[int(roots[0])])
    if args.static_qpos:
        pose = np.load(args.static_qpos.expanduser().resolve(), allow_pickle=False)
        if pose.shape != (model.nq,) or not np.isfinite(pose).all():
            raise ValueError('--static-qpos must be a finite array with model.nq entries')
        data.qpos[:] = pose
        for joint in roots:
            address = int(model.jnt_qposadr[int(joint)])
            norm = np.linalg.norm(data.qpos[address + 3:address + 7])
            if norm < 1e-8:
                raise ValueError('--static-qpos has an invalid root quaternion')
            data.qpos[address + 3:address + 7] /= norm
    elif model.nkey:
        mujoco.mj_resetDataKeyframe(model, data, 0)
    update_kinematics(model, data)
    scene = mujoco.MjvScene(model, maxgeom=2000)
    scene.flags[mujoco.mjtRndFlag.mjRND_SHADOW] = False
    scene.flags[mujoco.mjtRndFlag.mjRND_REFLECTION] = False
    options = mujoco.MjvOption()
    options.geomgroup[3:] = 0
    options.sitegroup[:] = 0
    camera = mujoco.MjvCamera()
    camera.type = mujoco.mjtCamera.mjCAMERA_FREE
    camera.azimuth = args.camera_azimuth
    camera.elevation = args.camera_elevation
    following = not args.overview and args.camera_lookat is None
    observation = None

    def focus_robot():
        address = observation.focus_address() if observation is not None else root_address
        camera.lookat[:] = data.qpos[address:address + 3]
        camera.distance = args.camera_distance

    def overview():
        nonlocal following
        following = False
        camera.lookat[:] = (low + high) / 2
        # Include the full bounding sphere in the narrower field of view.
        aspect = args.width / args.height
        half_fov = math.radians(float(model.vis.global_.fovy) / 2)
        half_fov = min(half_fov, math.atan(math.tan(half_fov) * aspect))
        camera.distance = np.linalg.norm(high - low) * 0.55 / math.sin(half_fov)
        camera.elevation = -32

    focus_robot()
    if args.overview:
        overview()
    if args.camera_lookat:
        camera.lookat[:] = args.camera_lookat
    # The physics model's extent may describe only the robot. Keep the full
    # scan visible when zooming out while preserving the model's near plane.
    model.vis.map.zfar = max(float(model.vis.map.zfar),
                            float(np.linalg.norm(high - low) * 8 / model.stat.extent))
    context = None
    renderer = None
    window = None
    if not glfw.init():
        raise RuntimeError('GLFW could not connect to DISPLAY; run in the desktop session')
    try:
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
        glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_COMPAT_PROFILE)
        glfw.window_hint(glfw.SAMPLES, args.samples)
        glfw.window_hint(glfw.DEPTH_BITS, 24)
        glfw.window_hint(glfw.VISIBLE, glfw.FALSE if args.hidden else glfw.TRUE)
        window = glfw.create_window(args.width, args.height, 'Go2 | Dense GPU point cloud', None, None)
        if not window:
            raise RuntimeError('Failed to create an OpenGL 3.3 compatibility window')
        glfw.make_context_current(window)
        glfw.swap_interval(0 if args.no_vsync else 1)
        vendor = GL.glGetString(GL.GL_VENDOR).decode()
        gpu = GL.glGetString(GL.GL_RENDERER).decode()
        version = GL.glGetString(GL.GL_VERSION).decode()
        print(f'OpenGL vendor: {vendor}\nOpenGL renderer: {gpu}\nOpenGL version: {version}', flush=True)
        software = any(name in gpu.lower() for name in ('llvmpipe', 'softpipe', 'swrast', 'software'))
        if software and not args.allow_software:
            raise RuntimeError('Software OpenGL detected; select the discrete GPU. For NVIDIA use '
                               '__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia. '
                               'Use --allow-software only for diagnostics.')
        if software:
            print('WARNING: software OpenGL explicitly enabled; this is not GPU acceleration.', flush=True)
        context = mujoco.MjrContext(model, mujoco.mjtFontScale.mjFONTSCALE_100)
        mujoco.mjr_setBuffer(mujoco.mjtFramebuffer.mjFB_WINDOW, context)
        renderer = PointRenderer(cloud, normals, radii, args.radius_reference)
        renderer.lod_directory = args.cloud.expanduser().resolve().parent
        renderer.set_mode(args.render_mode)
        del cloud, normals, radii
        size_label = (f'adaptive radius scale {args.point_radius / args.radius_reference:.2f}'
                      if renderer.radius_vbo is not None else f'point radius {args.point_radius:.3f} m')
        print(f'Multisample antialiasing: {renderer.samples} samples; {size_label}', flush=True)
        print('View: left drag orbit | right/middle drag pan | wheel zoom | F follow toggle | '
              'O overview | G Gaussian/surfel | +/- or [/] splat size | Esc close', flush=True)
        lod_label = getattr(renderer.gaussian, 'label', None)
        print('Rendering mode: ' + renderer.mode + ' ('
              + (lod_label + '; measured points, no photo training' if lod_label
                 else 'measured points; no photo-based training') + ')', flush=True)
        if not args.static_qpos:
            if len(roots) == 1:
                observation = RosObservation(model, args, ros_args)
            else:
                observation = FleetObservation(model, args, ros_args, roots)
            selection = ''
            if len(roots) > 1:
                selection = f' Keys 1-{len(roots)} select that robot (go2 is 1), 0 selects all.'
            print('Movement: hold W/S forward/back | A/D turn | Q/E strafe | Space stop | R reset.'
                  + selection + ' Release or leave the window to stop.', flush=True)
            print('Waiting for state on ' + observation.namespace, flush=True)
            deadline = time.monotonic() + args.state_timeout
            glfw.set_window_title(window, f'Go2 | {renderer.count / 1e6:.2f}M | Waiting for physics')
            while observation.rclpy.ok() and not observation.update(data):
                glfw.poll_events()
                if glfw.window_should_close(window):
                    return 0
                if time.monotonic() > deadline:
                    raise TimeoutError(f'No complete ROS observation within {args.state_timeout:g}s; '
                                       'start simulation and use the same ROS_DOMAIN_ID')
                time.sleep(0.01)
            if not observation.rclpy.ok():
                return 0
            update_kinematics(model, data)
            if not args.overview and not args.camera_lookat:
                focus_robot()
        last_cursor = list(glfw.get_cursor_pos(window))
        pressed_keys = set()
        focused = bool(glfw.get_window_attrib(window, glfw.FOCUSED))

        def cursor_callback(_window, x, y):
            nonlocal following
            dx, dy = x - last_cursor[0], y - last_cursor[1]
            last_cursor[:] = x, y
            left = glfw.get_mouse_button(window, glfw.MOUSE_BUTTON_LEFT) == glfw.PRESS
            pan = (glfw.get_mouse_button(window, glfw.MOUSE_BUTTON_RIGHT) == glfw.PRESS or
                   glfw.get_mouse_button(window, glfw.MOUSE_BUTTON_MIDDLE) == glfw.PRESS)
            if not left and not pan:
                return
            if pan:
                following = False
            height = max(1, glfw.get_window_size(window)[1])
            action = mujoco.mjtMouse.mjMOUSE_MOVE_V if pan else mujoco.mjtMouse.mjMOUSE_ROTATE_V
            mujoco.mjv_moveCamera(model, action, dx / height, dy / height, camera)

        def scroll_callback(_window, _x, y):
            camera.distance = np.clip(camera.distance * math.exp(-0.12 * y), 0.25, 1500.0)

        def key_callback(_window, key, _scan, action, _mods):
            nonlocal following
            digit = None
            if glfw.KEY_0 <= key <= glfw.KEY_9:
                digit = key - glfw.KEY_0
            elif glfw.KEY_KP_0 <= key <= glfw.KEY_KP_9:
                digit = key - glfw.KEY_KP_0
            if digit is not None and action == glfw.PRESS and observation and hasattr(observation, 'select'):
                observation.select(None if digit == 0 else digit - 1)
                return
            if key in MOVEMENT_KEYS:
                if observation:
                    if action == glfw.PRESS and focused:
                        pressed_keys.add(key)
                    elif action == glfw.RELEASE:
                        pressed_keys.discard(key)
                    # Ignore repeat events: Space/focus loss require a fresh press.
                    if action != glfw.REPEAT:
                        observation.command_velocity(movement_command(pressed_keys))
                return
            if action not in (glfw.PRESS, glfw.REPEAT):
                return
            if key == glfw.KEY_ESCAPE:
                pressed_keys.clear()
                if observation:
                    observation.stop()
                glfw.set_window_should_close(window, True)
            elif key == glfw.KEY_SPACE and action == glfw.PRESS:
                pressed_keys.clear()
                if observation:
                    observation.stop(force=True)
            elif key == glfw.KEY_R and action == glfw.PRESS:
                pressed_keys.clear()
                if observation:
                    observation.reset()
            elif key == glfw.KEY_F and action == glfw.PRESS:
                following = not following
                if following:
                    focus_robot()
            elif key == glfw.KEY_O and action == glfw.PRESS:
                overview()
            elif key == glfw.KEY_G and action == glfw.PRESS:
                if renderer.oriented:
                    renderer.set_mode('surfel' if renderer.mode == 'gaussian' else 'gaussian')
                    print('Rendering mode: ' + renderer.mode, flush=True)
            elif key in (glfw.KEY_LEFT_BRACKET, glfw.KEY_MINUS, glfw.KEY_KP_SUBTRACT):
                args.point_radius = max(0.002, args.point_radius / 1.15)
            elif key in (glfw.KEY_RIGHT_BRACKET, glfw.KEY_EQUAL, glfw.KEY_KP_ADD):
                args.point_radius = min(0.5, args.point_radius * 1.15)

        def focus_callback(_window, has_focus):
            nonlocal focused
            focused = bool(has_focus)
            if not focused:
                pressed_keys.clear()
                if observation:
                    observation.stop()

        def close_callback(_window):
            pressed_keys.clear()
            if observation:
                observation.stop()

        glfw.set_cursor_pos_callback(window, cursor_callback)
        glfw.set_scroll_callback(window, scroll_callback)
        glfw.set_key_callback(window, key_callback)
        glfw.set_window_focus_callback(window, focus_callback)
        glfw.set_window_close_callback(window, close_callback)
        started = report_at = time.monotonic()
        title_at = 0.0
        fps = 0.0
        frames = report_frames = 0
        screenshot_saved = False
        width = height = 0
        while not glfw.window_should_close(window):
            glfw.poll_events()
            if observation:
                if not observation.rclpy.ok():
                    break
                if observation.update(data):
                    update_kinematics(model, data)
                observation.command_velocity(movement_command(pressed_keys) if focused else (0, 0, 0))
            if following:
                address = observation.focus_address() if observation is not None else root_address
                camera.lookat[:] = data.qpos[address:address + 3]
            width, height = glfw.get_framebuffer_size(window)
            if width <= 0 or height <= 0:
                glfw.wait_events_timeout(0.1)
                continue
            mujoco.mjv_updateScene(model, data, options, None, camera,
                                  mujoco.mjtCatBit.mjCAT_ALL, scene)
            viewport = mujoco.MjrRect(0, 0, width, height)
            mujoco.mjr_render(viewport, scene, context)
            renderer.render(height, args.point_radius)
            frames += 1
            report_frames += 1
            now = time.monotonic()
            finished = ((args.frames is not None and frames >= args.frames) or
                        (args.duration is not None and now - started >= args.duration))
            if args.screenshot and finished:
                save_png(args.screenshot, width, height)
                screenshot_saved = True
            glfw.swap_buffers(window)
            if now - report_at >= 2.0:
                fps = report_frames / (now - report_at)
                report_at = now
                report_frames = 0
                report = True
            else:
                report = False
            if now - title_at >= 0.2 or report:
                status = (observation.status() + ' | ' + observation.motion_status()
                          if observation else 'static pose')
                size_label = (f'size x{args.point_radius / args.radius_reference:.2f}'
                              if renderer.radius_vbo is not None else f'radius {args.point_radius:.3f}m')
                controls = ('WASD/QE move | Space stop | R reset | '
                            f'cmd {observation.command[0]:+.2f},'
                            f'{observation.command[1]:+.2f},{observation.command[2]:+.2f}'
                            if observation else size_label)
                title = (f'Go2 | {renderer.mode} | {renderer.count / 1e6:.2f}M | {fps:.0f} FPS | {status} | '
                         f'{controls} | F follow {"on" if following else "off"} / O overview')
                glfw.set_window_title(window, title)
                title_at = now
                if report:
                    print(title + ' | ' + size_label, flush=True)
            if finished:
                break
        elapsed = time.monotonic() - started
        if args.screenshot and not screenshot_saved and frames and width and height:
            # The last displayed frame lives in GL_FRONT after swap.
            GL.glReadBuffer(GL.GL_FRONT)
            save_png(args.screenshot, width, height)
        print(f'Rendered {frames} frames in {elapsed:.2f}s; average {frames / max(elapsed, 1e-6):.1f} FPS',
              flush=True)
    finally:
        if observation:
            observation.close()
        if renderer:
            renderer.close()
        if context:
            context.free()
        if window:
            glfw.destroy_window(window)
        glfw.terminate()
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as error:
        print(f'GPU viewer error: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
