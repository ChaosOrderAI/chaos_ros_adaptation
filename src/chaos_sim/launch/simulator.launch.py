"""One headless process containing one or two isolated physical worlds."""
import json
from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction, UnsetEnvironmentVariable
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

MODELS = {'go2': 'go2/unitree_robots/go2/scene.xml',
          't2': 't2/robots/T2/T2_31dof/T2_31dof.xml'}


def start(context):
    robots = LaunchConfiguration('robots').perform(context).split(',')
    if not robots or len(set(robots)) != len(robots) or set(robots) - MODELS.keys():
        raise ValueError('robots must be go2, t2, or go2,t2')
    root = Path(LaunchConfiguration('model_root').perform(context)).expanduser().resolve()
    worlds = []
    for robot in robots:
        model = root / MODELS[robot]
        if not model.is_file():
            raise FileNotFoundError(f'{model}: fetch official models explicitly with fetch_models.py --output {root}')
        worlds.append({'name': robot, 'namespace': '/sim/' + robot,
                       'model_path': str(model), 'kp': 40.0, 'kd': 2.0})
    return [Node(package='chaos_sim', executable='simulator', output='screen',
                 additional_env={'ROS_AUTOMATIC_DISCOVERY_RANGE': 'LOCALHOST'},
                 parameters=[{'worlds_json': json.dumps(worlds), 'discard_visual': True,
                              'publish_rate_hz': 50.0, 'command_timeout_s': 0.5}])]


def generate_launch_description():
    return LaunchDescription([
        # ROS_LOCALHOST_ONLY overrides ROS_AUTOMATIC_DISCOVERY_RANGE.
        UnsetEnvironmentVariable('ROS_LOCALHOST_ONLY'),
        DeclareLaunchArgument('robots', default_value='go2,t2'),
        DeclareLaunchArgument('model_root', default_value='~/.cache/chaos/mujoco'),
        OpaqueFunction(function=start),
    ])
