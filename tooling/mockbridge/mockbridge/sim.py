"""파이 쪽 노드들을 순수 Python 으로 흉내 낸다(rclpy 없음).

DiffDrive 물리는 tooling/sim/diffdrive_sim.py, 워치독은 cmd_watchdog_node.py, 직진은 straight_drive_node.py,
핑퐁·하트비트는 pingpong_node.py, odom→base_link TF 는 odom_tf_node.py 와 같은 식이다.
"""
import asyncio
import json
import math
import random
import socket
import time

from .protocol import Raw, norm_type
from .room import RES

R, L = 0.05, 0.34          # 바퀴 반지름, 바퀴 간격(펌웨어 config.h)
MAX_V, MAX_W = 0.1, 0.25
DT = 0.01
LASER = (-0.12, 0.0, 0.28)  # base_link→laser (x, y, z), 실기 /tf_static
IMU = (-0.12, 0.0, 0.18)    # base_link→imu

BASE_TYPES = {
    "std_msgs/String", "std_msgs/Empty", "std_msgs/Int32", "std_msgs/Float32", "std_msgs/Float64", "std_msgs/Bool",
    "std_msgs/Header", "geometry_msgs/Twist", "geometry_msgs/Vector3", "geometry_msgs/PoseStamped",
    "geometry_msgs/PoseWithCovarianceStamped", "geometry_msgs/PolygonStamped", "geometry_msgs/TransformStamped",
    "geometry_msgs/Pose2D", "nav_msgs/Odometry", "nav_msgs/Path", "nav_msgs/OccupancyGrid", "sensor_msgs/Imu",
    "sensor_msgs/LaserScan", "sensor_msgs/JointState", "sensor_msgs/Image", "sensor_msgs/CompressedImage",
    "tf2_msgs/TFMessage", "rcl_interfaces/Log", "diagnostic_msgs/DiagnosticArray", "action_msgs/GoalStatusArray",
}


def quat(yaw):
    return {"x": 0.0, "y": 0.0, "z": math.sin(yaw / 2), "w": math.cos(yaw / 2)}


def yaw_of(q):
    x, y, z, w = (q.get(k, 0.0) or 0.0 for k in "xyzw")
    if not any((x, y, z, w)):
        w = 1.0
    return math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))


def wrap(a):
    return math.atan2(math.sin(a), math.cos(a))


def compose(a, b):
    """2D 변환 a ∘ b, 각각 (x, y, yaw)."""
    c, s = math.cos(a[2]), math.sin(a[2])
    return (a[0] + c * b[0] - s * b[1], a[1] + s * b[0] + c * b[1], a[2] + b[2])


def inverse(a):
    c, s = math.cos(a[2]), math.sin(a[2])
    return (-c * a[0] - s * a[1], s * a[0] - c * a[1], -a[2])


class Source:
    """서버에 발행할 메시지를 대 주는 쪽(시뮬레이션 또는 재생)의 공통 부분."""

    def __init__(self, server):
        self.server = server
        self.stopped = set()      # 시간표 stop_topic 으로 멈춘 토픽
        self.types = {}           # 발행 토픽 -> 형식
        self.hidden = set()

    def topic_type(self, topic):
        if topic in self.types:
            return self.types[topic]
        for c in self.server.conns:
            if topic in c.adv:
                return c.adv[topic]
        return ""

    def known_types(self):
        return BASE_TYPES | {norm_type(t) for t in self.types.values()}

    def visible_topics(self):
        out = {t: ty for t, ty in self.types.items() if t not in self.hidden}
        for c in self.server.conns:
            for t, ty in c.adv.items():
                out.setdefault(t, ty)
        return out

    def pi_initialpose(self):
        """파이 initialpose_node: 5 s 마다 map 원점 /initialpose (발행하고 그대로 적용)."""
        msg = {"header": self.header("map"), "pose": {"pose": {"position": {"x": 0.0, "y": 0.0, "z": 0.0},
                                                             "orientation": quat(0.0)}, "covariance": [0.0] * 36}}
        self.on_command("/initialpose", msg)
        return msg

    def on_subscribe(self, conn, topic):
        pass

    def on_command(self, topic, msg):
        pass

    def make(self, topic):
        return None


class World(Source):
    def __init__(self, server, args, room):
        super().__init__(server)
        self.args, self.room = args, room
        self.rng = random.Random(args.seed)
        self.host = socket.gethostname() if args.host_name is None else args.host_name
        self.hidden = set(args.hide_topic or [])
        self.skew = args.stamp_skew_ms / 1000.0
        self.t0 = time.perf_counter()
        # 물리 (diffdrive_sim.py 와 같은 값)
        self.left_gain, self.right_gain = args.left_gain, 1.0
        self.enc_l, self.enc_r = 1.0, args.encoder_scale
        self.imu_offset, self.imu_noise = args.imu_offset, args.imu_noise
        if args.freeze_pose:
            self.imu_noise = 0.0
        self.true = [0.0, 0.0, 0.0]
        self.est = [0.0, 0.0, 0.0]
        self.cmd = (0.0, 0.0)            # 마지막 /cmd_vel (Pico 는 마지막 속도를 계속 유지)
        self.v_true = self.w_true = self.v_est = self.w_est = 0.0
        self.accel_x = 0.0
        self.wheels = (0.0, 0.0)          # 엔코더가 본 바퀴 속도 rad/s
        self.wheel_cmd = (0.0, 0.0)
        # 워치독
        self.teleop, self.teleop_t = (0.0, 0.0), -1e9
        self.wd_moving, self.wd_zeros = False, 0
        # 직진
        self.straight = None
        # map->odom, 경로 추종
        self.map_odom = None
        if args.map_odom:
            x, y, yaw = (float(v) for v in args.map_odom.split(","))
            self.map_odom = (x, y, math.radians(yaw))
        self.goal = None
        self.goal_status = []            # [(uuid, status)]
        self.goal_seq = 0
        self.beat = 0
        self.rosout_n = 0
        self.map_w, self.map_h, self.map_origin = self._map_geometry()
        self.params = make_params()
        self._build_schedule()

    # ---- 토픽 표 ------------------------------------------------------------------------
    def _build_schedule(self):
        a = self.args
        T = {
            "/odom": "nav_msgs/Odometry", "/tf": "tf2_msgs/TFMessage", "/tf_static": "tf2_msgs/TFMessage",
            "/wheel_states": "sensor_msgs/JointState", "/wheel_cmd": "sensor_msgs/JointState",
            "/imu/data_raw": "sensor_msgs/Imu", "/scan": "sensor_msgs/LaserScan", "/map": "nav_msgs/OccupancyGrid",
            "/plan": "nav_msgs/Path", "/link/heartbeat": "std_msgs/String", "/link/pong": "std_msgs/String",
            "/straight_drive/status": "std_msgs/String", "/rosout": "rcl_interfaces/msg/Log",
            "/diagnostics": "diagnostic_msgs/DiagnosticArray", "/client_count": "std_msgs/Int32",
            "/cmd_vel": "geometry_msgs/Twist",
        }
        if a.nav:
            T.update({"/amcl_pose": "geometry_msgs/PoseWithCovarianceStamped",
                      "/global_costmap/costmap": "nav_msgs/OccupancyGrid",
                      "/local_costmap/costmap": "nav_msgs/OccupancyGrid",
                      "/local_costmap/published_footprint": "geometry_msgs/PolygonStamped",
                      "/navigate_to_pose/_action/status": "action_msgs/msg/GoalStatusArray"})
        if a.pi_initialpose:
            T["/initialpose"] = "geometry_msgs/PoseWithCovarianceStamped"
        self.types = T
        s = [("/odom", 1 / 33, self.make_odom), ("/tf", 1 / 33, self.make_tf_odom), ("map_odom", 0.2, self.make_tf_map),
             ("/wheel_states", 1 / 30, lambda: self.make_joint(self.wheels)),
             ("/wheel_cmd", 1 / 30, lambda: self.make_joint(self.wheel_cmd)),
             ("/imu/data_raw", 0.01, self.make_imu), ("/scan", 0.1, self.make_scan),
             ("/link/heartbeat", 1.0, self.make_beat), ("/rosout", 1.0 / max(1.0, a.rosout_burst or 1.0), self.make_rosout),
             ("/diagnostics", 1.0, self.make_diag), ("/client_count", 1.0, self.make_clients)]
        if a.map_odom or a.nav:
            # PC 내비 스택(tooling/nav/map_odom_pub.py)처럼 map→odom 만 /map_odom 으로도 5 Hz. throttle 된 /tf 에서는
            # odom→base_link 에 덮여 운 좋을 때만 살아남는다.
            T["/map_odom"] = "tf2_msgs/TFMessage"
            s.append(("/map_odom", 0.2, self.make_tf_map))
        if a.map_every:
            s.append(("/map", a.map_every, self.make_map))
        if a.pi_initialpose:
            s.append(("/initialpose", 5.0, self.pi_initialpose))
        if a.map_grow:
            s.append(("map_grow", 2.0, self.grow_map))
        if a.nav:
            s += [("/amcl_pose", 0.5, self.make_amcl), ("/global_costmap/costmap", 1.0, self.make_global_costmap),
                  ("/local_costmap/costmap", 0.5, self.make_local_costmap),
                  ("/local_costmap/published_footprint", 0.2, self.make_footprint),
                  ("/navigate_to_pose/_action/status", 1.0, self.make_action_status)]
        self.schedule = s
        self.makers = {k: f for k, _, f in s if k.startswith("/")}
        self.makers.update({"/map": self.make_map, "/tf_static": self.make_tf_static, "/plan": lambda: self.last_plan})
        self.last_plan = None

    def make(self, topic):
        f = self.makers.get(topic)
        return f() if f else None

    # ---- 시각 · 머리글 --------------------------------------------------------------------
    def stamp(self):
        t = time.time() + self.skew
        sec = math.floor(t)
        return {"sec": sec, "nanosec": int((t - sec) * 1e9)}

    def header(self, frame):
        return {"stamp": self.stamp(), "frame_id": frame}

    # ---- 물리 --------------------------------------------------------------------------
    def physics(self):
        if self.args.freeze_pose:
            return
        v = max(-MAX_V, min(MAX_V, self.cmd[0]))
        w = max(-MAX_W, min(MAX_W, self.cmd[1]))
        wl = (2 * v - w * L) / (2 * R) * self.left_gain     # 실제 바퀴 각속도
        wr = (2 * v + w * L) / (2 * R) * self.right_gain
        self.wheel_cmd = ((2 * v - w * L) / (2 * R), (2 * v + w * L) / (2 * R))
        self.wheels = (wl * self.enc_l, wr * self.enc_r)
        prev_v = self.v_true
        for i, (pose, sl, sr) in enumerate(((self.true, 1.0, 1.0), (self.est, self.enc_l, self.enc_r))):
            vl, vr = wl * R * sl, wr * R * sr
            vv, ww = (vl + vr) / 2, (vr - vl) / L
            pose[0] += vv * math.cos(pose[2]) * DT
            pose[1] += vv * math.sin(pose[2]) * DT
            pose[2] += ww * DT
            if i == 0:
                self.v_true, self.w_true = vv, ww
            else:
                self.v_est, self.w_est = vv, ww
        self.accel_x = (self.v_true - prev_v) / DT

    def controllers(self):
        """20 Hz: 워치독 · 직진 · 경로 추종 (각 노드가 /cmd_vel 에 쓰는 것과 같다)."""
        now = time.perf_counter()
        if not self.args.no_watchdog:
            timeout = self.params["/cmd_watchdog"]["timeout"][1]
            fresh = now - self.teleop_t < timeout
            if fresh and (abs(self.teleop[0]) > 1e-6 or abs(self.teleop[1]) > 1e-6):
                self.cmd, self.wd_moving, self.wd_zeros = self.teleop, True, 5
            else:
                self.wd_moving = False
                if self.wd_zeros > 0:
                    self.cmd, self.wd_zeros = (0.0, 0.0), self.wd_zeros - 1
        if self.straight:
            self.straight_step(now)
        if self.goal:
            self.goal_step()

    # ---- 명령 --------------------------------------------------------------------------
    def on_command(self, topic, msg):
        if topic == "/teleop/cmd_vel":
            if not self.args.no_watchdog:
                self.teleop = (float(msg.get("linear", {}).get("x") or 0), float(msg.get("angular", {}).get("z") or 0))
                self.teleop_t = time.perf_counter()
        elif topic == "/link/ping":
            self.server.publish("/link/pong", {"data": f"{msg.get('data', '')}|pi={self.host}|pi_time={time.time():.6f}"})
        elif topic == "/straight_drive/goal":
            self.straight_goal(float(msg.get("x") or 0), float(msg.get("y") or 0))
        elif topic == "/straight_drive/cancel":
            self.straight_finish("canceled")
        elif topic == "/goal_pose":
            self.set_goal(msg)
        elif topic in ("/initialpose", "/dashboard/initialpose"):
            # 대시보드는 /dashboard/initialpose 로 보낸다(P4). 받으면 map→odom(--map-odom)을 그에 맞춘다.
            p = msg.get("pose", {}).get("pose", {})
            pm = (p.get("position", {}).get("x", 0.0), p.get("position", {}).get("y", 0.0),
                  yaw_of(p.get("orientation", {})))
            self.map_odom = compose(pm, inverse(tuple(self.est)))
            self.server.publish("/tf", self.make_tf_map())
            if self.args.nav:
                self.server.publish("/amcl_pose", self.make_amcl())

    def pi_initialpose(self):
        """파이 initialpose_node: 5 s 마다 map 원점 /initialpose (발행하고 그대로 적용)."""
        msg = {"header": self.header("map"), "pose": {"pose": {"position": {"x": 0.0, "y": 0.0, "z": 0.0},
                                                             "orientation": quat(0.0)}, "covariance": [0.0] * 36}}
        self.on_command("/initialpose", msg)
        return msg

    def on_subscribe(self, conn, topic):
        # 구독할 때 한 번 보내는 토픽(transient local)
        if topic == "/tf_static" and topic not in self.stopped:
            for msg in self.static_tfs():
                self.server.publish(topic, msg, only=conn)
        elif topic == "/map" and topic not in self.stopped:
            self.server.publish(topic, self.make(topic), only=conn)
        elif topic == "/client_count":
            self.server.publish(topic, self.make_clients(), only=conn)

    # ---- 직진 (straight_drive_node 의 단순판) ------------------------------------------------
    def report(self, data):
        self.server.publish("/straight_drive/status", {"data": json.dumps(data)})

    def imu_yaw(self):
        return self.true[2] + self.imu_offset

    def straight_goal(self, dist, speed):
        if dist <= 0:
            self.report({"state": "rejected", "reason": "distance <= 0"})
            return
        p = self.params["/straight_drive"]
        speed = min(abs(speed) or p["default_speed"][1], p["max_speed"][1])
        self.straight = {"distance": dist, "speed": speed, "x0": self.est[0], "y0": self.est[1], "th0": self.est[2],
                         "imu0": self.imu_yaw(), "px": self.est[0], "py": self.est[1], "along": 0.0, "cross": 0.0,
                         "v": 0.0, "t0": time.perf_counter(), "timeout": dist / speed * 2.0 + 5.0, "steps": 0}
        self.report({"state": "started", "distance": dist, "speed": speed, "imu": True})

    def heading_error(self, g):
        a = self.params["/straight_drive"]["imu_weight"][1]
        d_odom = wrap(self.est[2] - g["th0"])
        d_imu = wrap(self.imu_yaw() - g["imu0"])
        return -(a * d_imu + (1 - a) * d_odom)

    def heading_deg(self, e):
        return None if self.args.null_heading else round(math.degrees(e), 2)

    def straight_step(self, now):
        g, p = self.straight, {k: v[1] for k, v in self.params["/straight_drive"].items()}
        if now - g["t0"] > g["timeout"]:
            return self.straight_finish("time_limit")
        e = self.heading_error(g)
        ds = math.hypot(self.est[0] - g["px"], self.est[1] - g["py"])
        g["px"], g["py"] = self.est[0], self.est[1]
        g["along"] += ds * math.cos(-e)
        g["cross"] += ds * math.sin(-e)
        remaining = g["distance"] - g["along"]
        if remaining <= p["stop_tolerance"]:
            return self.straight_finish("reached")
        v_stop = math.sqrt(max(0.0, 2 * p["accel"] * remaining))
        g["v"] = min(g["speed"], g["v"] + p["accel"] * 0.05, max(v_stop, 0.02))
        w = max(-p["max_angular"], min(p["max_angular"], p["k_heading"] * e - p["k_cross"] * g["cross"]))
        self.cmd = (g["v"], w)
        g["steps"] += 1
        if g["steps"] % 2 == 0:   # 약 10 Hz
            self.report({"state": "running", "along": round(g["along"], 3), "cross": round(g["cross"], 4),
                         "heading_err_deg": self.heading_deg(e), "v": round(g["v"], 3), "w": round(w, 3)})

    def straight_finish(self, reason):
        g, self.straight = self.straight, None
        if g is None:
            return
        self.cmd = (0.0, 0.0)
        self.report({"state": "finished", "reason": reason, "along": round(g["along"], 3), "cross": round(g["cross"], 4),
                     "heading_err_deg": self.heading_deg(self.heading_error(g)),
                     "duration": round(time.perf_counter() - g["t0"], 2), "log": None})

    # ---- 이동 목표 (/goal_pose → /plan, 경로 추종, 액션 상태) -----------------------------------
    def set_goal(self, msg):
        frame = msg.get("header", {}).get("frame_id", "map")
        p = msg.get("pose", {})
        g = (p.get("position", {}).get("x", 0.0), p.get("position", {}).get("y", 0.0), yaw_of(p.get("orientation", {})))
        if frame == "map" and self.map_odom:
            g = compose(inverse(self.map_odom), g)        # odom 프레임으로
        self.goal_seq += 1
        uuid = [self.goal_seq % 256] + [0] * 15
        self.goal = {"pose": g, "uuid": uuid}
        self.goal_status = [(uuid, 2)]
        # 직선 경로를 0.1 m 간격으로 한 번 발행
        pf = self.args.plan_frame
        x0, y0 = self.est[0], self.est[1]
        n = max(1, int(math.hypot(g[0] - x0, g[1] - y0) / 0.1))
        poses = []
        for i in range(n + 1):
            pt = (x0 + (g[0] - x0) * i / n, y0 + (g[1] - y0) * i / n, g[2])
            if pf == "map" and self.map_odom:
                pt = compose(self.map_odom, pt)
            poses.append({"header": self.header(pf), "pose": {"position": {"x": pt[0], "y": pt[1], "z": 0.0},
                                                              "orientation": quat(pt[2])}})
        self.last_plan = {"header": self.header(pf), "poses": poses}
        self.server.publish("/plan", self.last_plan)
        if self.args.nav:
            self.server.publish("/navigate_to_pose/_action/status", self.make_action_status())

    def goal_step(self):
        gx, gy, _ = self.goal["pose"]
        dx, dy = gx - self.est[0], gy - self.est[1]
        dist = math.hypot(dx, dy)
        if dist < 0.05:
            return self.goal_end(4)
        err = wrap(math.atan2(dy, dx) - self.est[2])
        w = max(-MAX_W, min(MAX_W, 1.5 * err))
        self.cmd = (0.0 if abs(err) > 0.3 else min(MAX_V, dist), w)

    def goal_end(self, status):
        if not self.goal:
            return False
        self.goal_status = [(self.goal["uuid"], status)]
        self.goal = None
        self.cmd = (0.0, 0.0)
        if self.args.nav:
            self.server.publish("/navigate_to_pose/_action/status", self.make_action_status())
        return True

    # ---- 메시지 ------------------------------------------------------------------------
    def make_odom(self):
        cov = [0.0] * 36
        if self.args.odom_cov:
            for i, v in zip((0, 7, 14, 21, 28, 35), (0.001, 0.001, 1e6, 1e6, 1e6, 0.03)):
                cov[i] = v
        return {"header": self.header("odom"), "child_frame_id": "base_link",
                "pose": {"pose": {"position": {"x": self.est[0], "y": self.est[1], "z": 0.0},
                                  "orientation": quat(self.est[2])}, "covariance": cov},
                "twist": {"twist": {"linear": {"x": self.v_est, "y": 0.0, "z": 0.0},
                                    "angular": {"x": 0.0, "y": 0.0, "z": self.w_est}}, "covariance": [0.0] * 36}}

    def tf(self, parent, child, pose, z=0.0):
        return {"header": self.header(parent), "child_frame_id": child,
                "transform": {"translation": {"x": pose[0], "y": pose[1], "z": z}, "rotation": quat(pose[2])}}

    def make_tf_odom(self):
        return {"transforms": [self.tf("odom", "base_link", self.est)]}

    def make_tf_map(self):
        if not self.map_odom:
            return None
        return {"transforms": [self.tf("map", "odom", self.map_odom)]}

    def static_tfs(self):
        """실기 /tf_static: 변환 3 개가 각각 따로 온다."""
        return [{"transforms": [self.tf("base_link", "base_footprint", (0.0, 0.0, 0.0))]},
                {"transforms": [self.tf("base_link", "laser", LASER[:2] + (math.radians(self.args.laser_yaw_deg),),
                                        LASER[2])]},
                {"transforms": [self.tf("base_link", "imu", IMU[:2] + (0.0,), IMU[2])]}]

    def make_tf_static(self):
        return self.static_tfs()[1]

    def make_joint(self, vel):
        return {"header": self.header(""), "name": ["left_wheel_joint", "right_wheel_joint"],
                "position": [], "velocity": list(vel), "effort": []}

    def make_imu(self):
        yaw = self.imu_yaw() + self.rng.gauss(0, self.imu_noise)
        return {"header": self.header("imu"), "orientation": quat(yaw), "orientation_covariance": [0.0] * 9,
                "angular_velocity": {"x": 0.0, "y": 0.0, "z": self.w_true}, "angular_velocity_covariance": [0.0] * 9,
                "linear_acceleration": {"x": self.accel_x, "y": 0.0, "z": 9.81},
                "linear_acceleration_covariance": [0.0] * 9}

    def make_scan(self):
        a = self.args
        n = a.scan_rays
        # 실기 1800 광선: angle_min -3.124, angle_increment 0.003483 (한 바퀴를 조금 덜 채운다)
        a0, inc = (-3.124, 0.003483) if n == 1800 else (-math.pi, 2 * math.pi / n)
        ox, oy, yaw = self.true
        if a.scan_frame != "base_link":    # laser 프레임이면 장착 위치에서 잰다
            c, s = math.cos(yaw), math.sin(yaw)
            ox, oy = ox + c * LASER[0] - s * LASER[1], oy + s * LASER[0] + c * LASER[1]
            yaw += math.radians(a.laser_yaw_deg)
        ranges = self.room.raycast(ox, oy, yaw + a0, inc, n, a.scan_max)
        rng = self.rng
        for i, r in enumerate(ranges):
            if r is None:
                continue
            if a.scan_dropout and rng.random() < a.scan_dropout:
                ranges[i] = None
            elif a.scan_noise:
                ranges[i] = r + rng.gauss(0, a.scan_noise)
        if a.self_hit:   # 몸체 기둥에 맞은 근거리 반사
            for i in range(0, n, max(1, n // 8)):
                ranges[i] = 0.12 + rng.random() * 0.05
        intens = [0.0 if r is None else 47.0 for r in ranges]
        return {"header": self.header(a.scan_frame), "angle_min": a0, "angle_max": a0 + (n - 1) * inc,
                "angle_increment": inc, "time_increment": 0.1 / n, "scan_time": 0.1, "range_min": 0.15,
                "range_max": a.scan_max, "ranges": ranges, "intensities": intens}

    def make_beat(self):
        self.beat += 1
        return {"data": f"{self.host} seq={self.beat} time={time.time():.3f}"}

    def make_rosout(self):
        self.rosout_n += 1
        n = self.rosout_n
        level, name, text = 20, "straight_drive", "ready"
        if n % 30 == 0:
            level, name, text = 40, "control_node", "serial read timeout (1.0 s)"
        elif n % 10 == 0:
            level, name, text = 30, "cmd_watchdog", "/teleop/cmd_vel silent for 0.5s: stopping robot"
        return {"stamp": self.stamp(), "level": level, "name": name, "msg": f"{text} #{n}", "file": f"{name}.py",
                "function": "step", "line": 42}

    def make_diag(self):
        if self.args.diag_stale and time.perf_counter() - self.t0 > self.args.diag_stale:
            return None
        return {"header": self.header(""), "status": [
            {"level": 0, "name": "control_node: pico link", "message": "OK", "hardware_id": "pico",
             "values": [{"key": "rx_hz", "value": "33.0"}, {"key": "errors", "value": "0"}]},
            {"level": 0, "name": "sllidar_node: lidar", "message": "OK", "hardware_id": "rplidar",
             "values": [{"key": "scan_hz", "value": "10.0"}]},
            {"level": 1 if self.rosout_n % 20 == 0 else 0, "name": "system: cpu", "message": "load",
             "hardware_id": self.host, "values": [{"key": "cpu_percent", "value": "41"}]}]}

    def make_clients(self):
        n = self.args.clients if self.args.clients is not None else len(self.server.conns)
        return {"data": n}

    # ---- 지도 ---------------------------------------------------------------------------
    def _map_geometry(self):
        a = self.args
        yaw = math.radians(a.map_origin_yaw_deg)
        xmin, ymin, xmax, ymax = self.room.bounds
        if a.map_size:
            w, h = (int(v) for v in a.map_size.lower().split("x"))
            if max(w, h) > a.map_max:
                raise SystemExit(f"--map-size {a.map_size} 가 상한 {a.map_max} 를 넘음(--map-max 로 올린다)")
        else:
            w, h = int(round((xmax - xmin + 1.0) / RES)), int(round((ymax - ymin + 1.0) / RES))
        cx, cy = (xmin + xmax) / 2, (ymin + ymax) / 2
        hw, hh = w * RES / 2, h * RES / 2
        c, s = math.cos(yaw), math.sin(yaw)
        return w, h, (cx - c * hw + s * hh, cy - s * hw - c * hh, yaw)

    def grid_msg(self, w, h, origin, frame="map", data=None):
        if data is None:
            data = Raw(self.room.grid(w, h, *origin)[0])
        return {"header": self.header(frame),
                "info": {"map_load_time": self.stamp(), "resolution": RES, "width": w, "height": h,
                         "origin": {"position": {"x": origin[0], "y": origin[1], "z": 0.0}, "orientation": quat(origin[2])}},
                "data": data}

    def make_map(self):
        return self.grid_msg(self.map_w, self.map_h, self.map_origin)

    def grow_map(self):
        d = 40
        ox, oy, yaw = self.map_origin
        self.map_w += d
        self.map_origin = (ox - d * RES * math.cos(yaw), oy - d * RES * math.sin(yaw), yaw)

    def make_global_costmap(self):
        return self.grid_msg(self.map_w, self.map_h, self.map_origin)

    def make_local_costmap(self):
        n = 60
        ox, oy = round(self.est[0] / RES) * RES - n * RES / 2, round(self.est[1] / RES) * RES - n * RES / 2
        return self.grid_msg(n, n, (ox, oy, 0.0), frame="odom")

    def make_footprint(self):
        pts = [compose(tuple(self.est), (x, y, 0.0)) for x, y in ((0.2, 0.17), (-0.2, 0.17), (-0.2, -0.17), (0.2, -0.17))]
        return {"header": self.header("odom"), "polygon": {"points": [{"x": p[0], "y": p[1], "z": 0.0} for p in pts]}}

    def make_amcl(self):
        p = compose(self.map_odom or (0.0, 0.0, 0.0), tuple(self.est))
        cov = [0.0] * 36
        cov[0] = cov[7] = 0.01
        cov[35] = 0.02
        return {"header": self.header("map"), "pose": {"pose": {"position": {"x": p[0], "y": p[1], "z": 0.0},
                                                                "orientation": quat(p[2])}, "covariance": cov}}

    def make_action_status(self):
        return {"status_list": [{"goal_info": {"goal_id": {"uuid": u}, "stamp": self.stamp()}, "status": s}
                                for u, s in self.goal_status]}

    # ---- 돌리기 -------------------------------------------------------------------------
    async def run(self):
        server = self.server
        now = time.perf_counter()
        sim_t, step = now, 0
        nxt = {k: now for k, _, _ in self.schedule}
        nxt["map_odom"] = now + 0.013     # odom→base_link 과 같은 틱에 맞물려 늘 살아남지 않게(실제 노드는 따로 돈다)
        while True:
            now = time.perf_counter()
            while sim_t + DT <= now:
                sim_t += DT
                self.physics()
                step += 1
                if step % 5 == 0:
                    self.controllers()
            if now - sim_t > 1.0:     # 너무 밀리면 따라잡지 않는다
                sim_t = now
            for key, period, maker in self.schedule:
                if nxt[key] > now:
                    continue
                topic = {"map_odom": "/tf", "map_grow": "/map"}.get(key, key)
                while nxt[key] <= now:
                    nxt[key] += period
                    if key == "map_grow":
                        self.grow_map()
                        maker = self.make_map
                    if topic in self.stopped or (not server.wants(topic) and key != "/initialpose"):
                        continue
                    msg = maker()
                    if msg is not None:
                        server.publish(topic, msg)
                if now - nxt[key] > 1.0:
                    nxt[key] = now + period
            server.flush()
            await asyncio.sleep(0.005)


def make_params():
    """가짜 노드 매개변수: 이름 -> (형식, 값, 범위). 형식 1 bool, 2 int, 3 double, 4 string."""
    return {
        "/straight_drive": {
            "rate_hz": (3, 20.0, (1.0, 50.0)), "default_speed": (3, 0.08, (0.0, 0.1)), "max_speed": (3, 0.1, (0.0, 0.1)),
            "max_angular": (3, 0.25, (0.0, 0.25)), "accel": (3, 0.1, (0.01, 1.0)), "imu_weight": (3, 0.7, (0.0, 1.0)),
            "k_heading": (3, 1.5, (0.0, 10.0)), "k_cross": (3, 2.0, (0.0, 10.0)),
            "stop_tolerance": (3, 0.01, (0.0, 0.1)), "sensor_pause": (3, 0.3, (0.0, 5.0)),
            "sensor_timeout": (3, 1.5, (0.0, 10.0)), "use_imu": (1, True, None),
            "log_dir": (4, "~/robot_link/logs", None), "use_sim_time": (1, False, None)},
        "/cmd_watchdog": {"timeout": (3, 0.5, (0.05, 5.0)), "use_sim_time": (1, False, None),
                          "zero_repeats": (2, 5, (0, 20))},
    }
