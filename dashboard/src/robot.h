#pragma once

#include "rosbridge.h"

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPointF>
#include <QUrl>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

// 로봇 토픽을 해석해 화면이 쓰는 값으로 내놓는다. 값은 30 Hz 로 묶어서 알린다(telemetryChanged).
// 좌표는 모두 ROS 기준(odom 프레임, x 앞 · y 왼쪽 · z 위, m · rad).
class Robot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge CONSTANT)
    // 오도메트리 · 바퀴 · IMU · LiDAR
    Q_PROPERTY(double x MEMBER m_x NOTIFY telemetryChanged)
    Q_PROPERTY(double y MEMBER m_y NOTIFY telemetryChanged)
    Q_PROPERTY(double theta MEMBER m_theta NOTIFY telemetryChanged)
    Q_PROPERTY(double v MEMBER m_v NOTIFY telemetryChanged)
    Q_PROPERTY(double w MEMBER m_w NOTIFY telemetryChanged)
    Q_PROPERTY(double distance MEMBER m_distance NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelCmdL MEMBER m_cmdL NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelCmdR MEMBER m_cmdR NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelL MEMBER m_wheelL NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelR MEMBER m_wheelR NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelAngleL MEMBER m_angleL NOTIFY telemetryChanged)
    Q_PROPERTY(double wheelAngleR MEMBER m_angleR NOTIFY telemetryChanged)
    Q_PROPERTY(double roll MEMBER m_roll NOTIFY telemetryChanged)
    Q_PROPERTY(double pitch MEMBER m_pitch NOTIFY telemetryChanged)
    Q_PROPERTY(double yaw MEMBER m_yaw NOTIFY telemetryChanged)
    Q_PROPERTY(double gyroZ MEMBER m_gyroZ NOTIFY telemetryChanged)
    Q_PROPERTY(double accelX MEMBER m_accelX NOTIFY telemetryChanged)
    Q_PROPERTY(QList<QVector3D> scanPoints MEMBER m_scan NOTIFY telemetryChanged)
    Q_PROPERTY(int scanCount MEMBER m_scanCount NOTIFY telemetryChanged)
    Q_PROPERTY(double nearestRange MEMBER m_nearR NOTIFY telemetryChanged)
    Q_PROPERTY(double nearestAngle MEMBER m_nearA NOTIFY telemetryChanged)
    Q_PROPERTY(double lidarHz MEMBER m_lidarHz NOTIFY statusChanged)   // 헤드 회전수, /scan 이 끊기면 0
    Q_PROPERTY(QList<QVector3D> trail MEMBER m_trail NOTIFY trailChanged)
    Q_PROPERTY(QList<QVector3D> plan MEMBER m_plan NOTIFY planChanged)
    Q_PROPERTY(int frameCount MEMBER m_frameCount NOTIFY statusChanged)
    // 지도 (odom 프레임에 놓은 판)
    Q_PROPERTY(bool hasMap MEMBER m_hasMap NOTIFY mapChanged)
    Q_PROPERTY(QImage mapImage MEMBER m_mapImage NOTIFY mapChanged)
    Q_PROPERTY(QVector3D mapCenter MEMBER m_mapCenter NOTIFY mapChanged)
    Q_PROPERTY(double mapYaw MEMBER m_mapYaw NOTIFY mapChanged)
    Q_PROPERTY(QSizeF mapSize MEMBER m_mapSize NOTIFY mapChanged)
    Q_PROPERTY(bool mapWanted READ mapWanted WRITE setMapWanted NOTIFY mapChanged)
    // 연결 · 센서 · 토픽 표
    Q_PROPERTY(bool linkLost MEMBER m_linkLost NOTIFY statusChanged)
    Q_PROPERTY(bool watchdogOk MEMBER m_watchdogOk NOTIFY statusChanged)
    Q_PROPERTY(QVariantList sensors MEMBER m_sensorRows NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap sensorOn MEMBER m_sensorOn NOTIFY statusChanged)
    Q_PROPERTY(QVariantList rates MEMBER m_rates NOTIFY statusChanged)
    Q_PROPERTY(QVariantList topicRows MEMBER m_topicRows NOTIFY statusChanged)
    Q_PROPERTY(QStringList missingRequired MEMBER m_missing NOTIFY statusChanged)
    Q_PROPERTY(bool probed MEMBER m_probed NOTIFY statusChanged)
    Q_PROPERTY(QStringList events MEMBER m_events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList savedRobots READ savedRobots NOTIFY savedChanged)
    Q_PROPERTY(QString lastHost READ lastHost CONSTANT)
    Q_PROPERTY(int lastPort READ lastPort CONSTANT)
    // 조종 · 직진 시험
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(double maxSpeed MEMBER m_maxSpeed NOTIFY modeChanged)
    Q_PROPERTY(bool straightRunning MEMBER m_straightRunning NOTIFY straightChanged)
    Q_PROPERTY(QVariantMap straightResult MEMBER m_straightResult NOTIFY straightChanged)
    Q_PROPERTY(QList<QPointF> straightTrack MEMBER m_straightTrack NOTIFY straightChanged)
    Q_PROPERTY(QList<QVector3D> targetLine MEMBER m_targetLine NOTIFY straightChanged)
    // 기록
    Q_PROPERTY(bool recording MEMBER m_recording NOTIFY recordChanged)
    Q_PROPERTY(QString recordPath MEMBER m_recordPath NOTIFY recordChanged)
    Q_PROPERTY(QStringList recordTopics MEMBER m_recordTopics NOTIFY recordChanged)
    Q_PROPERTY(QString recordElapsed READ recordElapsed NOTIFY statusChanged)

public:
    explicit Robot(QObject *parent = nullptr);
    ~Robot() override;

    RosBridge *bridge() { return &m_bridge; }
    QVariantList savedRobots() const;
    QString lastHost() const;
    int lastPort() const;
    QString mode() const { return m_mode; }
    void setMode(const QString &m);
    bool mapWanted() const { return m_mapWanted; }
    void setMapWanted(bool on);
    QString recordElapsed() const;

    Q_INVOKABLE void connectTo(const QString &host, int port);
    Q_INVOKABLE void setSensor(const QString &key, bool on);
    Q_INVOKABLE void probe();
    Q_INVOKABLE void drive(double linear, double angular);   // -1..1 (최대 속도 비율)
    Q_INVOKABLE void straight(double distance);
    Q_INVOKABLE void estop();
    Q_INVOKABLE void setRecordTopic(const QString &topic, bool on);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void openRecords();
    Q_INVOKABLE void clearTrail();
    Q_INVOKABLE QUrl modelFile(const QString &name) const;
    Q_INVOKABLE QVariantMap modelParts() const;   // model/parts.json (LiDAR 헤드 위치 등)

signals:
    void telemetryChanged();
    void trailChanged();
    void planChanged();
    void mapChanged();
    void statusChanged();
    void eventsChanged();
    void savedChanged();
    void modeChanged();
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
    void onTf(const QJsonObject &m);
    void onMap(const QJsonObject &m);
    void onPlan(const QJsonObject &m);
    void onStraight(const QJsonObject &m);
    void placeMap();
    void updateStatus();
    void addEvent(const QString &text);
    void sendTwist(double v, double w);
    void record(const QString &topic, const QJsonObject &msg);
    void stopRecording();

    RosBridge m_bridge;
    QTimer m_ui, m_status, m_teleop;
    QElapsedTimer m_clock;
    bool m_dirty = false;

    double m_x = 0, m_y = 0, m_theta = 0, m_v = 0, m_w = 0, m_distance = 0;
    bool m_haveOdom = false;
    double m_cmdL = 0, m_cmdR = 0, m_wheelL = 0, m_wheelR = 0, m_angleL = 0, m_angleR = 0;
    qint64 m_wheelT = 0;
    double m_roll = 0, m_pitch = 0, m_yaw = 0, m_gyroZ = 0, m_accelX = 0;
    QList<QVector3D> m_scan;
    int m_scanCount = 0;
    double m_nearR = 0, m_nearA = 0, m_scanTime = 0, m_lidarHz = 0;
    QList<QVector3D> m_trail, m_plan, m_targetLine;
    QList<QPointF> m_straightTrack;
    QSet<QString> m_frames;
    int m_frameCount = 0;
    bool m_haveMapOdom = false;
    double m_moX = 0, m_moY = 0, m_moYaw = 0;   // map -> odom
    bool m_hasMap = false, m_mapWanted = false;
    QImage m_mapImage;
    double m_mapOx = 0, m_mapOy = 0, m_mapOyaw = 0;  // 지도 판 중심(map 프레임)
    QVector3D m_mapCenter;
    double m_mapYaw = 0;
    QSizeF m_mapSize;

    bool m_everConnected = false, m_linkLost = false, m_watchdogOk = false, m_probed = false;
    QVariantList m_sensorRows, m_rates, m_topicRows;
    QVariantMap m_sensorOn;
    QStringList m_missing, m_events;
    QSet<QString> m_late;

    QString m_mode = QStringLiteral("키보드 조종");
    double m_maxSpeed = 0.08, m_lin = 0, m_ang = 0;
    bool m_straightRunning = false;
    QVariantMap m_straightResult;

    bool m_recording = false;
    QString m_recordPath;
    QStringList m_recordTopics;
    QElapsedTimer m_recordClock;
    QHash<QString, QFile *> m_files;
    QHash<QString, QStringList> m_headers;
};
