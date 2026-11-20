"""One launch entry point, one local host; MQTT joins independent DDS domains."""

from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration

from chaos_bringup.launching import build_launch_actions, discovery_range, parse_bool
from chaos_bringup.planning import prepare_plan


def _start(context):
    mode = LaunchConfiguration('mode').perform(context)
    dry_run = parse_bool(LaunchConfiguration('dry_run').perform(context), 'dry_run')
    mqtt_enabled = parse_bool(LaunchConfiguration('enable_mqtt').perform(context), 'enable_mqtt')
    selected = [v.strip() for v in LaunchConfiguration('assets').perform(context).split(',') if v.strip()]
    discovery = discovery_range(LaunchConfiguration('discovery_range').perform(context))
    plan = prepare_plan(
        LaunchConfiguration('inventory').perform(context),
        LaunchConfiguration('host_id').perform(context), mode=mode or None,
        asset_ids=selected, enable_mqtt=mqtt_enabled, check_files=not dry_run,
    )
    skills = {
        'enable_agent': parse_bool(LaunchConfiguration('enable_agent').perform(context), 'enable_agent'),
        'vocabulary_file': LaunchConfiguration('vocabulary_file').perform(context) or None,
        'initial_hub': LaunchConfiguration('initial_hub').perform(context) or None,
        'llama_endpoint': LaunchConfiguration('llama_endpoint').perform(context) or None,
        'task': LaunchConfiguration('task').perform(context) or None,
        'iteration_hz': (float(LaunchConfiguration('iteration_hz').perform(context))
                         if LaunchConfiguration('iteration_hz').perform(context) else None),
    }
    return build_launch_actions(plan, dry_run=dry_run, discovery=discovery,
                                capabilities={'skills': skills})


def generate_launch_description():
    default_inventory = str(Path(get_package_share_directory('chaos_assets')) / 'config/demo.yaml')
    return LaunchDescription([
        DeclareLaunchArgument('inventory', default_value=default_inventory),
        DeclareLaunchArgument('host_id', default_value='dev'),
        DeclareLaunchArgument('mode', default_value='', description='Empty uses configured backend; mock or real overrides it'),
        DeclareLaunchArgument('assets', default_value='', description='Optional comma-separated asset IDs on this host'),
        DeclareLaunchArgument('enable_mqtt', default_value='false'),
        DeclareLaunchArgument('dry_run', default_value='false', description='Validate and print the plan without starting nodes'),
        DeclareLaunchArgument('discovery_range', default_value='LOCALHOST'),
        DeclareLaunchArgument('enable_agent', default_value='false'),
        DeclareLaunchArgument('vocabulary_file', default_value='', description='Versioned skill vocabulary JSON for every selected asset'),
        DeclareLaunchArgument('initial_hub', default_value=''),
        DeclareLaunchArgument('llama_endpoint', default_value=''),
        DeclareLaunchArgument('task', default_value='', description='Task text handed to the onboard agent'),
        DeclareLaunchArgument('iteration_hz', default_value=''),
        OpaqueFunction(function=_start),
    ])
