"""rosapi · 액션 취소 · 가짜 노드 매개변수 · slam_toolbox 서비스 응답."""

# 가짜 노드 표: 이름 -> (발행, 구독)
NODES = {
    "/control_node": (["/odom", "/wheel_states", "/wheel_cmd"], ["/cmd_vel"]),
    "/cmd_watchdog": (["/cmd_vel"], ["/teleop/cmd_vel"]),
    "/straight_drive": (["/straight_drive/status", "/cmd_vel"],
                        ["/straight_drive/goal", "/straight_drive/cancel", "/odom"]),
    "/link_pingpong": (["/link/pong", "/link/heartbeat"], ["/link/ping"]),
    "/odom_tf_broadcaster": (["/tf"], ["/odom"]),
    "/static_tf_laser": (["/tf_static"], []),
    "/imu_node": (["/imu/data_raw"], []),
    "/sllidar_node": (["/scan"], []),
    "/slam_toolbox": (["/map", "/tf"], ["/scan", "/initialpose"]),
    "/rosbridge_websocket": (["/client_count"], []),
    "/rosapi": ([], []),
    "/diagnostic_aggregator": (["/diagnostics"], []),
}
NAV_NODES = {
    "/amcl": (["/amcl_pose"], ["/scan", "/initialpose"]),
    "/planner_server": (["/plan", "/global_costmap/costmap"], []),
    "/controller_server": (["/local_costmap/costmap", "/local_costmap/published_footprint"], []),
    "/bt_navigator": (["/navigate_to_pose/_action/status"], ["/goal_pose"]),
}
PARAM_SRV = {"list_parameters": "rcl_interfaces/srv/ListParameters",
             "describe_parameters": "rcl_interfaces/srv/DescribeParameters",
             "get_parameters": "rcl_interfaces/srv/GetParameters",
             "set_parameters": "rcl_interfaces/srv/SetParameters"}
ROSAPI = ["topics", "subscribers", "publishers", "nodes", "node_details", "services", "service_type", "topic_type"]
FIXED_SRV = {"/navigate_to_pose/_action/cancel_goal": "action_msgs/srv/CancelGoal",
             "/slam_toolbox/save_map": "slam_toolbox/srv/SaveMap"}
VALUE_KEY = {1: "bool_value", 2: "integer_value", 3: "double_value", 4: "string_value"}


def param_value(t, v):
    out = {"type": t, "bool_value": False, "integer_value": 0, "double_value": 0.0, "string_value": "",
           "byte_array_value": [], "bool_array_value": [], "integer_array_value": [], "double_array_value": [],
           "string_array_value": []}
    out[VALUE_KEY[t]] = v
    return out


def ros2_type(t):
    """ROS 2 rosapi 처럼 'pkg/msg/Name' 꼴로."""
    pkg, _, name = t.partition("/")
    return t if not name or "/" in name else f"{pkg}/msg/{name}"


class Services:
    def __init__(self, server, args):
        self.server, self.args = server, args
        self.no_respond = set(args.no_respond or [])

    @property
    def source(self):
        return self.server.source

    def params(self):
        return getattr(self.source, "params", {})

    def nodes(self):
        n = dict(NODES)
        if getattr(self.args, "nav", False):
            n.update(NAV_NODES)
        if self.args.no_watchdog:
            n.pop("/cmd_watchdog")
        return n

    def services(self):
        s = {f"/rosapi/{name}": f"rosapi/srv/{name.title().replace('_', '')}" for name in ROSAPI}
        s.update(FIXED_SRV)
        for node in self.params():
            if node in self.nodes():
                s.update({f"{node}/{k}": v for k, v in PARAM_SRV.items()})
        return s

    def handle(self, name, args):
        """(result, values) 또는 응답하지 않을 때 None."""
        if name in self.no_respond:
            return None
        if name.startswith("/rosapi/"):
            f = getattr(self, "rosapi_" + name[len("/rosapi/"):], None)
            if f:
                return True, f(args)
        if name == "/navigate_to_pose/_action/cancel_goal":
            goal = getattr(self.source, "goal", None)
            uuid = goal["uuid"] if goal else None
            if hasattr(self.source, "goal_end"):
                self.source.goal_end(6)
            return True, {"return_code": 0, "goals_canceling": [{"goal_id": {"uuid": uuid}, "stamp": {"sec": 0, "nanosec": 0}}]
                          if uuid else []}
        if name == "/slam_toolbox/save_map":
            return True, {"result": 0}
        node, _, srv = name.rpartition("/")
        if srv in PARAM_SRV and node in self.params() and node in self.nodes():
            return True, getattr(self, "param_" + srv)(self.params()[node], args)
        return False, "서비스 없음"

    # ---- rosapi --------------------------------------------------------------------------
    def rosapi_topics(self, _):
        t = self.source.visible_topics()
        names = sorted(t)
        return {"topics": names, "types": [ros2_type(t[n]) for n in names]}

    def rosapi_topic_type(self, a):
        return {"type": ros2_type(self.source.topic_type(a.get("topic", "")))}

    def rosapi_subscribers(self, a):
        topic = a.get("topic", "")
        return {"subscribers": sorted(n for n, (_, sub) in self.nodes().items() if topic in sub)}

    def rosapi_publishers(self, a):
        topic = a.get("topic", "")
        return {"publishers": sorted(n for n, (pub, _) in self.nodes().items() if topic in pub)}

    def rosapi_nodes(self, _):
        return {"nodes": sorted(self.nodes())}

    def rosapi_node_details(self, a):
        node = a.get("node", "")
        pub, sub = self.nodes().get(node, ([], []))
        return {"publishing": pub, "subscribing": sub,
                "services": sorted(s for s in self.services() if s.startswith(node + "/"))}

    def rosapi_services(self, _):
        return {"services": sorted(self.services())}

    def rosapi_service_type(self, a):
        return {"type": self.services().get(a.get("service", ""), "")}

    # ---- rcl_interfaces 매개변수 -------------------------------------------------------------
    def param_list_parameters(self, p, a):
        prefixes = a.get("prefixes") or []
        names = sorted(n for n in p if not prefixes or any(n.startswith(x) for x in prefixes))
        return {"result": {"names": names, "prefixes": []}}

    def param_describe_parameters(self, p, a):
        out = []
        for n in a.get("names") or []:
            t, _, rng = p.get(n, (0, None, None))
            d = {"name": n, "type": t, "description": "", "additional_constraints": "", "read_only": False,
                 "floating_point_range": [], "integer_range": []}
            if rng and t == 3:
                d["floating_point_range"] = [{"from_value": rng[0], "to_value": rng[1], "step": 0.0}]
            elif rng and t == 2:
                d["integer_range"] = [{"from_value": rng[0], "to_value": rng[1], "step": 1}]
            out.append(d)
        return {"descriptors": out}

    def param_get_parameters(self, p, a):
        return {"values": [param_value(*p[n][:2]) if n in p else {"type": 0} for n in a.get("names") or []]}

    def param_set_parameters(self, p, a):
        results = []
        for item in a.get("parameters") or []:
            n, v = item.get("name"), item.get("value") or {}
            if n not in p:
                results.append({"successful": False, "reason": "매개변수 없음"})
                continue
            t, _, rng = p[n]
            if v.get("type") != t:
                results.append({"successful": False, "reason": f"형식이 다름({v.get('type')} != {t})"})
                continue
            val = v.get(VALUE_KEY[t])
            if rng and not rng[0] <= val <= rng[1]:
                results.append({"successful": False, "reason": f"범위 밖 {rng[0]}..{rng[1]}"})
                continue
            p[n] = (t, val, rng)
            results.append({"successful": True, "reason": ""})
        return {"results": results}
