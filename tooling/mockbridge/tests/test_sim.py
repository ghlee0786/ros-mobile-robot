"""시뮬레이션 · 서비스 계약: 워치독, 직진 status, 경로 목표, 지도, 매개변수, /scan 계산 시간."""
import json
import math
import time
import types

from mockbridge.room import Room
from mockbridge.sim import World

from conftest import ROOT


def twist(v, w=0.0):
    return {"linear": {"x": v, "y": 0.0, "z": 0.0}, "angular": {"x": 0.0, "y": 0.0, "z": w}}


def test_watchdog_stops_after_silence(mock, client):
    c = client(mock().port)
    c.send(op="advertise", topic="/teleop/cmd_vel", type="geometry_msgs/Twist")
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    for _ in range(5):
        c.send(op="publish", topic="/teleop/cmd_vel", msg=twist(0.1))
        time.sleep(0.1)
    moving = c.until(lambda m: m.get("topic") == "/odom" and m["msg"]["twist"]["twist"]["linear"]["x"] > 0.05)
    assert moving
    c.collect(1.0)             # 0.5 s 무명령 → 워치독이 세운다(그동안 온 것은 버린다)
    odom = c.collect(0.3, lambda m: m.get("topic") == "/odom")
    assert odom and all(abs(m["msg"]["twist"]["twist"]["linear"]["x"]) < 1e-9 for _, m in odom)


def test_watchdog_subscribers(mock, client):
    c = client(mock().port)
    assert c.call("/rosapi/subscribers", {"topic": "/teleop/cmd_vel"})["values"]["subscribers"] == ["/cmd_watchdog"]
    c2 = client(mock("--no-watchdog").port)
    assert c2.call("/rosapi/subscribers", {"topic": "/teleop/cmd_vel"})["values"]["subscribers"] == []


def statuses(c, secs):
    out = []
    for _, m in c.collect(secs, lambda m: m.get("topic") == "/straight_drive/status"):
        out.append(json.loads(m["msg"]["data"]))
        if out[-1]["state"] in ("finished", "rejected"):
            break
    return out


def test_straight_status_order(mock, client):
    c = client(mock().port)
    c.send(op="advertise", topic="/straight_drive/goal", type="geometry_msgs/Vector3")
    c.send(op="advertise", topic="/straight_drive/cancel", type="std_msgs/Empty")
    c.send(op="subscribe", topic="/straight_drive/status", type="std_msgs/String")
    time.sleep(0.2)
    c.send(op="publish", topic="/straight_drive/goal", msg={"x": 0.0, "y": 0.1, "z": 0.0})
    assert statuses(c, 2)[-1]["state"] == "rejected"
    c.send(op="publish", topic="/straight_drive/goal", msg={"x": 0.15, "y": 0.1, "z": 0.0})
    st = statuses(c, 8)
    names = [s["state"] for s in st]
    assert names[0] == "started" and names[-1] == "finished" and "running" in names
    assert set(names[1:-1]) == {"running"}
    assert st[-1]["reason"] == "reached" and st[-1]["along"] >= 0.14
    assert isinstance(st[1]["heading_err_deg"], float)
    c.send(op="publish", topic="/straight_drive/goal", msg={"x": 1.0, "y": 0.1, "z": 0.0})
    time.sleep(0.5)
    c.send(op="publish", topic="/straight_drive/cancel", msg={})
    st = statuses(c, 3)
    assert st[-1]["state"] == "finished" and st[-1]["reason"] == "canceled"


def test_straight_null_heading(mock, client):
    c = client(mock("--null-heading").port)
    c.send(op="advertise", topic="/straight_drive/goal", type="geometry_msgs/Vector3")
    c.send(op="subscribe", topic="/straight_drive/status", type="std_msgs/String")
    time.sleep(0.2)
    c.send(op="publish", topic="/straight_drive/goal", msg={"x": 0.1, "y": 0.1, "z": 0.0})
    st = statuses(c, 6)
    assert st[-1]["state"] == "finished" and st[-1]["heading_err_deg"] is None
    assert all(s["heading_err_deg"] is None for s in st if s["state"] == "running")


def test_goal_plan_cancel(mock, client):
    c = client(mock("--map-odom", "1,2,30", "--nav").port)
    for t, ty in (("/plan", "nav_msgs/Path"), ("/navigate_to_pose/_action/status", "action_msgs/GoalStatusArray"),
                  ("/amcl_pose", "geometry_msgs/PoseWithCovarianceStamped")):
        c.send(op="subscribe", topic=t, type=ty)
    c.send(op="advertise", topic="/goal_pose", type="geometry_msgs/PoseStamped")
    time.sleep(0.2)
    c.send(op="publish", topic="/goal_pose", msg={"header": {"frame_id": "map", "stamp": {"sec": 0, "nanosec": 0}},
                                                  "pose": {"position": {"x": 1.5, "y": 2.0, "z": 0},
                                                           "orientation": {"x": 0, "y": 0, "z": 0, "w": 1}}})
    plan = c.until(lambda m: m.get("topic") == "/plan")
    assert plan["msg"]["header"]["frame_id"] == "map"
    end = plan["msg"]["poses"][-1]["pose"]["position"]
    assert math.hypot(end["x"] - 1.5, end["y"] - 2.0) < 1e-6
    start = plan["msg"]["poses"][0]["pose"]["position"]
    assert math.hypot(start["x"] - 1.0, start["y"] - 2.0) < 0.05      # 로봇(odom 0,0)은 map (1,2)
    assert c.until(lambda m: m.get("topic") == "/navigate_to_pose/_action/status"
                   and m["msg"]["status_list"] and m["msg"]["status_list"][0]["status"] == 2)
    assert c.until(lambda m: m.get("topic") == "/amcl_pose")["msg"]["header"]["frame_id"] == "map"
    assert c.call("/navigate_to_pose/_action/cancel_goal", {})["result"] is True
    assert c.until(lambda m: m.get("topic") == "/navigate_to_pose/_action/status"
                   and m["msg"]["status_list"] and m["msg"]["status_list"][0]["status"] == 6)


def test_map_once_and_size(mock, client):
    c = client(mock("--map-size", "400x300", "--map-origin-yaw-deg", "10").port)
    c.send(op="subscribe", topic="/map", type="nav_msgs/OccupancyGrid")
    got = c.collect(1.5, lambda m: m.get("topic") == "/map")
    assert len(got) == 1                     # 구독할 때 한 번
    m = got[0][1]["msg"]
    assert (m["info"]["width"], m["info"]["height"]) == (400, 300) and len(m["data"]) == 400 * 300
    assert set(m["data"]) == {-1, 0, 100}


def test_map_grow(mock, client):
    c = client(mock("--map-grow").port)
    c.send(op="subscribe", topic="/map", type="nav_msgs/OccupancyGrid")
    got = c.collect(4.5, lambda m: m.get("topic") == "/map")
    widths = [m["msg"]["info"]["width"] for _, m in got]
    xs = [m["msg"]["info"]["origin"]["position"]["x"] for _, m in got]
    assert len(got) >= 2 and widths == sorted(widths) and widths[-1] > widths[0] and xs[-1] < xs[0]


def test_tf_static_and_heartbeat(mock, client):
    c = client(mock("--laser-yaw-deg", "180").port)
    # 실기처럼 throttle 0 · queue 10 으로 늦게 구독해도 변환 3 개가 각각 따로 온다
    c.send(op="subscribe", topic="/tf_static", type="tf2_msgs/TFMessage", queue_length=10)
    msgs = [m["msg"]["transforms"] for _, m in c.collect(1.0, lambda m: m.get("topic") == "/tf_static")]
    assert [len(t) for t in msgs] == [1, 1, 1]
    tfs = {t[0]["child_frame_id"]: t[0] for t in msgs}
    assert set(tfs) == {"base_footprint", "laser", "imu"} and all(t["header"]["frame_id"] == "base_link" for t in tfs.values())
    tr = tfs["laser"]["transform"]
    assert (tr["translation"]["x"], tr["translation"]["z"]) == (-0.12, 0.28)
    assert abs(abs(tr["rotation"]["z"]) - 1.0) < 1e-9
    assert tfs["imu"]["transform"]["translation"]["z"] == 0.18
    c.send(op="subscribe", topic="/link/heartbeat", type="std_msgs/String")
    beat = c.until(lambda m: m.get("topic") == "/link/heartbeat")["msg"]["data"]
    assert beat.startswith("mockpi seq=") and " time=" in beat


def test_parameters(mock, client):
    c = client(mock().port)
    names = c.call("/straight_drive/list_parameters", {"prefixes": [], "depth": 0})["values"]["result"]["names"]
    assert "k_heading" in names and "use_imu" in names
    d = c.call("/straight_drive/describe_parameters", {"names": ["k_heading"]})["values"]["descriptors"][0]
    assert d["type"] == 3 and d["floating_point_range"][0]["to_value"] == 10.0
    v = c.call("/cmd_watchdog/get_parameters", {"names": ["timeout"]})["values"]["values"][0]
    assert v["type"] == 3 and v["double_value"] == 0.5
    r = c.call("/cmd_watchdog/set_parameters", {"parameters": [
        {"name": "timeout", "value": {"type": 3, "double_value": 1.0}},
        {"name": "timeout", "value": {"type": 3, "double_value": 99.0}},
        {"name": "timeout", "value": {"type": 4, "string_value": "x"}}]})["values"]["results"]
    assert [x["successful"] for x in r] == [True, False, False]
    assert c.call("/cmd_watchdog/get_parameters", {"names": ["timeout"]})["values"]["values"][0]["double_value"] == 1.0
    services = c.call("/rosapi/services")["values"]["services"]
    assert "/slam_toolbox/save_map" in services and "/straight_drive/set_parameters" in services
    nd = c.call("/rosapi/node_details", {"node": "/cmd_watchdog"})["values"]
    assert nd["subscribing"] == ["/teleop/cmd_vel"] and "/cmd_watchdog/get_parameters" in nd["services"]
    assert c.call("/slam_toolbox/save_map", {"name": {"data": "x"}})["result"] is True


def test_scan_cost_one_core():
    """1800 광선 10 Hz 가 한 코어에서 버티는지(한 장 50 ms 미만 = 코어 절반 미만)."""
    args = types.SimpleNamespace(seed=1, host_name="t", hide_topic=None, stamp_skew_ms=0, left_gain=0.95,
                                 encoder_scale=1.01, imu_offset=1.0, imu_noise=0.0, freeze_pose=False, map_odom=None,
                                 map_origin_yaw_deg=0, map_size=None, map_max=2000, nav=False, map_every=0,
                                 map_grow=False, rosout_burst=0, scan_rays=1800, scan_max=12.0, scan_noise=0.01,
                                 scan_dropout=0.01, self_hit=True, laser_yaw_deg=0.0, scan_frame="base_link", pi_initialpose=False)
    w = World(types.SimpleNamespace(conns=set()), args, Room.load(f"{ROOT}/rooms/default.json"))
    t = time.perf_counter()
    for i in range(20):
        w.true[0] = i * 0.05
        scan = w.make_scan()
    ms = (time.perf_counter() - t) / 20 * 1000
    print(f"/scan 1800 광선 한 장 {ms:.1f} ms")
    assert len(scan["ranges"]) == 1800 and ms < 50


def amcl_xy(c):
    p = c.until(lambda m: m.get("topic") == "/amcl_pose")["msg"]["pose"]["pose"]["position"]
    return p["x"], p["y"]


def test_dashboard_initialpose_moves_map_odom(mock, client):
    """대시보드의 /dashboard/initialpose 를 받으면 --map-odom 이 그에 맞게 바뀐다."""
    c = client(mock("--map-odom", "1,2,30", "--nav").port)
    c.send(op="subscribe", topic="/amcl_pose", type="geometry_msgs/PoseWithCovarianceStamped")
    assert math.hypot(amcl_xy(c)[0] - 1, amcl_xy(c)[1] - 2) < 0.01
    c.send(op="advertise", topic="/dashboard/initialpose", type="geometry_msgs/PoseWithCovarianceStamped")
    c.send(op="publish", topic="/dashboard/initialpose", msg={"header": {"frame_id": "map"}, "pose": {"pose": {
        "position": {"x": -0.5, "y": 0.75, "z": 0}, "orientation": {"x": 0, "y": 0, "z": 0, "w": 1}}}})
    c.collect(0.3)
    x, y = amcl_xy(c)
    assert math.hypot(x + 0.5, y - 0.75) < 0.01


def test_pi_initialpose(mock, client):
    """--pi-initialpose: 파이 initialpose_node 처럼 /initialpose 를 map 원점으로 발행하고 적용한다."""
    c = client(mock("--map-odom", "1,2,30", "--nav", "--pi-initialpose").port)
    c.send(op="subscribe", topic="/initialpose", type="geometry_msgs/PoseWithCovarianceStamped")
    c.send(op="subscribe", topic="/amcl_pose", type="geometry_msgs/PoseWithCovarianceStamped")
    ip = c.until(lambda m: m.get("topic") == "/initialpose", 6.0)
    assert ip["msg"]["header"]["frame_id"] == "map" and ip["msg"]["pose"]["pose"]["position"]["x"] == 0.0
    c.collect(0.3)
    assert math.hypot(*amcl_xy(c)) < 0.01


def test_scan_compact(mock, client):
    """/scan_compact: 기본으로 토픽 목록에 있고 /scan 과 같은 스캔을 mm 정수로 낸다. --no-scan-compact 면 목록에서도 빠진다."""
    c = client(mock("--freeze-pose").port)
    topics = c.call("/rosapi/topics")["values"]
    assert topics["types"][topics["topics"].index("/scan_compact")] == "std_msgs/msg/UInt16MultiArray"
    c.send(op="subscribe", topic="/scan", type="sensor_msgs/LaserScan")
    c.send(op="subscribe", topic="/scan_compact", type="std_msgs/UInt16MultiArray")
    scan = c.until(lambda m: m.get("topic") == "/scan")["msg"]
    comp = c.until(lambda m: m.get("topic") == "/scan_compact")["msg"]
    f = comp["layout"]["dim"][0]["label"].split(" ")
    assert len(f) == 7 and f[0] == scan["header"]["frame_id"]
    assert abs(float(f[1]) - scan["angle_min"]) < 1e-6 and abs(float(f[2]) - scan["angle_increment"]) < 1e-8
    assert float(f[3]) == scan["range_min"] and float(f[4]) == scan["range_max"]
    assert comp["layout"]["dim"][0]["size"] == len(comp["data"]) == len(scan["ranges"])
    for r, mm in zip(scan["ranges"], comp["data"]):   # 멈춘 로봇이라 두 스캔이 같다
        if r is None or not scan["range_min"] <= r <= scan["range_max"]:
            assert mm == 0
        else:
            assert mm == int(r * 1000)
    c2 = client(mock("--no-scan-compact").port)
    assert "/scan_compact" not in c2.call("/rosapi/topics")["values"]["topics"]
