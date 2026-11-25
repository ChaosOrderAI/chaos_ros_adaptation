from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('board_ip', default_value='192.168.123.188'),
        DeclareLaunchArgument('board_port', default_value='8888'),
        DeclareLaunchArgument('ptp_period_ms', default_value='100'),
        DeclareLaunchArgument('trigger_topic', default_value='board/trigger'),
        DeclareLaunchArgument('imu_topic', default_value='board/imu'),
        Node(
            package='chaos_sensors',
            executable='chaos_time_sync_node',
            name='chaos_time_sync',
            parameters=[{
                'board_ip': LaunchConfiguration('board_ip'),
                'board_port': LaunchConfiguration('board_port'),
                'ptp_period_ms': LaunchConfiguration('ptp_period_ms'),
                'trigger_topic': LaunchConfiguration('trigger_topic'),
                'imu_topic': LaunchConfiguration('imu_topic'),
            }],
            output='screen',
        ),
    ])
