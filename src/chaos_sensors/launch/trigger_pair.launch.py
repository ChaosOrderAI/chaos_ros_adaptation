from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('image_topic', default_value='image_raw'),
        DeclareLaunchArgument('image_output_topic', default_value='image_synced'),
        DeclareLaunchArgument('board_channel', default_value='CAM_1'),
        DeclareLaunchArgument('max_age_ms', default_value='50.0'),
        DeclareLaunchArgument('exposure_offset_us', default_value='0'),
        DeclareLaunchArgument('missing_trigger', default_value='arrival'),
        Node(
            package='chaos_sensors',
            executable='trigger_pair_node',
            name='trigger_pair',
            parameters=[{
                'image_topic': LaunchConfiguration('image_topic'),
                'image_output_topic': LaunchConfiguration('image_output_topic'),
                'board_channel': LaunchConfiguration('board_channel'),
                'max_age_ms': LaunchConfiguration('max_age_ms'),
                'exposure_offset_us': LaunchConfiguration('exposure_offset_us'),
                'missing_trigger': LaunchConfiguration('missing_trigger'),
            }],
            output='screen',
        ),
    ])
