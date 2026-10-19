// Robot: bridge()->message 를 직접 내보내 토픽 해석을 보고, 보내는 JSON 은 모의 rosbridge 에서 캡처한다.
#include "mockserver.h"
#include "robot.h"

#include <QTemporaryDir>
#include <QtMath>
#include <QtTest>

namespace {
QJsonObject tfMsg(const QString &parent, const QString &child, double x, double y, double yaw)
{
    return {{"transforms", QJsonArray{QJsonObject{
                {"header", QJsonObject{{"frame_id", parent}}}, {"child_frame_id", child},
                {"transform", QJsonObject{{"translation", QJsonObject{{"x", x}, {"y", y}, {"z", 0}}},
                                          {"rotation", msgparse::quatFromYaw(yaw)}}}}}}};
}

QJsonObject odomMsg(double x, double y)
{
    const QJsonObject pose{{"position", QJsonObject{{"x", x}, {"y", y}}}, {"orientation", QJsonObject{{"w", 1}}}};
    return {{"pose", QJsonObject{{"pose", pose}}}};
}

QJsonObject lastPublish(const MockServer &s, const QString &topic)
{
    const auto l = s.ops("publish", topic);
    return l.isEmpty() ? QJsonObject() : l.last()["msg"].toObject();
}

bool connectRobot(Robot &r, const MockServer &s)
{
    r.connectTo("127.0.0.1", s.port());
    return QTest::qWaitFor([&] { return r.bridge()->connected() && !s.clients.isEmpty(); }, 5000);
}
} // namespace

class TstRobot : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("RobotDashboardTest");
        QCoreApplication::setApplicationName("RobotDashboardUnitTest");
        qputenv("ROBOT_DASHBOARD_DIR", m_dir.path().toUtf8());
        qputenv("ROBOT_DASHBOARD_NO_PUBLISH", "0");
    }

    // 직진 결과의 null 값은 키를 넣지 않는다.
    void straightNullOmitted()
    {
        Robot r;
        const QJsonObject st{{"state", "finished"}, {"reason", "done"}, {"cross", 0.012}, {"heading_err_deg", QJsonValue()},
                             {"duration", 12.5}, {"along", QJsonValue()}};
        emit r.bridge()->message("/straight_drive/status",
                                 {{"data", QString::fromUtf8(QJsonDocument(st).toJson(QJsonDocument::Compact))}});
        const QVariantMap res = r.straightResult();
        QVERIFY(!res.contains("heading"));
        QVERIFY(!res.contains("along"));
        QVERIFY(qAbs(res["cross"].toDouble() - 1.2) < 1e-9);
        QCOMPARE(res["duration"].toDouble(), 12.5);
        QCOMPARE(res["reason"].toString(), QString("done"));
    }

    // /tf_static 의 base_link→laser 가 yaw=π 면 레이저 앞 1 m 점은 로봇 뒤(-x)에 놓인다.
    void laserMount()
    {
        Robot r;
        emit r.bridge()->message("/tf_static", tfMsg("base_link", "laser", 0, 0, M_PI));
        QSignalSpy scan(&r, &Robot::scanChanged);
        emit r.bridge()->message("/scan", {{"header", QJsonObject{{"frame_id", "laser"}}}, {"angle_min", 0.0},
                                           {"angle_increment", 0.1}, {"range_min", 0.05}, {"range_max", 10.0},
                                           {"ranges", QJsonArray{1.0}}});
        QVERIFY(scan.wait(500));
        QCOMPARE(r.scanCount(), 1);
        const QVector3D p = r.scanPoints().first();
        QVERIFY(qAbs(p.x() + 1) < 1e-5 && qAbs(p.y()) < 1e-5);
        QCOMPARE(r.nearestRange(), 1.0);

        // 실기 /scan 은 frame_id 가 base_link — base_link→laser 변환이 있어도 그대로 쓴다.
        scan.clear();
        emit r.bridge()->message("/scan", {{"header", QJsonObject{{"frame_id", "base_link"}}}, {"angle_min", 0.0},
                                           {"angle_increment", 0.1}, {"range_min", 0.05}, {"range_max", 10.0},
                                           {"ranges", QJsonArray{1.0}}});
        QVERIFY(scan.wait(500));
        const QVector3D q = r.scanPoints().first();
        QVERIFY(qAbs(q.x() - 1) < 1e-5 && qAbs(q.y()) < 1e-5);
    }

    // scanChanged 는 /scan 이 바뀔 때만, /odom 에는 나지 않는다.
    void scanSignalOnlyForScan()
    {
        Robot r;
        QTest::qWait(100);
        QSignalSpy scan(&r, &Robot::scanChanged), tele(&r, &Robot::telemetryChanged);
        emit r.bridge()->message("/odom", odomMsg(0.1, 0));
        QVERIFY(tele.wait(500));
        QTest::qWait(100);
        QCOMPARE(scan.size(), 0);
    }

    // map→odom 이 신선하면 목표 · 초기 위치를 map 프레임으로 바꿔 보낸다.
    void goalAndInitialPoseFrames()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return s.ops("advertise", "/dashboard/initialpose").size() == 1; }, 2000));
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 1, 2, M_PI / 2));
        QVERIFY(r.mapOdomFresh());
        r.sendGoal(1, 0, 0);
        r.setInitialPose(1, 0, 0);
        QVERIFY(QTest::qWaitFor([&] { return !lastPublish(s, "/goal_pose").isEmpty() && !lastPublish(s, "/dashboard/initialpose").isEmpty(); }, 2000));

        const QJsonObject goal = lastPublish(s, "/goal_pose");
        QCOMPARE(goal["header"].toObject()["frame_id"].toString(), QString("map"));
        const QJsonObject gp = goal["pose"].toObject()["position"].toObject();
        QVERIFY(qAbs(gp["x"].toDouble() - 1) < 1e-9 && qAbs(gp["y"].toDouble() - 3) < 1e-9);
        QVERIFY(qAbs(msgparse::yawOf(goal["pose"].toObject()["orientation"].toObject()) - M_PI / 2) < 1e-9);

        const QJsonObject init = lastPublish(s, "/dashboard/initialpose");
        QCOMPARE(s.ops("publish", "/initialpose").size(), 0);
        QCOMPARE(init["header"].toObject()["frame_id"].toString(), QString("map"));
        const QJsonObject ip = init["pose"].toObject()["pose"].toObject()["position"].toObject();
        QVERIFY(qAbs(ip["x"].toDouble() - 1) < 1e-9 && qAbs(ip["y"].toDouble() - 3) < 1e-9);
        const QJsonArray cov = init["pose"].toObject()["covariance"].toArray();
        QCOMPARE(cov.size(), 36);
        QCOMPARE(cov[0].toDouble(), 0.25);
        QVERIFY(qAbs(cov[35].toDouble() - 0.0685) < 1e-4);
    }

    // map→odom 이 낡으면(10 s) 목표를 odom 프레임 그대로 보내고 이벤트를 남긴다.
    void staleMapOdomFallsBackToOdom()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 1, 2, M_PI / 2));
        QVERIFY(QTest::qWaitFor([&] { return !r.mapOdomFresh(); }, 11000));
        r.sendGoal(1, 0, 0);
        QVERIFY(QTest::qWaitFor([&] { return !lastPublish(s, "/goal_pose").isEmpty(); }, 2000));
        const QJsonObject goal = lastPublish(s, "/goal_pose");
        QCOMPARE(goal["header"].toObject()["frame_id"].toString(), QString("odom"));
        QCOMPARE(goal["pose"].toObject()["position"].toObject()["x"].toDouble(), 1.0);
        QVERIFY(r.events().join('\n').contains("odom 프레임"));
    }

    // 다른 주소로 붙으면 이전 로봇의 자취 · 거리 · 지도 · map→odom 을 지운다.
    void resetSessionOnNewHost()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        emit r.bridge()->message("/odom", odomMsg(0, 0));
        emit r.bridge()->message("/odom", odomMsg(0.2, 0));
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 1, 2, 0));
        QVERIFY(r.distance() > 0.19);
        QCOMPARE(r.trail().size(), 2);
        QVERIFY(r.mapOdomFresh());
        r.connectTo("127.0.0.1", s.port());   // 같은 주소: 그대로
        QCOMPARE(r.trail().size(), 2);
        MockServer other;
        QVERIFY(connectRobot(r, other));
        QCOMPARE(r.trail().size(), 0);
        QCOMPARE(r.distance(), 0.0);
        QVERIFY(!r.mapOdomFresh());
        QVERIFY(!r.hasMap());
        QCOMPARE(r.frameCount(), 0);
    }

    // 기록 시작 → 중지 → probe 를 거쳐도 IMU 50 · TF 200 throttle 이 남는다. raw.jsonl 첫 줄은 meta.
    void recordKeepsThrottle()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        r.toggleRecording();
        QVERIFY(r.recording());
        emit r.bridge()->message("/odom", odomMsg(0.5, 0));
        r.toggleRecording();
        r.probe();
        QCOMPARE(r.bridge()->throttleOf("/imu/data_raw"), 50);
        QCOMPARE(r.bridge()->throttleOf("/tf"), 200);
        QTest::qWait(300);
        for (const QJsonObject &o : s.ops("subscribe", "/imu/data_raw"))
            QCOMPARE(o["throttle_rate"].toInt(), 50);
        QVERIFY(s.ops("subscribe", "/imu/data_raw").size() >= 2);   // 처음 + probe
        QCOMPARE(s.ops("unsubscribe", "/odom").size(), 1);           // probe 만, 기록 중지로는 안 끊는다
        QVERIFY(r.recordPath().startsWith(QDir(m_dir.path()).absolutePath()));

        QFile raw(r.recordPath() + "/raw.jsonl");
        QVERIFY(raw.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = raw.readAll().split('\n');
        QVERIFY(QJsonDocument::fromJson(lines[0]).object()["meta"].toObject().contains("topics"));
        const QJsonObject l1 = QJsonDocument::fromJson(lines[1]).object();
        QCOMPARE(l1["topic"].toString(), QString("/odom"));
        QVERIFY(l1.contains("t") && l1.contains("wall") && l1.contains("msg"));
    }

    // 끊긴 채로 정지를 누르면 이벤트 · lastSendDropped
    void droppedWhileDisconnected()
    {
        Robot r;
        r.estop();
        QVERIFY(r.lastSendDropped());
        QVERIFY(r.events().join('\n').contains("정지 명령을 보내지 못함"));
    }

    // 2000×2000 지도: 이미지화는 다른 스레드, GUI 스레드는 100 ms 넘게 막히지 않는다.
    void bigMapDoesNotBlockGui()
    {
        Robot r;
        const int w = 2000, h = 2000;
        QJsonArray data;
        for (int i = 0; i < w * h; ++i)
            data.append(i % 997 == 0 ? 100 : 0);
        const QJsonObject map{{"info", QJsonObject{{"width", w}, {"height", h}, {"resolution", 0.05},
                                                   {"origin", QJsonObject{{"position", QJsonObject{{"x", -50}, {"y", -50}}},
                                                                          {"orientation", QJsonObject{{"w", 1}}}}}}},
                              {"data", data}};
        QSignalSpy poseSpy(&r, &Robot::mapPoseChanged), imageSpy(&r, &Robot::mapImageChanged);
        QElapsedTimer gap;
        gap.start();
        qint64 maxGap = 0;
        QTimer tick;
        tick.setInterval(5);
        QObject::connect(&tick, &QTimer::timeout, [&] { maxGap = qMax(maxGap, gap.restart()); });
        tick.start();
        QElapsedTimer call;
        call.start();
        emit r.bridge()->message("/map", map);
        const qint64 callMs = call.elapsed();
        gap.restart();
        QVERIFY(QTest::qWaitFor([&] { return r.mapImageSerial() == 1; }, 20000));
        tick.stop();
        qInfo().noquote() << QString("[perf] robot map 2000x2000 emit_ms=%1 max_gui_gap_ms=%2").arg(callMs).arg(maxGap);
        QVERIFY2(callMs < 100 && maxGap < 100, qPrintable(QString("GUI 막힘 emit %1 ms, 최대 %2 ms").arg(callMs).arg(maxGap)));
        QVERIFY(r.hasMap());
        QCOMPARE(r.mapImage().size(), QSize(w, h));
        QVERIFY(qAbs(r.mapCenter().x()) < 1e-4 && qAbs(r.mapCenter().y()) < 1e-4);   // origin -50 + 100/2 = 0

        // map→odom 이 와도 이미지는 다시 만들지 않는다(위치만).
        const int images = int(imageSpy.size());
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 1, 0, 0));
        QCOMPARE(int(imageSpy.size()), images);
        QVERIFY(poseSpy.size() >= 2);
        QVERIFY(qAbs(r.mapCenter().x() + 1) < 1e-4);
    }
};

QTEST_MAIN(TstRobot)
#include "tst_robot.moc"
