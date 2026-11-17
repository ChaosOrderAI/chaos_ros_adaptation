from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node


def generate_launch_description():
    share = Path(get_package_share_directory('fast_livo2'))
    config = LaunchConfiguration('config')
    camera_config = LaunchConfiguration('camera_config')
    use_rviz = LaunchConfiguration('use_rviz')
    use_sim_time = LaunchConfiguration('use_sim_time')
    fixed_frame = LaunchConfiguration('fixed_frame')

    # camera_config is a second parameter file with the cam_* keys. Leave it
    # empty when the main file already has them or img_en is 0.
    params = [config, {'use_sim_time': use_sim_time}]
    mapping = Node(
        package='fast_livo2',
        executable='fastlivo_mapping',
        name='fast_livo2',
        output='screen',
        parameters=params,
        condition=IfCondition(PythonExpression(["'", camera_config, "' == ''"])),
    )
    mapping_with_camera = Node(
        package='fast_livo2',
        executable='fastlivo_mapping',
        name='fast_livo2',
        output='screen',
        parameters=params + [camera_config],
        condition=IfCondition(PythonExpression(["'", camera_config, "' != ''"])),
    )

    return LaunchDescription([
        DeclareLaunchArgument('config', default_value=str(share / 'config' / 'go2_mid360.yaml')),
        DeclareLaunchArgument('camera_config', default_value=''),
        DeclareLaunchArgument('use_rviz', default_value='false'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        # publish.world_frame of the config: odom for go2_mid360, camera_init upstream.
        DeclareLaunchArgument('fixed_frame', default_value='odom'),
        mapping,
        mapping_with_camera,
        Node(
            package='rviz2',
            executable='rviz2',
            name='fast_livo2_rviz',
            arguments=['-d', str(share / 'rviz' / 'fast_livo2.rviz'), '-f', fixed_frame],
            parameters=[{'use_sim_time': use_sim_time}],
            condition=IfCondition(use_rviz),
        ),
    ])
