"""Launch robot state publishers from the validated, frozen URDF snapshot."""

from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError

from .sensors import require_executable


def enabled_description(asset):
    description = asset.get('description')
    return description if description and description.get('enabled', True) else None


def description_summary(asset):
    """Keep expanded XML out of dry-run logs and their potentially large output."""
    description = enabled_description(asset)
    if description is None:
        return None
    return {key: description[key] for key in
            ('file', 'format', 'root', 'frame_prefix', 'sha256')} | {
                'link_count': len(description['links']),
                'joint_count': len(description['joints']),
            }


def preflight_descriptions(plan):
    """Resolve the publisher once, before any process action is constructed."""
    if any(enabled_description(item['asset']) for item in plan):
        try:
            require_executable('robot_state_publisher', 'robot_state_publisher')
        except ConfigurationError as error:
            raise ConfigurationError(
                'robot description dependency robot_state_publisher/robot_state_publisher '
                'is unavailable; install it and source its ROS workspace') from error


def component_asset(asset):
    """Avoid copying megabytes of URDF into unrelated node parameter payloads.

    The full snapshot remains unchanged for robot_state_publisher and the private
    resolved-assets.json file. The other nodes retain description metadata.
    """
    result = dict(asset)
    if isinstance(asset.get('description'), dict):
        result['description'] = {key: value for key, value in asset['description'].items()
                                 if key != 'xml'}
    return result


def description_processes(asset, environment):
    """Return supervised publishers; never read or expand the source file again."""
    description = enabled_description(asset)
    if description is None:
        return []
    publisher = Node(
        package='robot_state_publisher', executable='robot_state_publisher',
        name='robot_state_publisher', namespace=asset['namespace'],
        output='screen', additional_env=environment,
        parameters=[{
            'robot_description': ParameterValue(description['xml'], value_type=str),
            'frame_prefix': ParameterValue(description['frame_prefix'], value_type=str),
            'publish_frequency': float(description['publish_frequency']),
            'use_sim_time': description['use_sim_time'],
        }],
        remappings=[('joint_states', description['joint_states_topic']),
                    ('tf', '/tf'), ('tf_static', '/tf_static')],
    )
    return [(f'Robot description {asset["id"]}', publisher)]
