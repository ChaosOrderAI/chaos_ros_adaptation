"""Onboard entry point: explicitly select the one real asset on this machine."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration

from chaos_bringup.launching import build_launch_actions, discovery_range, parse_bool
from chaos_bringup.planning import prepare_edge_plan


def _start(context):
    keys = ('inventory', 'asset_id', 'enable_mqtt', 'dry_run', 'discovery_range',
            'enable_agent', 'vocabulary_file', 'initial_hub', 'llama_endpoint',
            'task', 'iteration_hz', 'intent_topic', 'event_topic',
            'announcement_topic', 'sensor_cloud_topic', 'sensor_imu_topic')
    # The skill arguments carry their own defaults: a caller that drives the
    # entry point directly does not have to declare them.
    defaults = {
        'enable_agent': 'false', 'vocabulary_file': '', 'initial_hub': '',
        'llama_endpoint': '', 'task': '', 'iteration_hz': '',
        'intent_topic': 'skill_intent', 'event_topic': 'skill_event',
        'announcement_topic': 'communication/announcement',
        'sensor_cloud_topic': '', 'sensor_imu_topic': '',
    }
    values = {
        key: LaunchConfiguration(key, default=defaults.get(key)).perform(context)
        for key in keys
    }
    mqtt_enabled = parse_bool(values['enable_mqtt'], 'enable_mqtt')
    dry_run = parse_bool(values['dry_run'], 'dry_run')
    discovery = discovery_range(values['discovery_range'])
    plan = prepare_edge_plan(values['inventory'], values['asset_id'],
                             enable_mqtt=mqtt_enabled, check_files=not dry_run)
    skills = {
        'enable_agent': parse_bool(values['enable_agent'], 'enable_agent'),
        'vocabulary_file': values['vocabulary_file'] or None,
        'initial_hub': values['initial_hub'] or None,
        'llama_endpoint': values['llama_endpoint'] or None,
        'task': values['task'] or None,
        'iteration_hz': float(values['iteration_hz']) if values['iteration_hz'] else None,
        'intent_topic': values['intent_topic'] or 'skill_intent',
        'event_topic': values['event_topic'] or 'skill_event',
        'announcement_topic': values['announcement_topic'] or 'communication/announcement',
        'sensor_cloud_topic': values['sensor_cloud_topic'],
        'sensor_imu_topic': values['sensor_imu_topic'],
    }
    return build_launch_actions(plan, dry_run=dry_run, discovery=discovery,
                                capabilities={'skills': skills})


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('inventory', description='Absolute path to this device inventory'),
        DeclareLaunchArgument('asset_id', description='Explicit identity of this physical robot'),
        DeclareLaunchArgument('enable_mqtt', default_value='true'),
        DeclareLaunchArgument('dry_run', default_value='false'),
        DeclareLaunchArgument('discovery_range', default_value='LOCALHOST'),
        DeclareLaunchArgument('enable_agent', default_value='false'),
        DeclareLaunchArgument('vocabulary_file', default_value=''),
        DeclareLaunchArgument('initial_hub', default_value=''),
        DeclareLaunchArgument('llama_endpoint', default_value=''),
        DeclareLaunchArgument('task', default_value=''),
        DeclareLaunchArgument('iteration_hz', default_value=''),
        DeclareLaunchArgument('intent_topic', default_value='skill_intent'),
        DeclareLaunchArgument('event_topic', default_value='skill_event'),
        DeclareLaunchArgument('announcement_topic', default_value='communication/announcement'),
        DeclareLaunchArgument('sensor_cloud_topic', default_value=''),
        DeclareLaunchArgument('sensor_imu_topic', default_value=''),
        OpaqueFunction(function=_start),
    ])
