#pragma once

#include "msgparse.h"
#include "rosbridge.h"
#include "topicmodel.h"

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QUrl>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

// 로봇 토픽을 해석해 화면이 쓰는 값으로 내놓는다. 값은 30 Hz 로 묶어서 알린다(telemetryChanged · scanChanged).
// 좌표는 모두 ROS 기준(odom 프레임, x 앞 · y 왼쪽 · z 위, m · rad).
class Robot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge CONSTANT)
    // 오도메트리 · 바퀴 · IMU · LiDAR
    Q_PROPERTY(double x READ x NOTIFY telemetryChanged)
    Q_PROPERTY(double y READ y NOTIFY telemetryChanged)
    Q_PROPERTY(double theta READ theta NOTIFY telemetryChanged)
    Q_PROPERTY(double v READ v NOTIFY telemetryChanged)
    Q_PROPERTY(double w READ w NOTIFY telemetryChanged)
    Q_PROPERTY(double distance READ distance NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelCmdL READ wheelCmdL NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelCmdR READ wheelCmdR NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelL READ wheelL NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelR READ wheelR NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelAngleL READ wheelAngleL NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelAngleR READ wheelAngleR NOTIFY telemetryChanged)
    Q_PROPERTY(double roll READ roll NOTIFY telemetryChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY telemetryChanged)
    Q_PROPERTY(double yaw READ yaw NOTIFY telemetryChanged)
    Q_PROPERTY(double gyroZ READ gyroZ NOTIFY telemetryChanged)
    Q_PROPERTY(double accelX READ accelX NOTIFY telemetryChanged)
    Q_PROPERTY(QList<QVector3D> scanPoints READ scanPoints NOTIFY scanChanged)
    Q_PROPERTY(int scanCount READ scanCount NOTIFY scanChanged)
    Q_PROPERTY(double nearestRange READ nearestRange NOTIFY scanChanged)
    Q_PROPERTY(double nearestAngle READ nearestAngle NOTIFY scanChanged)
    Q_PROPERTY(double lidarHz READ lidarHz NOTIFY statusChanged)   // 헤드 회전수, /scan 이 끊기면 0
    // LiDAR 센서가 실제로 구독한 토픽: 토픽 목록에 /scan_compact 가 있으면 그것, 없으면 /scan (정하기 전에는 "")
    Q_PROPERTY(QString scanTopic READ scanTopic NOTIFY statusChanged)
    Q_PROPERTY(QList<QVector3D> trail READ trail NOTIFY trailChanged)
    Q_PROPERTY(QList<QVector3D> plan READ plan NOTIFY planChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY statusChanged)
    // 지도 (odom 프레임에 놓은 판). 이미지는 /map 이 올 때만, 위치는 map→odom 이 올 때도 바뀐다.
    Q_PROPERTY(bool hasMap READ hasMap NOTIFY mapChanged)
    Q_PROPERTY(QImage mapImage READ mapImage NOTIFY mapImageChanged)
    Q_PROPERTY(int mapImageSerial READ mapImageSerial NOTIFY mapImageChanged)
    Q_PROPERTY(QVector3D mapCenter READ mapCenter NOTIFY mapPoseChanged)
    Q_PROPERTY(double mapYaw READ mapYaw NOTIFY mapPoseChanged)
    Q_PROPERTY(QSizeF mapSize READ mapSize NOTIFY mapPoseChanged)
    Q_PROPERTY(bool mapWanted READ mapWanted WRITE setMapWanted NOTIFY mapChanged)
    Q_PROPERTY(QString mapScheme READ mapScheme WRITE setMapScheme NOTIFY mapStyleChanged)   // map · costmap · raw
    Q_PROPERTY(bool mapBinary READ mapBinary WRITE setMapBinary NOTIFY mapStyleChanged)       // 점유 65 이상 검정 · 미만 흰색
    Q_PROPERTY(bool mapOdomFresh READ mapOdomFresh NOTIFY statusChanged)   // 이번 세션에 받은 map→odom 이 10 s 이내
    // 연결 · 센서 · 토픽 표
    Q_PROPERTY(bool linkLost READ linkLost NOTIFY statusChanged)
    Q_PROPERTY(bool watchdogOk READ watchdogOk NOTIFY statusChanged)
    Q_PROPERTY(QVariantList sensors READ sensors NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap sensorOn READ sensorOn NOTIFY statusChanged)
    Q_PROPERTY(QVariantList rates READ rates NOTIFY statusChanged)
    Q_PROPERTY(QVariantList topicRows READ topicRows NOTIFY statusChanged)
    Q_PROPERTY(TopicModel *topicModel READ topicModel CONSTANT)
    Q_PROPERTY(SensorModel *sensorModel READ sensorModel CONSTANT)
    Q_PROPERTY(QStringList missingRequired READ missingRequired NOTIFY statusChanged)
    Q_PROPERTY(bool probed READ probed NOTIFY statusChanged)
    Q_PROPERTY(bool lastSendDropped READ lastSendDropped NOTIFY statusChanged)   // 끊긴 채로 조종 · 정지를 보냄
    Q_PROPERTY(QStringList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList savedRobots READ savedRobots NOTIFY savedChanged)
    Q_PROPERTY(QString lastHost READ lastHost CONSTANT)
    Q_PROPERTY(int lastPort READ lastPort CONSTANT)
    // 조종 · 직진 시험
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(double maxSpeed READ maxSpeed WRITE setMaxSpeed NOTIFY modeChanged)
    // 조종 잠금: 잠긴 동안 drive() 입력을 버린다(0 은 10 Hz 로 계속). 연결 · 재연결 · linkLost · estop · 무입력이면 잠긴다.
    Q_PROPERTY(bool teleopArmed READ teleopArmed NOTIFY teleopArmedChanged)
    Q_PROPERTY(int armIdleMs READ armIdleMs WRITE setArmIdleMs NOTIFY teleopArmedChanged)   // 무입력 자동 잠금(ms), 시험에서 줄인다
    Q_PROPERTY(bool straightRunning READ straightRunning NOTIFY straightChanged)
    Q_PROPERTY(QVariantMap straightResult READ straightResult NOTIFY straightChanged)
    Q_PROPERTY(QList<QPointF> straightTrack READ straightTrack NOTIFY straightChanged)
    Q_PROPERTY(QList<QVector3D> targetLine READ targetLine NOTIFY straightChanged)
    // 기록
    Q_PROPERTY(bool recording READ recording NOTIFY recordChanged)
    Q_PROPERTY(QString recordPath READ recordPath NOTIFY recordChanged)
    Q_PROPERTY(QStringList recordTopics READ recordTopics NOTIFY recordChanged)
    Q_PROPERTY(QString recordElapsed READ recordElapsed NOTIFY statusChanged)

public:
    explicit Robot(QObject *parent = nullptr);
    ~Robot() override;

    RosBridge *bridge() { return &m_bridge; }
    double x() const { return m_x; }
    double y() const { return m_y; }
    double theta() const { return m_theta; }
    double v() const { return m_v; }
    double w() const { return m_w; }
    double distance() const { return m_distance; }
    double wheelCmdL() const { return m_cmdL; }
    double wheelCmdR() const { return m_cmdR; }
    double wheelL() const { return m_wheelL; }
    double wheelR() const { return m_wheelR; }
    double wheelAngleL() const { return m_angleL; }
    double wheelAngleR() const { return m_angleR; }
    double roll() const { return m_roll; }
    double pitch() const { return m_pitch; }
    double yaw() const { return m_yaw; }
    double gyroZ() const { return m_gyroZ; }
    double accelX() const { return m_accelX; }
    QList<QVector3D> scanPoints() const { return m_scan; }
    int scanCount() const { return m_scanCount; }
    double nearestRange() const { return m_nearR; }
    double nearestAngle() const { return m_nearA; }
    double lidarHz() const { return m_lidarHz; }
    QString scanTopic() const { return m_scanTopic; }
    QList<QVector3D> trail() const { return m_trail; }
    QList<QVector3D> plan() const { return m_plan; }
    int frameCount() const { return m_frameCount; }
    bool hasMap() const { return m_hasMap; }
    QImage mapImage() const { return m_mapImage; }
    int mapImageSerial() const { return m_mapImageSerial; }
    QVector3D mapCenter() const { return m_mapCenter; }
    double mapYaw() const { return m_mapYaw; }
    QSizeF mapSize() const { return m_mapSize; }
    bool mapWanted() const { return m_mapWanted; }
    void setMapWanted(bool on);
    QString mapScheme() const { return m_mapScheme; }
    void setMapScheme(const QString &s);
    bool mapBinary() const { return m_mapBinary; }
    void setMapBinary(bool on);
    bool mapOdomFresh() const;
    bool linkLost() const { return m_linkLost; }
    bool watchdogOk() const { return m_watchdogOk; }
    QVariantList sensors() const { return m_sensorRows; }
    QVariantMap sensorOn() const { return m_sensorOn; }
    QVariantList rates() const { return m_rates; }
    QVariantList topicRows() const { return m_topicRows; }
    TopicModel *topicModel() { return &m_topicModel; }
    SensorModel *sensorModel() { return &m_sensorModel; }
    QStringList missingRequired() const { return m_missing; }
    bool probed() const { return m_probed; }
    bool lastSendDropped() const { return m_lastSendDropped; }
    QStringList events() const { return m_events; }
    QVariantList savedRobots() const;
    QString lastHost() const;
    int lastPort() const;
    QString mode() const { return m_mode; }
    void setMode(const QString &m);
    double maxSpeed() const { return m_maxSpeed; }
    void setMaxSpeed(double v);
    bool teleopArmed() const { return m_armed; }
    int armIdleMs() const { return m_armIdleMs; }
    void setArmIdleMs(int ms);
    bool straightRunning() const { return m_straightRunning; }
    QVariantMap straightResult() const { return m_straightResult; }
    QList<QPointF> straightTrack() const { return m_straightTrack; }
    QList<QVector3D> targetLine() const { return m_targetLine; }
    bool recording() const { return m_recording; }
    QString recordPath() const { return m_recordPath; }
    QStringList recordTopics() const { return m_recordTopics; }
    QString recordElapsed() const;

    // 데이터 폴더(model/ · records/): env ROBOT_DASHBOARD_DIR → 실행 파일 위 폴더(model/ 이 있으면) → 현재 폴더
    static QString dataDir();
    Q_INVOKABLE QString recordsDir() const;   // dataDir/records (없으면 만든다) — 그래프 CSV · PNG 저장

    Q_INVOKABLE void connectTo(const QString &host, int port);
    Q_INVOKABLE void resetSession();   // 다른 로봇에 붙을 때: 자취 · 거리 · 지도 · 경로 · 프레임 · 직진 결과 · map→odom
    Q_INVOKABLE void setSensor(const QString &key, bool on);
    Q_INVOKABLE void probe();
    Q_INVOKABLE void drive(double linear, double angular);   // -1..1 (최대 속도 비율)
    Q_INVOKABLE void armTeleop(bool on);
    Q_INVOKABLE void straight(double distance);
    Q_INVOKABLE void sendGoal(double x, double y, double yaw);   // Nav2 이동 목표 (odom 프레임 m · rad)
    Q_INVOKABLE void setInitialPose(double x, double y, double yaw);   // AMCL 초기 위치 (odom 프레임 m · rad) → /dashboard/initialpose
    Q_INVOKABLE void estop();
    Q_INVOKABLE void setRecordTopic(const QString &topic, bool on);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void openRecords();
    Q_INVOKABLE void clearTrail();
    Q_INVOKABLE QUrl modelFile(const QString &name) const;
    Q_INVOKABLE QVariantMap modelParts() const;   // model/parts.json (LiDAR 헤드 위치 등)

signals:
    void telemetryChanged();
    void scanChanged();
    void trailChanged();
    void planChanged();
    void mapChanged();
    void mapImageChanged();
    void mapPoseChanged();
    void mapStyleChanged();
    void statusChanged();
    void eventsChanged();
    void savedChanged();
    void modeChanged();
    void teleopArmedChanged();
    void straightChanged();
    void recordChanged();
    // 그래프용 표본(30 Hz): 시간 s, 바퀴 목표/실측 L·R rad/s, gyro z rad/s, accel x m/s²
    void sampled(double t, double cmdL, double cmdR, double actL, double actR, double gyroZ, double accelX);

private:
    void onMessage(const QString &topic, const QJsonObject &msg);
    void onOdom(const QJsonObject &m);
    void onWheels(const QJsonObject &m, bool cmd);
    void onImu(const QJsonObject &m);
    void onScan(const QJsonObject &m);
    void pickScanTopic();
    void onTf(const QJsonObject &m, bool isStatic);
    void onMap(const QJsonObject &m);
    void renderMap(double bridgeWorkerMs);
    void onPlan(const QJsonObject &m);
    void onStraight(const QJsonObject &m);
    void placeMap();
    void updateStatus();
    void addEvent(const QString &text);
    void sendTwist(double v, double w);
    void sendStop();
    void noteDropped();
    void cancelGoal();
    bool linkDown();
    QString moveBlocked();
    void record(const QString &topic, const QJsonObject &msg);
    void stopRecording();

    RosBridge m_bridge;
    QTimer m_ui, m_status, m_teleop;
    QElapsedTimer m_clock;
    bool m_dirty = false, m_scanDirty = false;

    double m_x = 0, m_y = 0, m_theta = 0, m_v = 0, m_w = 0, m_distance = 0;
    bool m_haveOdom = false;
    double m_cmdL = 0, m_cmdR = 0, m_wheelL = 0, m_wheelR = 0, m_angleL = 0, m_angleR = 0;
    qint64 m_wheelT = 0;
    double m_roll = 0, m_pitch = 0, m_yaw = 0, m_gyroZ = 0, m_accelX = 0;
    QList<QVector3D> m_scan;
    int m_scanCount = 0;
    double m_nearR = 0, m_nearA = 0, m_scanTime = 0, m_lidarHz = 0;
    QString m_scanTopic;          // "" = 아직 정하지 않음(토픽 목록 기다림)
    QHash<QString, msgparse::Pose2D> m_mounts;   // /tf_static base_link → 센서 프레임
    QList<QVector3D> m_trail, m_plan, m_targetLine;
    QJsonObject m_mapPlan;               // map 프레임 /plan 원문 — map→odom 이 바뀌면 다시 투영한다
    QList<QPointF> m_straightTrack;
    QSet<QString> m_frames;
    int m_frameCount = 0;
    bool m_haveMapOdom = false;
    msgparse::Pose2D m_mo;               // map -> odom
    QElapsedTimer m_moClock;             // map -> odom 받은 뒤 경과
    bool m_hasMap = false, m_mapWanted = false, m_mapBinary = false;
    QString m_mapScheme = QStringLiteral("map");
    QJsonObject m_lastMap;
    int m_mapJob = 0, m_mapApplied = 0, m_mapImageSerial = 0;
    QImage m_mapImage;
    msgparse::Pose2D m_mapO;             // 지도 판 중심(map 프레임)
    QVector3D m_mapCenter;
    double m_mapYaw = 0;
    QSizeF m_mapSize;

    bool m_everConnected = false, m_linkLost = false, m_watchdogOk = false, m_probed = false;
    bool m_lastSendDropped = false;
    QElapsedTimer m_connClock;   // 이번 연결 뒤 경과(링크 판정 · 토픽 목록이 오지 않을 때 /scan 으로 정하기)
    int m_statusTick = 0;
    QVariantList m_sensorRows, m_rates, m_topicRows;
    TopicModel m_topicModel;
    SensorModel m_sensorModel;
    QVariantMap m_sensorOn;
    QStringList m_missing, m_events;
    QSet<QString> m_late;

    QString m_mode = QStringLiteral("키보드 조종");
    double m_maxSpeed = 0.08, m_lin = 0, m_ang = 0;
    bool m_armed = false;
    int m_armIdleMs = 30000;
    QElapsedTimer m_armInput;   // 마지막 조종 입력
    bool m_straightRunning = false;
    QVariantMap m_straightResult;

    bool m_recording = false;
    QString m_recordPath;
    QStringList m_recordTopics;
    QElapsedTimer m_recordClock, m_flushClock;
    QHash<QString, QFile *> m_files;
    QFile *m_raw = nullptr;   // raw.jsonl (원문 기록, --replay 입력)
    QHash<QString, QStringList> m_headers;
    QSet<QString> m_extraKeyWarned;
    QHash<QString, QJsonObject> m_lastBig;   // 큰 메시지(OccupancyGrid)는 바뀌었을 때만 기록
};
