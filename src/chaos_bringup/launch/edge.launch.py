"""Onboard entry point: explicitly select the one real asset on this machine."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration

from chaos_bringup.launching import build_launch_actions, discovery_range, parse_bool
from chaos_bringup.planning import prepare_edge_plan


def _start(context):
    values = {key: LaunchConfiguration(key).perform(context) for key in
              ('inventory', 'asset_id', 'enable_mqtt', 'dry_run', 'discovery_range')}
    mqtt_enabled = parse_bool(values['enable_mqtt'], 'enable_mqtt')
    dry_run = parse_bool(values['dry_run'], 'dry_run')
    discovery = discovery_range(values['discovery_range'])
    plan = prepare_edge_plan(values['inventory'], values['asset_id'],
                             enable_mqtt=mqtt_enabled, check_files=not dry_run)
    return build_launch_actions(plan, dry_run=dry_run, discovery=discovery)


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('inventory', description='Absolute path to this device inventory'),
        DeclareLaunchArgument('asset_id', description='Explicit identity of this physical robot'),
        DeclareLaunchArgument('enable_mqtt', default_value='true'),
        DeclareLaunchArgument('dry_run', default_value='false'),
        DeclareLaunchArgument('discovery_range', default_value='LOCALHOST'),
        OpaqueFunction(function=_start),
    ])
