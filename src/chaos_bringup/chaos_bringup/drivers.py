"""Supervise hardware bridges that are part of this workspace."""
import importlib.util
import json

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def managed_go2(asset):
    return asset['mode'] == 'real' and asset['driver']['plugin'] == 'unitree_go2'


def preflight_drivers(plan):
    # Dry runs deliberately skip this check. SDK dependencies belong only to
    # the driver process; adapters and mock builds never initialize vendor DDS.
    if any(managed_go2(item['asset']) for item in plan):
        if importlib.util.find_spec('unitree_sdk2py') is None:
            raise RuntimeError(
                'Go2 driver requires unitree_sdk2py in the ROS Python environment; '
                'install the Unitree SDK dependencies before real startup '
                '(see docs/unitree-go2.md). dry_run and mock do not require the SDK.')


def driver_processes(asset, environment):
    if managed_go2(asset):
        yield f'Go2 driver {asset["id"]}', Node(
            package='chaos_runtime', executable='go2_driver', name='go2_driver',
            namespace=asset['namespace'], output='screen',
            parameters=[{'asset_json': ParameterValue(json.dumps(asset), value_type=str)}],
            additional_env=environment,
        )
