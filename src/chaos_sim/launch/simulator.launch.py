"""One headless process containing multiple isolated physical worlds."""
import json
from collections import Counter
from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction, UnsetEnvironmentVariable
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

MODELS = {'go2': 'go2/unitree_robots/go2/scene.xml',
          't2': 't2/robots/T2/T2_31dof/T2_31dof.xml'}
MAX_WORLDS = 32


def make_worlds(robots_value, root, scene=None):
    robots = robots_value.split(',')
    if (not robots_value or not 1 <= len(robots) <= MAX_WORLDS
            or set(robots) - MODELS.keys()):
        raise ValueError(f'robots must contain 1..{MAX_WORLDS} comma-separated go2/t2 entries; repeats create multiple instances')
    root = Path(root).expanduser().resolve()
    totals = Counter(robots)
    indices = Counter()
    worlds = []
    for robot in robots:
        indices[robot] += 1
        # Keep the original name and namespace when a model occurs only once.
        name = robot if totals[robot] == 1 else f'{robot}_{indices[robot]}'
        model = root / MODELS[robot]
        if not model.is_file():
            raise FileNotFoundError(f'{model}: fetch official models explicitly with fetch_models.py --output {root}')
        worlds.append({'name': name, 'namespace': '/sim/' + name,
                       'model_path': str(model), 'kp': 40.0, 'kd': 2.0,
                       **(scene or {})})
    return worlds


def start(context):
    scene_path = LaunchConfiguration('scene').perform(context)
    scene = None
    if scene_path:
        scene_file = Path(scene_path).expanduser().resolve()
        if not scene_file.is_file():
            raise FileNotFoundError(f'scene does not exist: {scene_file}')
        collision = LaunchConfiguration('scene_collision').perform(context).lower()
        if collision not in ('true', 'false', '1', '0'):
            raise ValueError('scene_collision must be true or false')
        scene = {
            'scene_path': str(scene_file),
            'scene_voxel_size': float(LaunchConfiguration('scene_voxel_size').perform(context)),
            'scene_point_radius': float(LaunchConfiguration('scene_point_radius').perform(context)),
            'scene_max_geoms': int(LaunchConfiguration('scene_max_geoms').perform(context)),
            'scene_collision': collision in ('true', '1'),
        }
    worlds = make_worlds(LaunchConfiguration('robots').perform(context),
                         LaunchConfiguration('model_root').perform(context), scene)
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
        DeclareLaunchArgument('scene', default_value='',
                              description='Optional PLY/PCD point cloud or 3D Gaussian PLY scene'),
        DeclareLaunchArgument('scene_voxel_size', default_value='0.1'),
        DeclareLaunchArgument('scene_point_radius', default_value='0.03'),
        DeclareLaunchArgument('scene_max_geoms', default_value='20000'),
        DeclareLaunchArgument('scene_collision', default_value='true'),
        OpaqueFunction(function=start),
    ])
