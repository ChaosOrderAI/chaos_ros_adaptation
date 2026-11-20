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
    # The skill arguments carry their own defaults: a caller that drives the
    # entry point directly does not have to declare them.
    def skill_argument(name, default=''):
        return LaunchConfiguration(name, default=default).perform(context)

    iteration_hz = skill_argument('iteration_hz')
    skills = {
        'enable_agent': parse_bool(skill_argument('enable_agent', 'false'), 'enable_agent'),
        'vocabulary_file': skill_argument('vocabulary_file') or None,
        'initial_hub': skill_argument('initial_hub') or None,
        'llama_endpoint': skill_argument('llama_endpoint') or None,
        'task': skill_argument('task') or None,
        'iteration_hz': float(iteration_hz) if iteration_hz else None,
        'intent_topic': skill_argument('intent_topic', 'skill_intent'),
        'event_topic': skill_argument('event_topic', 'skill_event'),
        'announcement_topic': skill_argument(
            'announcement_topic', 'communication/announcement'),
        'sensor_cloud_topic': skill_argument('sensor_cloud_topic'),
        'sensor_imu_topic': skill_argument('sensor_imu_topic'),
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
        DeclareLaunchArgument('intent_topic', default_value='skill_intent',
                              description='Topic the top-level sources publish their skill intents on'),
        DeclareLaunchArgument('event_topic', default_value='skill_event',
                              description='Topic the skill layer publishes its lifecycle on'),
        DeclareLaunchArgument('announcement_topic', default_value='communication/announcement',
                              description='Generic communication channel the announce skill writes to'),
        DeclareLaunchArgument('sensor_cloud_topic', default_value='',
                              description='Optional point cloud stream the observe skill may read'),
        DeclareLaunchArgument('sensor_imu_topic', default_value='',
                              description='Optional IMU stream the observe skill may read'),
        OpaqueFunction(function=_start),
    ])
