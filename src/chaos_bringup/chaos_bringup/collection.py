"""Launch optional data collection without giving it authority over robot control."""

import json
import os
from pathlib import Path
import stat

import launch.logging
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from chaos_assets import ConfigurationError

from .description import component_asset
from .sensors import require_executable


def enabled_collection(asset):
    config = asset.get('data_collection')
    return config if config and config.get('enabled', False) else None


def collection_summary(asset):
    config = enabled_collection(asset)
    if config is None:
        return None
    return {key: config[key] for key in (
        'output_dir', 'storage_id', 'topics', 'time_sync',
        'max_bagfile_duration_s', 'max_bagfile_size_mb', 'min_free_space_mb')}


def registered_writers():
    """Inspect plugin registration; never construct a writer or a ROS node."""
    try:
        from rosbag2_py import get_registered_writers
        return set(get_registered_writers())
    except (ImportError, RuntimeError, OSError) as error:
        raise ConfigurationError(
            'data collection needs rosbag2_py and a working rosbag2 storage plugin') from error


def check_output_directory(path):
    """Check the nearest existing ancestor without creating any filesystem entry."""
    candidate = Path(path)
    while True:
        try:
            info = candidate.stat()
        except FileNotFoundError:
            if candidate == candidate.parent:
                raise ConfigurationError(f'data collection output has no existing ancestor: {path}')
            candidate = candidate.parent
            continue
        except OSError as error:
            raise ConfigurationError(f'cannot inspect data collection output directory: {path}') from error
        if not stat.S_ISDIR(info.st_mode):
            raise ConfigurationError(f'data collection output ancestor is not a directory: {candidate}')
        if not os.access(candidate, os.W_OK | os.X_OK):
            raise ConfigurationError(f'data collection output ancestor is not writable: {candidate}')
        return


def preflight_collection(plan):
    """Check enabled recorders before any component starts; clock checks run onboard."""
    configs = [config for item in plan
               if (config := enabled_collection(item['asset'])) is not None]
    if not configs:
        return
    try:
        require_executable('chaos_data', 'collector')
    except ConfigurationError as error:
        raise ConfigurationError(
            'data collection dependency chaos_data/collector is unavailable; '
            'build and source the complete workspace') from error
    available = registered_writers()
    for config in configs:
        if config['storage_id'] not in available:
            raise ConfigurationError(
                f'data collection storage plugin is unavailable: {config["storage_id"]}')
        check_output_directory(config['output_dir'])


def collection_processes(asset, environment):
    if enabled_collection(asset) is None:
        return []
    child = Node(
        package='chaos_data', executable='collector', name='data_collector',
        namespace=asset['namespace'], output='screen', additional_env=environment,
        parameters=[{'asset_json': ParameterValue(
            json.dumps(component_asset(asset)), value_type=str), 'use_sim_time': False}],
        # Allow a signal during an in-flight probe/startup as well as the native
        # stop/flush deadline (5 + 15 + 5 + 10 seconds, plus child cleanup).
        sigterm_timeout='40.0', sigkill_timeout='5.0',
    )
    return [(f'Data collection {asset["id"]}', child)]


def on_collection_exit(label):
    """Report an auxiliary recorder failure while robot control continues running."""
    def callback(event, context):
        if context.is_shutdown:
            return []
        launch.logging.get_logger('launch.chaos_data').error(
            f'{label} exited with code {event.returncode}; data collection has stopped. '
            'Robot runtime continues; inspect the collection log and session manifest.')
        return []
    return callback
