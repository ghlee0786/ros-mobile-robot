# Topics relayed between the robot (domain 7) and the PC navigation stack (domain 8) by split_relay.py.
# Sourced by the Pi side (robot_link/nav_relay.sh) and the PC side (tooling/nav/run_bridge.sh): both
# sides must list the same topics in the same order, because frames carry the list index. That also
# lets a topic change name on the way: the dashboard sends its initial pose on /dashboard/initialpose
# because the Pi's initialpose_node publishes /initialpose = (0, 0, 0) every 5 s, which would keep
# resetting AMCL if it were relayed; the PC side publishes it as /initialpose.
NAV_RELAY_TO_PC_PORT=7701
NAV_RELAY_TO_ROBOT_PORT=7702
_to_pc=(/scan:sensor_msgs/msg/LaserScan:best_effort
        /odom:nav_msgs/msg/Odometry
        /tf:tf2_msgs/msg/TFMessage
        /tf_static:tf2_msgs/msg/TFMessage:transient_local
        /goal_pose:geometry_msgs/msg/PoseStamped
        /dashboard/nav_cancel:std_msgs/msg/Empty
        /teleop/estop:std_msgs/msg/Empty
        /dashboard/map_reset:std_msgs/msg/Empty)
NAV_RELAY_TO_PC_ROBOT_SIDE=("${_to_pc[@]}" /dashboard/initialpose:geometry_msgs/msg/PoseWithCovarianceStamped)
NAV_RELAY_TO_PC_PC_SIDE=("${_to_pc[@]}" /initialpose:geometry_msgs/msg/PoseWithCovarianceStamped)
# PC -> robot: Nav2 velocity (the Pi's cmd_watchdog relays it); map, plan, localization, the local
# costmap and map->odom (tooling/nav/map_odom_pub.py) for the dashboard
NAV_RELAY_TO_ROBOT=(/teleop/cmd_vel:geometry_msgs/msg/Twist
                    /map:nav_msgs/msg/OccupancyGrid:transient_local
                    /plan:nav_msgs/msg/Path
                    /amcl_pose:geometry_msgs/msg/PoseWithCovarianceStamped:transient_local
                    /global_costmap/costmap:nav_msgs/msg/OccupancyGrid:transient_local
                    /local_costmap/costmap:nav_msgs/msg/OccupancyGrid
                    /local_costmap/published_footprint:geometry_msgs/msg/PolygonStamped
                    /map_odom:tf2_msgs/msg/TFMessage)
