"""GS-Playground Go2 ONNX inference; observations/actions use named joints.

The ROS bridge and offline physics checks share this implementation. The
policy only produces joint targets; MuJoCo applies PD torques and contacts.
"""
from pathlib import Path
import math
import numpy as np
import onnxruntime as ort

JOINT_NAMES = tuple(f'{leg}_{part}_joint' for leg in ('FL', 'FR', 'RL', 'RR')
                    for part in ('hip', 'thigh', 'calf'))
DEFAULT = np.array([.1, .9, -1.8, -.1, .9, -1.8,
                    .1, .9, -1.8, -.1, .9, -1.8], dtype=np.float32)
LOWER = np.array([-1.0472, -1.5708, -2.7227] * 2 +
                 [-1.0472, -.5236, -2.7227] * 2)
UPPER = np.array([1.0472, 3.4907, -.83776] * 2 +
                 [1.0472, 4.5379, -.83776] * 2)
DEFAULT_POLICY = Path(__file__).resolve().parents[1] / 'src/chaos_sim/config/policies/go2/policy.onnx'


def gravity_in_body(quaternion_xyzw):
    q = np.asarray(quaternion_xyzw, dtype=float)
    norm = np.linalg.norm(q)
    if q.shape != (4,) or not np.isfinite(q).all() or norm < .1:
        raise ValueError('Invalid base orientation')
    x, y, z, w = q / norm
    return np.array([-2 * (x*z - w*y), -2 * (y*z + w*x),
                     -(1 - 2 * (x*x + y*y))])


class CommandTracking:
    """Bounded heading feedback for this policy/model contact combination."""
    def __init__(self):
        self.reset()

    def reset(self):
        self.heading = None
        self.filtered_yaw_rate = 0.
        self.lateral = 0.

    def update(self, command, quaternion, twist, dt):
        x, y, z, w = np.asarray(quaternion) / np.linalg.norm(quaternion)
        yaw = math.atan2(2*(w*z + x*y), 1 - 2*(y*y + z*z))
        self.filtered_yaw_rate += dt / (.12 + dt) * (twist[5] - self.filtered_yaw_rate)
        result = np.array(command, dtype=float)
        # This policy has a substantial pure-sideways command dead zone in the
        # current contact model. Calibrate it and slew in policy-command space.
        lateral_target = float(np.clip(2.5*command[1], -.5, .5))
        self.lateral += float(np.clip(lateral_target - self.lateral, -.6*dt, .6*dt))
        result[1] = self.lateral
        if self.heading is None or np.linalg.norm(result) <= 1e-3:
            self.heading = yaw
        if np.linalg.norm(result) <= 1e-3:
            return np.zeros(3)
        difference = self.heading - yaw + command[2]*dt
        error = float(np.clip(math.atan2(math.sin(difference), math.cos(difference)), -.6, .6))
        self.heading = yaw + error
        result[2] = np.clip(command[2] + 2*error +
                            .3*(command[2] - self.filtered_yaw_rate), -.9, .9)
        return result


class Go2Policy:
    def __init__(self, path=DEFAULT_POLICY):
        options = ort.SessionOptions()
        options.intra_op_num_threads = 1
        options.inter_op_num_threads = 1
        self.session = ort.InferenceSession(str(path), sess_options=options,
                                           providers=['CPUExecutionProvider'])
        inputs = self.session.get_inputs()
        if (len(inputs) != 1 or len(inputs[0].shape) != 2 or inputs[0].shape[1] != 48 or
                (isinstance(inputs[0].shape[0], int) and inputs[0].shape[0] != 1)):
            raise ValueError('Expected the GS-Playground 48-observation Go2 policy')
        self.input_name = inputs[0].name
        self.reset()

    def reset(self):
        self.last_action = np.zeros(12, dtype=np.float32)

    def target(self, position, velocity, twist, quaternion, command):
        observation = np.concatenate([
            twist[:3], twist[3:], gravity_in_body(quaternion),
            np.asarray(position) - DEFAULT, velocity, self.last_action,
            np.asarray(command) * [1., 1., 1.5],
        ]).astype(np.float32)
        if observation.shape != (48,) or not np.isfinite(observation).all():
            raise ValueError('Invalid locomotion observation')
        action = np.asarray(self.session.run(None, {self.input_name: observation[None]})[0][0])
        if action.shape != (12,) or not np.isfinite(action).all():
            raise ValueError('Invalid locomotion action')
        self.last_action = action.copy()
        # Native World validates the entire target message against these limits.
        return np.clip(DEFAULT + .5 * action, LOWER + .001, UPPER - .001)
