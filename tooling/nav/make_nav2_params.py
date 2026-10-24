#!/usr/bin/env python3
"""Generate nav2_params_robot.yaml from Jazzy's default nav2_params.yaml with this robot's settings.

    python3 tooling/nav/make_nav2_params.py   (inside WSL, ROS 2 Jazzy sourced)
Robot: diff drive, Pico firmware limits |v| <= 0.25 m/s and |w| <= 0.6 rad/s, ~0.25 m radius,
frames odom -> base_link (no base_footprint in the chain), RPLIDAR on /scan.
Nav2's final velocity output goes to /teleop/cmd_vel so the Pi's cmd_watchdog relays it and stops
the robot if the PC or Wi-Fi drops.
"""
import os

import yaml
from ament_index_python.packages import get_package_share_directory

SRC = os.path.join(get_package_share_directory('nav2_bringup'), 'params', 'nav2_params.yaml')
DST = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'nav2_params_robot.yaml')

V_MAX, W_MAX, RADIUS = 0.25, 0.6, 0.25
# The RPLIDAR sees the robot's own frame/posts at 0.14-0.19 m (nothing real within 0.25-0.45 m);
# returns closer than this are ignored so the robot is not an obstacle to itself.
SELF_HIT_RANGE = 0.3
RPP = {
    'plugin': 'nav2_regulated_pure_pursuit_controller::RegulatedPurePursuitController',
    'desired_linear_vel': 0.22,
    'lookahead_dist': 0.4, 'min_lookahead_dist': 0.25, 'max_lookahead_dist': 0.7,
    'lookahead_time': 2.0, 'use_velocity_scaled_lookahead_dist': False,
    'transform_tolerance': 0.5,
    'min_approach_linear_velocity': 0.03, 'approach_velocity_scaling_dist': 0.4,
    'use_collision_detection': True, 'max_allowed_time_to_collision_up_to_carrot': 1.5,
    'use_regulated_linear_velocity_scaling': True, 'use_cost_regulated_linear_velocity_scaling': False,
    'regulated_linear_scaling_min_radius': 0.6, 'regulated_linear_scaling_min_speed': 0.03,
    'use_rotate_to_heading': True, 'rotate_to_heading_angular_vel': W_MAX,
    'rotate_to_heading_min_angle': 0.6, 'max_angular_accel': 1.0,
    'allow_reversing': False, 'max_robot_pose_search_dist': 10.0,
}


def ros(node, data):
    """Return the ros__parameters dict of a top-level node entry."""
    return data[node]['ros__parameters']


def main():
    with open(SRC) as f:
        p = yaml.safe_load(f)

    for node in p.values():                                 # real robot, real clock
        if isinstance(node, dict) and 'ros__parameters' in node:
            node['ros__parameters']['use_sim_time'] = False

    a = ros('amcl', p)
    a.update(base_frame_id='base_link', odom_frame_id='odom', global_frame_id='map',
             robot_model_type='nav2_amcl::DifferentialMotionModel', scan_topic='scan',
             laser_max_range=12.0, laser_min_range=SELF_HIT_RANGE, transform_tolerance=1.0,
             set_initial_pose=False)

    ros('bt_navigator', p).update(robot_base_frame='base_link', odom_topic='/odom', transform_tolerance=0.5)

    c = ros('controller_server', p)
    c.update(controller_frequency=20.0, odom_topic='/odom')   # 10 -> 20 Hz: half the reaction delay
    c['progress_checker'].update(required_movement_radius=0.1, movement_time_allowance=20.0)
    c['general_goal_checker'].update(xy_goal_tolerance=0.15, yaw_goal_tolerance=0.15)
    c['FollowPath'] = RPP

    for name, frame in (('local_costmap', 'odom'), ('global_costmap', 'map')):
        cm = p[name][name]['ros__parameters']
        cm.update(global_frame=frame, robot_base_frame='base_link', robot_radius=RADIUS,
                  transform_tolerance=0.5)
        cm.pop('footprint', None)
        if name == 'local_costmap':
            cm.update(update_frequency=10.0)   # 5 -> 10 Hz: an obstacle reaches the controller up to 100 ms sooner
        for layer in ('obstacle_layer', 'voxel_layer'):
            if layer in cm and 'scan' in cm[layer]:
                cm[layer]['scan'].update(topic='/scan', raytrace_max_range=6.0, obstacle_max_range=5.0,
                                         obstacle_min_range=SELF_HIT_RANGE)
        if 'inflation_layer' in cm:
            cm['inflation_layer'].update(inflation_radius=0.45, cost_scaling_factor=3.0)

    ros('behavior_server', p).update(robot_base_frame='base_link', max_rotational_vel=W_MAX,
                                     min_rotational_vel=0.1, transform_tolerance=0.5)

    vs = ros('velocity_smoother', p)
    vs.update(max_velocity=[V_MAX, 0.0, W_MAX], min_velocity=[-V_MAX, 0.0, -W_MAX],
              max_accel=[0.3, 0.0, 1.0], max_decel=[-0.3, 0.0, -1.0], odom_topic='/odom')

    cmon = ros('collision_monitor', p)
    cmon.update(base_frame_id='base_link', odom_frame_id='odom', transform_tolerance=0.5,
                source_timeout=1.0, cmd_vel_out_topic='/teleop/cmd_vel')
    if 'scan' in cmon:
        cmon['scan']['topic'] = '/scan'
    # Its scan source has no minimum range, so the self-hits sit inside the footprint polygon and
    # would hold the robot still; obstacle avoidance stays with the costmaps + RPP collision check.
    for poly in cmon.get('polygons', []):
        cmon[poly]['enabled'] = False

    if 'docking_server' in p:
        ros('docking_server', p)['base_frame'] = 'base_link'

    with open(DST, 'w') as f:
        f.write('# Generated by tooling/nav/make_nav2_params.py from nav2_bringup/params/nav2_params.yaml\n')
        yaml.safe_dump(p, f, sort_keys=False, default_flow_style=None)
    print(DST)


if __name__ == '__main__':
    main()
