# Go2 velocity policy

`policy.onnx` is copied unchanged from GS-Playground, not an official Unitree
locomotion release. The robot MJCF remains the existing Unitree Go2 model.

- Source: https://github.com/discoverse-dev/gs_playground
- Revision: `33127caa0e05b59c341628aee266ce7418dbe184`
- Upstream path: `demo/navigation/policies/go2_policy.onnx`
- Upstream observation implementation: `demo/navigation/utils/policy.py`
- SHA256: `8f080e154475621caa89c4959c22f3f6521baca0121620666dc6894c3f122510`
- License: included MIT `LICENSE` (Yufei Jia).

The input is float32 `[batch,48]`: body linear velocity, body angular velocity,
projected gravity, joint positions minus default, joint velocities, previous
actions, and velocity command scaled by `[1,1,1.5]`. Each joint vector uses
`FL, FR, RL, RR`, each with `hip, thigh, calf`. Default positions are
`[.1,.9,-1.8, -.1,.9,-1.8, .1,.9,-1.8, -.1,.9,-1.8]`.

Inference runs at 50 Hz. Position targets are `default + 0.5 * action`, clipped
to the current model's joint limits. The native 500 Hz simulator applies PD
gains `kp=50, kd=0.5` and the existing motor torque limits. Observation vectors
are assembled by joint name because actuator order differs from qpos order.
The ROS bridge slews user velocity commands and adds bounded heading feedback
before feeding them to the policy; see `scripts/go2_policy.py:CommandTracking`.
This policy has been tested in the imported courtyard at limited speeds; it
does not perform navigation, terrain planning, or obstacle avoidance.
