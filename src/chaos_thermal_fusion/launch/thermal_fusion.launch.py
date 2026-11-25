from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    """AT600 thermal/visible split + fusion.

    Swap visible_topic (and skip the split node) to fuse a different visible
    source, e.g. an RTSP/Hikvision image topic.
    """
    return LaunchDescription([
        DeclareLaunchArgument('device', default_value='/dev/v4l/by-id/usb-IRay_AT600-video-index0'),
        DeclareLaunchArgument('thermal_topic', default_value='thermal/image_raw'),
        DeclareLaunchArgument('visible_topic', default_value='visible/image_raw'),
        DeclareLaunchArgument('mode', default_value='blend'),
        DeclareLaunchArgument('alpha', default_value='0.6'),
        DeclareLaunchArgument('colormap', default_value='inferno'),
        Node(
            package='chaos_sensors',
            executable='at600_split_node',
            name='at600_split_node',
            parameters=[{
                'device': LaunchConfiguration('device'),
                'thermal_topic': LaunchConfiguration('thermal_topic'),
                'visible_topic': LaunchConfiguration('visible_topic'),
            }],
            output='screen',
        ),
        Node(
            package='chaos_thermal_fusion',
            executable='thermal_fusion_node',
            name='thermal_fusion_node',
            parameters=[{
                'thermal_topic': LaunchConfiguration('thermal_topic'),
                'visible_topic': LaunchConfiguration('visible_topic'),
                'mode': LaunchConfiguration('mode'),
                'alpha': LaunchConfiguration('alpha'),
                'colormap': LaunchConfiguration('colormap'),
            }],
            output='screen',
        ),
    ])
