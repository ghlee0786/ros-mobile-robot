"""PC-side SLAM / localization + Nav2 + RViz for the robot (ROS 2 Jazzy in WSL).

  mode:=slam          build a map with slam_toolbox while Nav2 can already drive to clicked goals
  mode:=localization  load a saved map (map:=<yaml>) and localize with AMCL, then navigate
The robot itself (Pi, Foxy) provides /scan, /odom and the odom->base_link TF over DDS.
"""
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess, GroupAction, IncludeLaunchDescription
from launch.conditions import IfCondition, LaunchConfigurationEquals
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

HERE = os.path.dirname(os.path.abspath(__file__))
NAV2 = get_package_share_directory('nav2_bringup')
SLAM = get_package_share_directory('slam_toolbox')


def generate_launch_description():
    mode = LaunchConfiguration('mode')
    params = LaunchConfiguration('params_file')
    common = {'use_sim_time': 'false', 'autostart': 'true', 'use_composition': 'False',
              'params_file': params}.items()

    # /scan -> /scan_filtered (robot self-hits removed) for slam_toolbox, which has no min-range option.
    scan_filter = Node(
        package='laser_filters', executable='scan_to_scan_filter_chain', name='scan_to_scan_filter_chain',
        parameters=[os.path.join(HERE, 'scan_filter.yaml')],
        remappings=[('scan', '/scan'), ('scan_filtered', '/scan_filtered')], output='log',
        condition=LaunchConfigurationEquals('mode', 'slam'))

    slam = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(SLAM, 'launch', 'online_async_launch.py')),
        launch_arguments={'use_sim_time': 'false',
                          'slam_params_file': LaunchConfiguration('slam_params_file')}.items(),
        condition=LaunchConfigurationEquals('mode', 'slam'))

    localization = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(NAV2, 'launch', 'localization_launch.py')),
        launch_arguments=dict(common, map=LaunchConfiguration('map')).items(),
        condition=LaunchConfigurationEquals('mode', 'localization'))

    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(NAV2, 'launch', 'navigation_launch.py')),
        launch_arguments=common)

    rviz = ExecuteProcess(
        cmd=['rviz2', '-d', os.path.join(NAV2, 'rviz', 'nav2_default_view.rviz')],
        output='log', condition=IfCondition(LaunchConfiguration('rviz')))

    return LaunchDescription([
        DeclareLaunchArgument('mode', default_value='slam', description='slam | localization'),
        DeclareLaunchArgument('map', default_value='', description='map yaml for mode:=localization'),
        DeclareLaunchArgument('params_file', default_value=os.path.join(HERE, 'nav2_params_robot.yaml')),
        DeclareLaunchArgument('slam_params_file', default_value=os.path.join(HERE, 'slam_params.yaml')),
        DeclareLaunchArgument('rviz', default_value='true'),
        GroupAction([scan_filter, slam, localization, navigation, rviz]),
    ])
