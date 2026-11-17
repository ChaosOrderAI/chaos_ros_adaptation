import sys
from pathlib import Path

from ament_index_python.packages import PackageNotFoundError, get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    bringup = Path(get_package_share_directory('go2_bringup'))
    backend = Path(get_package_share_directory('go2_slam_backend'))
    terrain = Path(get_package_share_directory('go2_terrain_planner'))
    use_lidar = LaunchConfiguration('use_lidar')
    use_rviz = LaunchConfiguration('use_rviz')
    use_frontend = LaunchConfiguration('use_frontend')
    use_planner = LaunchConfiguration('use_planner')
    navi_mode = LaunchConfiguration('planner_navi_mode')

    robot = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{
            'robot_description': (bringup / 'urdf' / 'go2_sensors.urdf').read_text(),
        }],
    )
    slam = Node(
        package='go2_slam_backend',
        executable='slam_backend_node',
        name='slam_backend',
        output='screen',
        parameters=[str(backend / 'config' / 'slam_backend.yaml')],
    )
    stairs = Node(
        package='go2_terrain_planner',
        executable='terrain_planner_node',
        name='terrain_planner',
        output='screen',
        parameters=[str(terrain / 'config' / 'terrain_planner.yaml')],
    )

    actions = [
        DeclareLaunchArgument('use_lidar', default_value='false'),
        DeclareLaunchArgument('use_rviz', default_value='false'),
        # FAST-LIVO2 in this workspace. Set to false when another frontend
        # publishes /aft_mapped_to_init and /cloud_registered.
        DeclareLaunchArgument('use_frontend', default_value='true'),
        DeclareLaunchArgument('frontend_camera_config', default_value=''),
        # SCAN-Planner local planner -> /cmd_vel. navi_mode 3 follows
        # /initial_path (the terrain planner route, in odom); 1 takes the
        # RViz 2D goal directly; 2 runs preset fsm.waypoints.
        DeclareLaunchArgument('use_planner', default_value='true'),
        DeclareLaunchArgument('planner_navi_mode', default_value='3'),
        robot,
        slam,
        stairs,
        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2',
            arguments=['-d', str(bringup / 'rviz' / 'stack.rviz')],
            condition=IfCondition(use_rviz),
        ),
    ]

    try:
        livox = Path(get_package_share_directory('livox_ros_driver2'))
        # xfer_format 0 is PointCloud2. fast_livo2 is the only consumer of the
        # raw scan; the planners use its body-cropped /cloud_registered.
        actions.append(Node(
            package='livox_ros_driver2',
            executable='livox_ros_driver2_node',
            name='livox_lidar_publisher',
            output='screen',
            condition=IfCondition(use_lidar),
            parameters=[{
                'xfer_format': 0,
                'multi_topic': 0,
                'data_src': 0,
                'publish_freq': 10.0,
                'output_data_type': 0,
                'frame_id': 'livox_frame',
                'lvx_file_path': '',
                'user_config_path': str(livox / 'config' / 'MID360_config.json'),
                'cmdline_input_bd_code': 'livox0000000001',
            }],
        ))
    except PackageNotFoundError as error:
        print(f'[go2_bringup] WARNING: {error}; no lidar driver in this launch',
              file=sys.stderr)

    try:
        frontend = Path(get_package_share_directory('fast_livo2'))
        actions.append(IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(frontend / 'launch' / 'mapping.launch.py')),
            condition=IfCondition(use_frontend),
            launch_arguments={
                'config': str(frontend / 'config' / 'go2_mid360.yaml'),
                'camera_config': LaunchConfiguration('frontend_camera_config'),
            }.items(),
        ))
    except PackageNotFoundError as error:
        print(f'[go2_bringup] WARNING: {error}; no FAST-LIVO2 frontend in this launch',
              file=sys.stderr)

    try:
        planner_share = Path(get_package_share_directory('scan_planner'))
        actions.extend([
            Node(
                package='scan_planner',
                executable='scan_planner_node',
                name='scan_planner_node',
                output='screen',
                condition=IfCondition(use_planner),
                parameters=[
                    str(bringup / 'config' / 'scan_planner.yaml'),
                    {'fsm.navi_mode': ParameterValue(navi_mode, value_type=int)},
                ],
                remappings=[
                    # scan_planner's collision model and controller are relative
                    # to the trunk (body centre). The backend publishes it as
                    # /body_pose in odom; /odom itself is the base_link/IMU pose.
                    ('body_pose', '/body_pose'),
                    ('sensor_pose', '/aft_mapped_to_init'),
                    # Registered cloud: already in odom and with the robot body
                    # cropped by fast_livo2, unlike the raw /livox/lidar.
                    ('cloud', '/cloud_registered'),
                    ('initial_path', '/initial_path'),
                    ('move_base_simple/goal', '/goal_pose'),
                ],
            ),
            Node(
                package='scan_planner',
                executable='closed_loop_controller',
                name='closed_loop_controller',
                output='screen',
                condition=IfCondition(use_planner),
                parameters=[str(planner_share / 'config' / 'controllers.yaml')],
                remappings=[
                    ('body_pose', '/body_pose'),
                    ('cmd_vel', '/cmd_vel'),
                ],
            ),
        ])
    except PackageNotFoundError as error:
        print(f'[go2_bringup] WARNING: {error}; no local planner in this launch',
              file=sys.stderr)

    return LaunchDescription(actions)
