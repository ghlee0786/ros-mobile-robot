#include "robot.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QtConcurrent>
#include <QtMath>

namespace {

struct SensorDef { const char *key, *label, *topic, *type; bool on; int throttle; };
// throttle (ms) is applied by rosbridge on the Pi. Its Python server tops out at one core, and at
// full rate (/imu 100 Hz, /tf 33 Hz on top of /scan, /odom, wheels) every topic dropped to ~22 Hz;
// the IMU graph and the TF list do not need more than this, so the pose and wheel data stay full rate.
const SensorDef kSensors[] = {
    {"scan", "LiDAR", "/scan", "sensor_msgs/LaserScan", true, 0},
    {"imu", "IMU", "/imu/data_raw", "sensor_msgs/Imu", true, 50},
    {"odom", "오도메트리 (Pico 엔코더)", "/odom", "nav_msgs/Odometry", true, 0},
    {"wheel", "바퀴 속도", "/wheel_states", "sensor_msgs/JointState", true, 0},
    {"tf", "좌표 변환", "/tf", "tf2_msgs/TFMessage", true, 200},
    {"beat", "링크 하트비트", "/link/heartbeat", "std_msgs/String", true, 0},
    {"cam", "카메라", "/camera/image_raw", "sensor_msgs/Image", false, 1000},
};
// 센서를 켜면 함께 구독하는 토픽
const QHash<QString, QList<QPair<QString, QString>>> kExtra = {
    {"wheel", {{"/wheel_cmd", "sensor_msgs/JointState"}}},
    // /map_odom: map→odom 만 PC 내비 스택이 5 Hz 로 보낸다(tooling/nav/map_odom_pub.py). /tf 를 통째로 되돌려
    // 보내면 로봇 /tf 가 PC 로 갔다 다시 돌아오는 고리가 생겨, map→odom 은 이 토픽으로 따로 받는다.
    {"tf", {{"/tf_static", "tf2_msgs/TFMessage"}, {"/map_odom", "tf2_msgs/TFMessage"}}},
};
const char *kRequired[] = {"/odom", "/scan", "/wheel_states"};
constexpr double kSelfHide = 0.3;     // 로봇 몸체에 맞은 LiDAR 점(m 이내)은 숨김
constexpr double kMaxAngular = 0.25;  // 펌웨어 한계 rad/s
constexpr double kMaxLinear = 0.10;   // 펌웨어 한계 m/s
constexpr double kMaxStraight = 2.0;  // 직진 시험 거리 상한 m
// /tf 는 throttle 200 ms · queue 1 이라 창마다 마지막 변환 하나만 온다. map→odom 은 평균 약 0.66 Hz 로
// 불규칙하게 오므로 2 s 로 판정하면 24 % 동안 낡음으로 본다 — 10 s 안에 한 번이라도 왔으면 신선하다고 본다.
constexpr qint64 kMapOdomFreshMs = 10000;
constexpr int kMapBinaryThreshold = 65;
// 파이의 initialpose_node 가 5 s 마다 /initialpose 에 (0,0,0) 을 내므로 대시보드는 따로 보내고,
// PC 의 split_relay 가 이것을 Nav2 도메인의 /initialpose 로 넘긴다.
constexpr char kInitialPoseTopic[] = "/dashboard/initialpose";
// 파이 scan_compact_node: /scan 을 mm 정수(UInt16MultiArray)로 다시 낸다(rosbridge 송신량 약 1/4).
// 토픽 목록에 있으면 LiDAR 센서는 /scan 대신 이것을 구독한다. throttle 하한은 /scan 과 같다(없음).
constexpr char kScanCompact[] = "/scan_compact";
constexpr char kScanCompactType[] = "std_msgs/UInt16MultiArray";

} // namespace

using namespace msgparse;

Robot::Robot(QObject *parent) : QObject(parent)
{
    m_clock.start();
    connect(&m_bridge, &RosBridge::message, this, &Robot::onMessage);
    connect(&m_bridge, &RosBridge::bridgeError, this, &Robot::addEvent);
    connect(&m_bridge, &RosBridge::topicListChanged, this, &Robot::pickScanTopic);
    connect(&m_bridge, &RosBridge::connectedChanged, this, [this] {
        armTeleop(false);   // 연결 · 재연결 · 끊김 직후에는 잠근다
        if (m_bridge.connected()) {
            m_everConnected = true;
            m_lastSendDropped = false;
            m_connClock.start();
            addEvent("연결됨 " + m_bridge.host());
            // 끊긴(또는 반쯤 열린) 동안 누른 정지는 닿았는지 알 수 없다 — 화면이 '정지' 면 다시 보낸다.
            // advertise 는 connectedChanged 뒤에 나가므로 이벤트 루프 다음 턴에 보낸다.
            if (m_mode == "정지")
                QTimer::singleShot(0, this, [this] {
                    if (m_bridge.connected() && m_mode == "정지") {
                        sendStop();
                        addEvent("재연결 — 정지 명령을 다시 보냄");
                    }
                });
            QSettings s;
            QStringList hosts = s.value("hosts").toStringList();
            if (!hosts.contains(m_bridge.host()) && m_bridge.host() != "192.168.1.201" && m_bridge.host() != "192.168.1.200") {
                hosts.append(m_bridge.host());
                s.setValue("hosts", hosts);
                emit savedChanged();
            }
        } else {
            addEvent("연결 끊김 " + m_bridge.host() + " · 3 s 마다 재연결");
        }
        updateStatus();
    });
    // 앱을 닫을 때: 0 속도를 보내고 보낼 것을 내보낸 뒤 닫는다.
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
            for (int i = 0; i < 3; ++i)
                sendTwist(0, 0);
            m_bridge.shutdown();
        });
    if (m_bridge.noPublish())
        addEvent("ROBOT_DASHBOARD_NO_PUBLISH=1 — 로봇에 명령(advertise · publish)을 보내지 않음");

    for (const SensorDef &d : kSensors) {
        m_sensorOn[d.key] = d.on;
        if (d.on)
            setSensor(d.key, true);
    }
    m_bridge.want("/straight_drive/status", "std_msgs/String", "base", true);
    m_bridge.want("/plan", "nav_msgs/Path", "base", true);
    m_bridge.advertise("/teleop/cmd_vel", "geometry_msgs/Twist");
    m_bridge.advertise("/straight_drive/goal", "geometry_msgs/Vector3");
    m_bridge.advertise("/straight_drive/cancel", "std_msgs/Empty");
    m_bridge.advertise("/goal_pose", "geometry_msgs/PoseStamped");
    m_bridge.advertise(kInitialPoseTopic, "geometry_msgs/PoseWithCovarianceStamped");
    m_recordTopics = {"/odom", "/scan", "/imu/data_raw", "/wheel_states"};

    m_ui.setInterval(33);
    connect(&m_ui, &QTimer::timeout, this, [this] {
        if (m_scanDirty) {
            m_scanDirty = false;
            emit scanChanged();
        }
        if (!m_dirty)
            return;
        m_dirty = false;
        emit telemetryChanged();
        emit sampled(m_clock.elapsed() / 1000.0, m_cmdL, m_cmdR, m_wheelL, m_wheelR, m_gyroZ, m_accelX);
    });
    m_ui.start();

    m_status.setInterval(500);
    connect(&m_status, &QTimer::timeout, this, &Robot::updateStatus);
    m_status.start();

    // 키보드 조종 중에는 0 을 포함해 10 Hz 로 계속 보낸다 — 끊기면 파이 워치독이 0.5 s 안에 세운다.
    m_teleop.setInterval(100);
    m_teleop.setTimerType(Qt::PreciseTimer);   // CoarseTimer 는 Windows 에서 109 ms(9.1 Hz)로 나갔다
    connect(&m_teleop, &QTimer::timeout, this, [this] {
        if (m_armed && (m_lin != 0 || m_ang != 0))
            m_armInput.restart();   // 누르고 있는 동안은 입력 중
        else if (m_armed && m_armInput.elapsed() > m_armIdleMs)
            armTeleop(false);
        if (m_mode == "키보드 조종")
            sendTwist(m_lin * m_maxSpeed, m_ang * kMaxAngular);
    });
    m_teleop.start();
    updateStatus();
}

Robot::~Robot()
{
    sendTwist(0, 0);
    stopRecording();
}

// ---- 연결 · 설정 -----------------------------------------------------------------------

QVariantList Robot::savedRobots() const
{
    QVariantList l{QVariantMap{{"name", "robot1 · Wi-Fi"}, {"host", "192.168.1.201"}},
                   QVariantMap{{"name", "robot1 · 유선"}, {"host", "192.168.1.200"}}};
    for (const QString &h : QSettings().value("hosts").toStringList())
        l.append(QVariantMap{{"name", "저장됨"}, {"host", h}});
    return l;
}

QString Robot::lastHost() const { return QSettings().value("host", "192.168.1.201").toString(); }
int Robot::lastPort() const { return QSettings().value("port", 9090).toInt(); }

void Robot::connectTo(const QString &host, int port)
{
    QSettings s;
    s.setValue("host", host);
    s.setValue("port", port);
    if (host != m_bridge.host() || port != m_bridge.port())
        resetSession();
    m_bridge.open(host, port);
}

void Robot::resetSession()
{
    m_trail.clear();
    m_distance = 0;
    m_haveOdom = false;
    m_plan.clear();
    m_mapPlan = {};
    m_frames.clear();
    m_frameCount = 0;
    m_mounts.clear();
    m_haveMapOdom = false;
    m_mo = {};
    m_moClock.invalidate();
    m_hasMap = false;
    m_mapImage = {};
    m_lastMap = {};
    m_mapApplied = m_mapJob;   // 그리는 중인 지도는 버린다
    m_straightRunning = false;
    m_straightResult.clear();
    m_straightTrack.clear();
    m_targetLine.clear();
    m_dirty = true;
    emit trailChanged();
    emit planChanged();
    emit mapChanged();
    emit mapImageChanged();
    emit straightChanged();
    emit statusChanged();
}

void Robot::setSensor(const QString &key, bool on)
{
    for (const SensorDef &d : kSensors) {
        if (key != d.key)
            continue;
        m_sensorOn[key] = on;
        if (key != "scan")
            m_bridge.want(d.topic, d.type, "sensor", on, d.throttle);
        else if (!m_scanTopic.isEmpty())   // 정하기 전이면 pickScanTopic 이 구독한다
            m_bridge.want(m_scanTopic, m_scanTopic == kScanCompact ? kScanCompactType : d.type, "sensor", on, d.throttle);
        for (const auto &e : kExtra.value(key))
            m_bridge.want(e.first, e.second, "sensor", on);
        if (!on && key == "scan")
            m_scan.clear(), m_scanCount = 0, m_scanDirty = true;
    }
    m_probed = false;
    updateStatus();
}

void Robot::probe()
{
    m_bridge.resubscribe();
    m_probed = true;
    updateStatus();
}

QString Robot::dataDir()
{
    const QString env = qEnvironmentVariable("ROBOT_DASHBOARD_DIR");
    if (!env.isEmpty())
        return QDir(env).absolutePath();
    const QDir up(QCoreApplication::applicationDirPath() + "/..");
    if (up.exists("model"))
        return up.absolutePath();
    return QDir::current().absolutePath();
}

QString Robot::recordsDir() const
{
    const QString d = QDir(dataDir()).absoluteFilePath("records");
    QDir().mkpath(d);
    return d;
}

QUrl Robot::modelFile(const QString &name) const
{
    if (qEnvironmentVariableIntValue("ROBOT_DASHBOARD_NO_MODEL") == 1)
        return {};
    for (const char *ext : {"glb", "gltf", "fbx", "obj"}) {
        const QString p = QDir(dataDir()).absoluteFilePath(QString("model/%1.%2").arg(name, ext));
        if (QFile::exists(p))
            return QUrl::fromLocalFile(p);
    }
    return {};
}

QVariantMap Robot::modelParts() const
{
    QFile f(QDir(dataDir()).absoluteFilePath("model/parts.json"));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().toVariantMap() : QVariantMap{};
}

void Robot::setMapWanted(bool on)
{
    m_mapWanted = on;
    m_bridge.want("/map", "nav_msgs/OccupancyGrid", "map", on, 2000);
    emit mapChanged();
}

void Robot::setMapScheme(const QString &s)
{
    if (s == m_mapScheme)
        return;
    m_mapScheme = s;
    emit mapStyleChanged();
    renderMap(0);
}

void Robot::setMapBinary(bool on)
{
    if (on == m_mapBinary)
        return;
    m_mapBinary = on;
    emit mapStyleChanged();
    renderMap(0);
}

bool Robot::mapOdomFresh() const
{
    return m_haveMapOdom && m_moClock.isValid() && m_moClock.elapsed() < kMapOdomFreshMs;
}

// ---- 메시지 해석 -----------------------------------------------------------------------

void Robot::onMessage(const QString &topic, const QJsonObject &msg)
{
    if (topic == "/odom")
        onOdom(msg);
    else if (topic == "/wheel_states")
        onWheels(msg, false);
    else if (topic == "/wheel_cmd")
        onWheels(msg, true);
    else if (topic == "/imu/data_raw")
        onImu(msg);
    else if (topic == "/scan")
        onScan(msg);
    else if (topic == kScanCompact)
        onScan(scanFromCompact(msg));
    else if (topic == "/tf" || topic == "/tf_static" || topic == "/map_odom")
        onTf(msg, topic == "/tf_static");
    else if (topic == "/map")
        onMap(msg);
    else if (topic == "/plan")
        onPlan(msg);
    else if (topic == "/straight_drive/status")
        onStraight(msg);
    if (m_recording && m_recordTopics.contains(topic))
        record(topic, msg);
}

void Robot::onOdom(const QJsonObject &m)
{
    const QJsonObject pose = m["pose"].toObject()["pose"].toObject();
    const QJsonObject p = pose["position"].toObject();
    const QJsonObject tw = m["twist"].toObject()["twist"].toObject();
    const double x = p["x"].toDouble(), y = p["y"].toDouble();
    const double step = std::hypot(x - m_x, y - m_y);
    if (m_haveOdom && step < 0.5)   // 오도메트리 리셋 같은 큰 점프는 거리에 넣지 않는다
        m_distance += step;
    m_x = x, m_y = y;
    m_theta = yawOf(pose["orientation"].toObject());
    m_v = tw["linear"].toObject()["x"].toDouble();
    m_w = tw["angular"].toObject()["z"].toDouble();
    m_haveOdom = true;
    const QVector3D here(float(x), float(y), 0.01f);
    if (m_trail.isEmpty() || (m_trail.last() - here).length() >= 0.02f) {
        if (m_trail.size() > 20000)
            m_trail.remove(0, 1000);
        m_trail.append(here);
        emit trailChanged();
    }
    m_dirty = true;
}

void Robot::onWheels(const QJsonObject &m, bool cmd)
{
    const QJsonArray names = m["name"].toArray(), vel = m["velocity"].toArray();
    double l = 0, r = 0;
    for (int i = 0; i < vel.size(); ++i) {
        const QString n = i < names.size() ? names[i].toString() : QString();
        if (n.contains("right") || (n.isEmpty() && i == 1))
            r = vel[i].toDouble();
        else
            l = vel[i].toDouble();
    }
    if (cmd) {
        m_cmdL = l, m_cmdR = r;
    } else {
        // position 이 비어 있어 바퀴 각도는 속도를 적분해 3D 바퀴를 돌린다.
        const qint64 now = m_clock.elapsed();
        const double dt = m_wheelT ? qMin(0.2, (now - m_wheelT) / 1000.0) : 0;
        m_wheelT = now;
        m_angleL = std::fmod(m_angleL + l * dt, 2 * M_PI);
        m_angleR = std::fmod(m_angleR + r * dt, 2 * M_PI);
        m_wheelL = l, m_wheelR = r;
    }
    m_dirty = true;
}

void Robot::onImu(const QJsonObject &m)
{
    const QJsonObject q = m["orientation"].toObject();
    const double x = q["x"].toDouble(), y = q["y"].toDouble(), z = q["z"].toDouble(), w = q["w"].toDouble(1);
    m_roll = qRadiansToDegrees(std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)));
    m_pitch = qRadiansToDegrees(std::asin(qBound(-1.0, 2 * (w * y - z * x), 1.0)));
    m_yaw = qRadiansToDegrees(yawOf(q));
    m_gyroZ = m["angular_velocity"].toObject()["z"].toDouble();
    m_accelX = m["linear_acceleration"].toObject()["x"].toDouble();
    m_dirty = true;
}

static QString stripSlash(const QString &frame) { return frame.startsWith('/') ? frame.mid(1) : frame; }

void Robot::onScan(const QJsonObject &m)
{
    // 레이저 프레임의 점을 /tf_static 의 base_link → 레이저 장착 변환으로 로봇 기준으로 옮긴다.
    // frame_id 가 base_link(실기 /scan)이거나 변환이 없으면 그대로 쓴다.
    const QString frame = stripSlash(m["header"].toObject()["frame_id"].toString());
    const Scan s = parseScan(m, frame == "base_link" ? Pose2D{} : m_mounts.value(frame), kSelfHide);
    m_scan = s.points;
    m_scanCount = int(m_scan.size());
    m_nearR = s.nearestRange;
    m_nearA = s.nearestAngle;
    m_scanTime = s.scanTime;   // 한 바퀴 시간(s)
    m_scanDirty = true;
}

// 토픽 목록을 받기 전에는 LiDAR 를 구독하지 않는다 — /scan 을 잠깐이라도 구독하면 파이 rosbridge 가 큰 /scan 을
// 보내기 시작한다. 목록이 연결 뒤 6 s(rosapi 응답 시한 5 s) 안에 오지 않으면 /scan 을 쓴다(updateStatus 가 부른다).
void Robot::pickScanTopic()
{
    QString t;
    if (m_bridge.topicListReceived())
        t = m_bridge.topicTypes().contains(kScanCompact) ? kScanCompact : "/scan";
    else if (m_scanTopic.isEmpty() && m_bridge.connected() && m_connClock.isValid() && m_connClock.elapsed() > 6000)
        t = "/scan";
    if (t.isEmpty() || t == m_scanTopic)
        return;
    const QString old = m_scanTopic;
    m_scanTopic = t;
    const bool on = m_sensorOn.value("scan").toBool();
    if (!old.isEmpty())
        m_bridge.want(old, {}, "sensor", false);
    setSensor("scan", on);
    // 기록은 받은 토픽 그대로 남긴다: 기록 목록의 LiDAR 토픽도 바꾼다.
    const QString recorded = old.isEmpty() ? QStringLiteral("/scan") : old;
    if (m_recordTopics.contains(recorded) && recorded != t) {
        setRecordTopic(recorded, false);
        setRecordTopic(t, true);
    }
    addEvent("LiDAR 토픽 " + t);
}

void Robot::onTf(const QJsonObject &m, bool isStatic)
{
    for (const QJsonValue &v : m["transforms"].toArray()) {
        const QJsonObject t = v.toObject();
        const QString parent = t["header"].toObject()["frame_id"].toString(), child = t["child_frame_id"].toString();
        m_frames.insert(parent);
        m_frames.insert(child);
        const QJsonObject tr = t["transform"].toObject();
        const Pose2D p{tr["translation"].toObject()["x"].toDouble(), tr["translation"].toObject()["y"].toDouble(),
                       yawOf(tr["rotation"].toObject())};
        if (parent == "map" && child == "odom") {
            const bool moved = !m_haveMapOdom || p.x != m_mo.x || p.y != m_mo.y || p.yaw != m_mo.yaw;
            m_mo = p;
            m_haveMapOdom = true;
            m_moClock.start();
            placeMap();
            if (moved && !m_mapPlan.isEmpty())   // map 프레임 경로를 새 map→odom 으로 다시 투영한다
                onPlan(m_mapPlan);
        }
        if (isStatic && stripSlash(parent) == "base_link")
            m_mounts[stripSlash(child)] = p;
    }
    m_frameCount = int(m_frames.size());
}

void Robot::placeMap()
{
    if (!m_hasMap)
        return;
    const Pose2D c = m_haveMapOdom ? mapToOdom(m_mo, m_mapO) : m_mapO;
    m_mapCenter = QVector3D(float(c.x), float(c.y), 0.003f);
    m_mapYaw = qRadiansToDegrees(c.yaw);
    emit mapPoseChanged();
}

void Robot::onMap(const QJsonObject &m)
{
    m_lastMap = m;
    renderMap(m_bridge.lastWorkerMs());
}

// OccupancyGrid → 이미지는 다른 스레드에서 만들고, 결과는 GUI 스레드로 넘겨 받는다(2000×2000 이면 수백 ms).
void Robot::renderMap(double bridgeWorkerMs)
{
    if (m_lastMap.isEmpty())
        return;
    QElapsedTimer gui;
    gui.start();
    struct Result { QImage img; Pose2D center; QSizeF size; double ms = 0; };
    const int job = ++m_mapJob;
    const QJsonObject m = m_lastMap;
    const QString scheme = m_mapScheme;
    const int threshold = m_mapBinary ? kMapBinaryThreshold : -1;
    auto future = QtConcurrent::run([m, scheme, threshold] {
        QElapsedTimer t;
        t.start();
        Result r;
        const QJsonObject info = m["info"].toObject();
        r.img = occupancyToImage(m["data"].toArray(), info["width"].toInt(), info["height"].toInt(), scheme, threshold);
        const GridPlane plane = gridPlane(info);
        r.center = plane.center;
        r.size = QSizeF(plane.width, plane.height);
        r.ms = t.nsecsElapsed() / 1e6;
        return r;
    });
    const double guiStart = gui.nsecsElapsed() / 1e6;
    future.then(this, [this, job, guiStart, bridgeWorkerMs](const Result &r) {   // GUI 스레드로 queued
        if (job <= m_mapApplied || r.img.isNull())   // 더 새 지도가 이미 들어갔거나 data 가 짧다
            return;
        QElapsedTimer gui;
        gui.start();
        m_mapApplied = job;
        m_mapO = r.center;
        m_mapSize = r.size;
        m_mapImage = r.img;
        ++m_mapImageSerial;
        const bool had = m_hasMap;
        m_hasMap = true;
        emit mapImageChanged();
        if (!had)
            emit mapChanged();
        placeMap();
        qInfo().noquote() << QString("[perf] map w=%1 h=%2 gui_ms=%3 worker_ms=%4")
                                 .arg(r.img.width()).arg(r.img.height())
                                 .arg(guiStart + gui.nsecsElapsed() / 1e6, 0, 'f', 1)
                                 .arg(bridgeWorkerMs + r.ms, 0, 'f', 1);
    });
}

void Robot::onPlan(const QJsonObject &m)
{
    const bool inMap = m["header"].toObject()["frame_id"].toString() == "map";
    m_mapPlan = inMap ? m : QJsonObject();
    QList<QVector3D> out;
    QVector3D prev;
    for (const QJsonValue &v : m["poses"].toArray()) {
        const QJsonObject p = v.toObject()["pose"].toObject()["position"].toObject();
        Pose2D q{p["x"].toDouble(), p["y"].toDouble(), 0};
        if (inMap && m_haveMapOdom)
            q = mapToOdom(m_mo, q);
        const QVector3D cur(float(q.x), float(q.y), 0.012f);
        if (out.isEmpty())
            out.append(cur);
        else
            appendDense(out, prev, cur);
        prev = cur;
    }
    m_plan = out;
    emit planChanged();
}

void Robot::onStraight(const QJsonObject &m)
{
    const QJsonObject st = QJsonDocument::fromJson(m["data"].toString().toUtf8()).object();
    const QString state = st["state"].toString();
    if (state == "started") {
        const double d = st["distance"].toDouble();
        m_straightRunning = true;
        m_straightTrack = {QPointF(0, 0)};
        m_straightResult.clear();
        m_targetLine.clear();
        const QVector3D a(float(m_x), float(m_y), 0.012f);
        appendDense(m_targetLine, a, a + QVector3D(float(d * std::cos(m_theta)), float(d * std::sin(m_theta)), 0));
        addEvent(QString("직진 시작 %1 m @ %2").arg(d, 0, 'f', 1).arg(st["speed"].toDouble(), 0, 'f', 2));
    } else if (state == "running") {
        m_straightTrack.append(QPointF(st["along"].toDouble(), st["cross"].toDouble() * 100));
    } else if (state == "finished") {
        m_straightRunning = false;
        m_straightTrack.append(QPointF(st["along"].toDouble(), st["cross"].toDouble() * 100));
        // 값이 null(측정 못 함)이면 키를 넣지 않는다 — 화면은 '—' 로 보인다.
        m_straightResult = {{"reason", st["reason"].toString()}};
        const struct { const char *key, *src; double scale; } fields[] = {
            {"cross", "cross", 100.0}, {"heading", "heading_err_deg", 1.0}, {"duration", "duration", 1.0}, {"along", "along", 1.0}};
        for (const auto &f : fields)
            if (st[f.src].isDouble())
                m_straightResult[f.key] = st[f.src].toDouble() * f.scale;
        addEvent("직진 완료 " + st["reason"].toString());
    } else if (state == "rejected") {
        m_straightRunning = false;
        addEvent("직진 거부 " + st["reason"].toString());
    }
    emit straightChanged();
}

// ---- 상태 표 (2 Hz) ---------------------------------------------------------------------

void Robot::updateStatus()
{
    const auto types = m_bridge.topicTypes();
    const bool conn = m_bridge.connected();

    if (m_scanTopic.isEmpty())
        pickScanTopic();
    const QString scanTopic = m_scanTopic.isEmpty() ? QStringLiteral("/scan") : m_scanTopic;

    m_sensorRows.clear();
    QList<QVariantMap> sensorModelRows;
    for (const SensorDef &d : kSensors) {
        const bool on = m_sensorOn.value(d.key).toBool();
        const QString topic = QString(d.key) == "scan" ? scanTopic : QString(d.topic);
        const RosBridge::Stat s = m_bridge.stat(topic);
        const bool fresh = conn && s.age >= 0 && s.age < qMax(1.0, s.hz > 0 ? 3.0 / s.hz : 1.0);
        QString state, detail;
        if (!on) {
            state = "꺼짐", detail = "구독 안 함";
        } else if (fresh) {
            state = "수신 중";
            detail = QString::number(s.hz, 'f', 1) + " Hz · "
                     + (QString(d.key) == "tf" ? QString("프레임 %1개").arg(m_frameCount)
                                              : QString::number(s.age, 'f', 2) + " s 전");
        } else if (conn && s.age < 0 && s.sinceSub < 1.0) {
            state = "확인 중", detail = "구독함 · 기다리는 중";
        } else {
            state = "데이터 없음";
            detail = !conn ? "연결 안 됨"
                     : (m_bridge.topicListReceived() && !types.contains(topic)) ? "발행자 없음"
                     : s.age < 0 ? "1 s 동안 메시지 없음" : QString("마지막 수신 %1 s 전").arg(s.age, 0, 'f', 1);
        }
        if (QString(d.key) == "scan")   // LiDAR 헤드는 /scan 이 들어오는 동안 실제 회전수로 돈다
            m_lidarHz = on && fresh ? (m_scanTime > 0 ? 1 / m_scanTime : s.hz) : 0;
        m_sensorRows.append(QVariantMap{{"key", d.key}, {"label", d.label}, {"topic", topic}, {"on", on},
                                        {"state", state}, {"detail", detail}});
        sensorModelRows.append(QVariantMap{{"key", d.key}, {"label", d.label}, {"topic", topic}, {"on", on},
                                           {"state", state}, {"hz", std::round(s.hz * 10) / 10},
                                           {"age", std::round(s.age * 10) / 10}});
    }
    m_sensorModel.setRows(sensorModelRows);

    m_rates.clear();
    for (const auto &[topic, label] : {std::pair<QString, QString>{"/odom", "/odom"}, {scanTopic, scanTopic},
                                       {"/imu/data_raw", "/imu"}, {"/wheel_states", "/wheel_states"}}) {
        const RosBridge::Stat s = m_bridge.stat(topic);
        m_rates.append(QString("%1 %2").arg(label, s.hz > 0 && conn ? QString::number(s.hz, 'f', 0) + " Hz" : "—"));
    }

    QStringList names = types.keys();
    for (const QString &t : m_bridge.wantedTopics())
        if (!names.contains(t))
            names.append(t);
    names.sort();
    m_topicRows.clear();
    QList<QVariantMap> topicModelRows;
    for (const QString &t : names) {
        const RosBridge::Stat s = m_bridge.stat(t);
        const bool sub = m_bridge.subscribed(t);
        QString state = !sub ? "구독 안 함" : s.age < 0 ? "대기" : "정상";
        // 꾸준히 오던 토픽(2 Hz 이상, 최근 2 s 에 4 개 이상)이 0.5 s 넘게 끊기면 지연으로 보고 이벤트를 한 번 남긴다.
        // /rosout · /parameter_events 는 일이 있을 때만 몰려 오는 토픽이라 멈춰도 지연이 아니다.
        const bool periodic = t != "/rosout" && t != "/parameter_events";
        const bool late = periodic && sub && conn && s.age > 0.5 && ((s.hz >= 2 && s.recent >= 4) || m_late.contains(t));
        if (late) {
            state = "지연";
            if (!m_late.contains(t) && s.age < 5)
                m_late.insert(t), addEvent(QString("%1 %2 s 지연").arg(t).arg(s.age, 0, 'f', 1));
        } else if (s.age >= 0 && s.age < 0.5) {
            m_late.remove(t);
        }
        const bool haveLat = sub && !qIsNaN(s.latencyMs) && s.age >= 0;
        const double kbps = sub ? std::round(s.bytesPerSec * 8 / 100) / 10 : 0;   // kbit/s, 소수 1 자리
        const QString type = shortType(types.value(t, m_bridge.wantedType(t)));
        m_topicRows.append(QVariantMap{
            {"name", t}, {"type", type},
            {"hz", sub && s.hz > 0 ? QString::number(s.hz, 'f', 1) + " Hz" : "—"},
            {"lat", haveLat ? QString::number(qRound(s.latencyMs)) + " ms" : "—"},
            {"kbps", kbps}, {"clockSkew", haveLat && s.clockSkew},
            {"state", state}, {"rec", m_recordTopics.contains(t)}});
        topicModelRows.append(QVariantMap{
            {"name", t}, {"type", type}, {"hz", sub ? std::round(s.hz * 10) / 10 : 0.0},
            {"latencyMs", haveLat ? double(qRound(s.latencyMs)) : -1.0}, {"kbps", kbps}, {"state", state},
            {"rec", m_recordTopics.contains(t)}, {"clockSkew", haveLat && s.clockSkew}});
    }
    m_topicModel.setRows(topicModelRows);

    m_missing.clear();
    for (const char *r : kRequired) {
        const QString t = QString(r) == "/scan" ? scanTopic : QString(r);
        if (m_bridge.stat(t).age < 0)
            m_missing.append(t);
    }

    m_linkLost = linkDown();
    if (m_linkLost)
        armTeleop(false);

    if (conn && ++m_statusTick % 10 == 1)   // 5 s 마다 워치독 노드가 /teleop/cmd_vel 을 듣고 있는지 확인
        m_bridge.call("/rosapi/subscribers", {{"topic", "/teleop/cmd_vel"}}, [this](const QJsonObject &v) {
            m_watchdogOk = false;
            for (const QJsonValue &s : v["subscribers"].toArray())
                m_watchdogOk |= s.toString().contains("cmd_watchdog");
        }, [this](const QString &) { m_watchdogOk = false; });
    if (!conn)
        m_watchdogOk = false;

    if (m_recording && m_flushClock.elapsed() >= 1000) {   // 기록 파일은 1 s 마다 디스크로
        m_flushClock.restart();
        for (QFile *f : std::as_const(m_files))
            f->flush();
        if (m_raw)
            m_raw->flush();
    }
    emit statusChanged();
}

// 하트비트 센서를 꺼도 rosbridge 에서 3 s 넘게 아무것도 오지 않으면 링크가 끊긴 것으로 본다. 하트비트 공백은 다른 수신도
// 1 s 넘게 없을 때만 친다(하트비트 노드만 멈춘 것은 링크 끊김이 아니다). 재연결 뒤 3 s 는 이전 세션의 하트비트 나이를 보지 않는다.
// 상태 표(2 Hz)를 기다리지 않고 잠금 해제 · 움직임 명령 때 그 자리에서 다시 계산한다.
bool Robot::linkDown()
{
    if (!m_everConnected)
        return false;
    if (!m_bridge.connected())
        return true;
    const double rx = m_bridge.rxAge();
    const bool beatGap = m_sensorOn.value("beat").toBool() && m_bridge.stat("/link/heartbeat").age > 3.0
                         && m_connClock.elapsed() > 3000 && rx > 1.0;
    return beatGap || rx > 3.0;
}

void Robot::addEvent(const QString &text)
{
    m_events.prepend(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text);
    if (m_events.size() > 100)
        m_events.removeLast();
    emit eventsChanged();
}

// ---- 조종 ----------------------------------------------------------------------------

void Robot::sendTwist(double v, double w)
{
    m_bridge.publish("/teleop/cmd_vel", {{"linear", QJsonObject{{"x", v}, {"y", 0}, {"z", 0}}},
                                         {"angular", QJsonObject{{"x", 0}, {"y", 0}, {"z", w}}}});
}

// 정지: 0 Twist ×3 · 직진 취소 · Nav2 목표 취소
void Robot::sendStop()
{
    for (int i = 0; i < 3; ++i)
        sendTwist(0, 0);
    m_bridge.publish("/straight_drive/cancel", {});
    cancelGoal();
}

// 끊긴 채로 조종 · 정지를 누르면 조용히 버리지 않고 알린다. 연결돼 있어도 1 s 넘게 아무것도 받지 못했으면
// 반쯤 열린 연결일 수 있어(send 는 성공으로 끝난다) 닿았는지 알 수 없다고 알린다.
void Robot::noteDropped()
{
    const double rx = m_bridge.rxAge();
    if (m_bridge.noPublish() || (m_bridge.connected() && rx >= 0 && rx <= 1.0))
        return;
    if (!m_lastSendDropped) {
        m_lastSendDropped = true;
        emit statusChanged();
    }
    addEvent(m_bridge.connected()
                 ? QString("%1 s 동안 수신 없음 — 정지 명령이 닿았는지 알 수 없음(재연결 때 다시 보냄)").arg(rx, 0, 'f', 1)
                 : QString("연결 끊김 — 정지 명령을 보내지 못함(파이 워치독 0.5 s 에 의존, 재연결 때 다시 보냄)"));
}

void Robot::setMode(const QString &m)
{
    if (m == m_mode)
        return;
    const bool wasAuto = m_mode == "자율주행";
    m_mode = m;
    m_lin = m_ang = 0;
    for (int i = 0; i < 3; ++i)
        sendTwist(0, 0);
    if (m == "정지")
        m_bridge.publish("/straight_drive/cancel", {}), cancelGoal();
    else if (wasAuto)   // 자율주행에서 벗어나면 진행 중인 Nav2 목표를 취소한다(키보드 조종의 0 속도와 겹치지 않게)
        cancelGoal();
    addEvent("모드 " + m);
    emit modeChanged();
}

void Robot::setMaxSpeed(double v)
{
    v = qBound(0.02, v, kMaxLinear);
    if (v == m_maxSpeed)
        return;
    m_maxSpeed = v;
    emit modeChanged();
}

void Robot::setArmIdleMs(int ms)
{
    if (ms != m_armIdleMs) { m_armIdleMs = qMax(100, ms); emit teleopArmedChanged(); }
}

void Robot::armTeleop(bool on)
{
    if (on && !m_armed && linkDown()) {   // 링크가 끊긴 동안에는 풀지 않는다 — 낡은 화면을 보며 움직이게 된다
        addEvent("링크 끊김 — 조종 잠금 해제 거부");
        return;
    }
    m_armInput.restart();
    if (on == m_armed)
        return;
    m_armed = on;
    m_lin = m_ang = 0;
    addEvent(on ? "조종 잠금 해제" : "조종 잠금");
    emit teleopArmedChanged();
}

void Robot::drive(double linear, double angular)
{
    if (m_armed && linkDown())   // 상태 표(500 ms)를 기다리지 않고 잠근다
        armTeleop(false);
    if (!m_armed) {   // 잠김: 입력을 버린다(0 은 m_teleop 이 계속 보낸다)
        m_lin = m_ang = 0;
        return;
    }
    m_armInput.restart();
    // qBound 는 NaN 을 -1(전속 후진)로 만든다 — 숫자가 아니거나 무한대면 0
    m_lin = std::isfinite(linear) ? qBound(-1.0, linear, 1.0) : 0;
    m_ang = std::isfinite(angular) ? qBound(-1.0, angular, 1.0) : 0;
    if (!m_lastSendDropped)   // 누를 때마다가 아니라 끊긴 동안 한 번만 알린다
        noteDropped();
    if (m_mode == "키보드 조종")
        sendTwist(m_lin * m_maxSpeed, m_ang * kMaxAngular);   // 누르는 즉시 반응
}

// 로봇을 움직이는 명령(직진 · 목표)을 지금 보내도 되는지. 안 되면 이유(이벤트에 남긴다).
QString Robot::moveBlocked()
{
    if (!m_armed)
        return "조종이 잠겨 있음";
    if (!m_bridge.connected() || linkDown())
        return "연결 끊김";
    if (m_straightRunning)
        return "직진 진행 중";
    return {};
}

void Robot::straight(double distance)
{
    // 1 m 시험용: 키보드 조종 모드에서만(자율주행 중이면 Nav2 와 함께 제어하게 된다), |거리| ≤ 2 m
    QString why = !std::isfinite(distance) || std::abs(distance) > kMaxStraight
                      ? QString("거리 %1 m 가 범위(±%2 m) 밖").arg(distance).arg(kMaxStraight)
                  : m_mode != "키보드 조종" ? "모드가 " + m_mode : moveBlocked();
    if (!why.isEmpty()) {
        addEvent("직진 거부 — " + why);
        return;
    }
    m_bridge.publish("/straight_drive/goal", {{"x", distance}, {"y", qMin(m_maxSpeed, kMaxLinear)}, {"z", 0}});
}

void Robot::sendGoal(double x, double y, double yaw)
{
    const QString why = !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(yaw) ? QString("숫자가 아닌 목표")
                        : m_mode == "정지" ? QString("정지 모드") : moveBlocked();
    if (!why.isEmpty()) {
        addEvent("이동 목표 거부 — " + why);
        return;
    }
    // Nav2 bt_navigator 가 듣는 /goal_pose 로 보낸다. map→odom 이 신선하면 map 으로 바꿔 보낸다: p_map = (map->odom) * p_odom
    QString frame = "odom";
    Pose2D p{x, y, yaw};
    if (mapOdomFresh()) {
        p = odomToMap(m_mo, p);
        frame = "map";
    } else if (m_haveMapOdom) {
        addEvent("map→odom 이 10 s 넘게 오지 않아 목표를 odom 프레임으로 보냄");
    }
    // Nav2 속도도 /teleop/cmd_vel 로 나오므로 키보드 조종의 10 Hz 0 속도와 겹치지 않게 자율주행으로 바꾼다.
    setMode("자율주행");
    m_bridge.publish("/goal_pose", {
        {"header", QJsonObject{{"frame_id", frame}, {"stamp", QJsonObject{{"sec", 0}, {"nanosec", 0}}}}},
        {"pose", QJsonObject{{"position", QJsonObject{{"x", p.x}, {"y", p.y}, {"z", 0}}},
                             {"orientation", quatFromYaw(p.yaw)}}}});
    addEvent(QString("이동 목표 %1 (%2, %3) %4°").arg(frame).arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2).arg(qRadiansToDegrees(p.yaw), 0, 'f', 0));
}

void Robot::setInitialPose(double x, double y, double yaw)
{
    // /dashboard/initialpose → (PC split_relay) → AMCL 의 /initialpose. map→odom 이 신선하면 map 프레임으로 바꿔 보낸다.
    QString frame = "odom";
    Pose2D p{x, y, yaw};
    if (mapOdomFresh()) {
        p = odomToMap(m_mo, p);
        frame = "map";
    } else {
        addEvent(QString("경고: map→odom 이 없거나 낡아 초기 위치(%1)를 odom 프레임으로 보냄").arg(kInitialPoseTopic));
    }
    m_bridge.publish(kInitialPoseTopic, {
        {"header", QJsonObject{{"frame_id", frame}, {"stamp", QJsonObject{{"sec", 0}, {"nanosec", 0}}}}},
        {"pose", QJsonObject{{"pose", QJsonObject{{"position", QJsonObject{{"x", p.x}, {"y", p.y}, {"z", 0}}},
                                                  {"orientation", quatFromYaw(p.yaw)}}},
                             {"covariance", initialPoseCovariance()}}}});
    addEvent(QString("초기 위치 %1 %2 (%3, %4) %5°").arg(kInitialPoseTopic, frame).arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2).arg(qRadiansToDegrees(p.yaw), 0, 'f', 0));
}

// 0 uuid · 0 stamp = 모든 Nav2 목표 취소 (rosbridge 가 액션을 몰라 액션의 숨은 서비스를 직접 부른다)
void Robot::cancelGoal()
{
    if (m_bridge.noPublish())   // 읽기 전용: 쓰는 서비스도 부르지 않는다
        return;
    QJsonArray uuid;
    for (int i = 0; i < 16; ++i)
        uuid.append(0);
    m_bridge.call("/navigate_to_pose/_action/cancel_goal",
                  {{"goal_info", QJsonObject{{"goal_id", QJsonObject{{"uuid", uuid}}},
                                             {"stamp", QJsonObject{{"sec", 0}, {"nanosec", 0}}}}}},
                  [](const QJsonObject &) {});
}

void Robot::estop()
{
    noteDropped();
    m_lin = m_ang = 0;
    sendStop();
    setMode("정지");
    armTeleop(false);
    addEvent("비상 정지");
}

void Robot::clearTrail()
{
    m_trail.clear();
    emit trailChanged();
}

// ---- 기록 (토픽마다 CSV 하나 + 원문 raw.jsonl) ------------------------------------------

void Robot::setRecordTopic(const QString &topic, bool on)
{
    if (on && !m_recordTopics.contains(topic))
        m_recordTopics.append(topic);
    if (!on)
        m_recordTopics.removeAll(topic);
    if (m_recording)
        m_bridge.want(topic, m_bridge.topicTypes().value(topic), "record", on);
    emit recordChanged();
    updateStatus();
}

void Robot::toggleRecording()
{
    if (m_recording) {
        stopRecording();
        addEvent("기록 중지 " + m_recordPath);
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    m_recordPath = QDir(dataDir()).absoluteFilePath("records/" + now.toString("yyyyMMdd_HHmmss"));
    QDir().mkpath(m_recordPath);
    // 원문 기록: 첫 줄 meta, 이후 한 줄에 메시지 하나 (P1 --replay 입력과 같은 형식)
    m_raw = new QFile(m_recordPath + "/raw.jsonl");
    if (m_raw->open(QIODevice::WriteOnly)) {
        const QJsonObject meta{{"meta", QJsonObject{{"host", m_bridge.host()},
                                                    {"started", now.toMSecsSinceEpoch() / 1000.0},
                                                    {"topics", QJsonArray::fromStringList(m_recordTopics)}}}};
        m_raw->write(QJsonDocument(meta).toJson(QJsonDocument::Compact) + "\n");
    } else {
        delete m_raw;
        m_raw = nullptr;
    }
    for (const QString &t : std::as_const(m_recordTopics))
        m_bridge.want(t, m_bridge.topicTypes().value(t), "record", true);
    m_recording = true;
    m_recordClock.start();
    m_flushClock.start();
    addEvent("기록 시작 " + m_recordPath);
    emit recordChanged();
}

void Robot::stopRecording()
{
    if (!m_recording)
        return;
    for (const QString &t : std::as_const(m_recordTopics))
        m_bridge.want(t, {}, "record", false);
    qDeleteAll(m_files);
    m_files.clear();
    delete m_raw;
    m_raw = nullptr;
    m_headers.clear();
    m_extraKeyWarned.clear();
    m_lastBig.clear();
    m_recording = false;
    emit recordChanged();
}

QString Robot::recordElapsed() const
{
    if (!m_recording)
        return "00:00";
    const qint64 s = m_recordClock.elapsed() / 1000;
    return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

void Robot::openRecords()
{
    const QString dir = m_recordPath.isEmpty() ? QDir(dataDir()).absoluteFilePath("records") : m_recordPath;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void Robot::record(const QString &topic, const QJsonObject &msg)
{
    const QString type = m_bridge.wantedType(topic).isEmpty() ? m_bridge.topicTypes().value(topic) : m_bridge.wantedType(topic);
    if (type.contains("OccupancyGrid")) {   // 큰 메시지는 내용(header 제외)이 바뀌었을 때만
        QJsonObject body = msg;
        body.remove("header");
        if (m_lastBig.value(topic) == body)
            return;
        m_lastBig[topic] = body;
    }
    if (m_raw) {
        const QJsonObject line{{"t", m_recordClock.nsecsElapsed() / 1e9},
                               {"wall", QDateTime::currentMSecsSinceEpoch() / 1000.0},
                               {"topic", topic}, {"type", type}, {"msg", msg}};
        m_raw->write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
    }

    QHash<QString, QString> row;
    flatten(msg, {}, row);
    QFile *f = m_files.value(topic);
    if (!f) {
        QString name = topic.mid(1);
        f = new QFile(m_recordPath + "/" + name.replace('/', '_') + ".csv");
        if (!f->open(QIODevice::WriteOnly | QIODevice::Text)) {
            delete f;
            return;
        }
        m_files[topic] = f;
        QStringList keys = row.keys();
        keys.sort();
        m_headers[topic] = keys;
        f->write(("pc_time," + keys.join(',') + "\n").toUtf8());
    }
    const QStringList &header = m_headers[topic];
    if (!m_extraKeyWarned.contains(topic)) {
        for (auto it = row.cbegin(); it != row.cend(); ++it) {
            if (!header.contains(it.key())) {   // 배열 길이가 바뀌는 메시지 등 — 한 번만 알린다
                m_extraKeyWarned.insert(topic);
                addEvent(QString("경고: %1 에 CSV 헤더에 없는 열(%2)이 나옴 — CSV 에서는 빠지고 raw.jsonl 에만 남음").arg(topic, it.key()));
                break;
            }
        }
    }
    QStringList cells{QString::number(QDateTime::currentMSecsSinceEpoch() / 1000.0, 'f', 3)};
    for (const QString &k : header)
        cells << csvCell(row.value(k));
    f->write((cells.join(',') + "\n").toUtf8());
}

