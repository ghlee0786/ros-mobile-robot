// Robot: bridge()->message 를 직접 내보내 토픽 해석을 보고, 보내는 JSON 은 모의 rosbridge 에서 캡처한다.
#include "mockserver.h"
#include "robot.h"

#include <QTemporaryDir>
#include <QtMath>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

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
        r.armTeleop(true);   // 목표는 잠금을 푼 뒤에만 나간다
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
        QTimer alive;   // 기다리는 동안 링크가 끊긴 것으로 보지 않게 /odom 을 계속 보낸다
        QObject::connect(&alive, &QTimer::timeout, [&] { s.publish("/odom", odomMsg(0, 0)); });
        alive.start(200);
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 1, 2, M_PI / 2));
        QVERIFY(QTest::qWaitFor([&] { return !r.mapOdomFresh(); }, 11000));
        r.armTeleop(true);
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

    // 서버 송신이 멈춰도 WebSocket 층은 ping 에 답한다(모의 서버 --freeze). 하트비트를 한 번도 못 받았어도
    // 3 s 넘게 메시지가 없으면 linkLost, 연결은 끊지 않는다(pong 이 오므로 반쯤 열린 연결이 아니다).
    void linkLostWhenDataStopsButPongs()
    {
        MockServer s;   // /rosapi/topics 응답 말고는 아무것도 보내지 않는다(QWebSocketServer 는 ping 에 자동으로 답한다)
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(!r.linkLost());
        QVERIFY(QTest::qWaitFor([&] { return r.linkLost(); }, 4800));
        QVERIFY(r.bridge()->connected());
        QVERIFY(r.bridge()->rxAge() > 3.0);
        s.publish("/odom", odomMsg(0, 0));
        QVERIFY(QTest::qWaitFor([&] { return !r.linkLost(); }, 2000));
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

    // B15: /wheel_cmd 는 바퀴 그래프의 목표 계열이 보일 때만 구독한다(기본 안 함, 바퀴 센서를 꺼 두면 켜도 안 함).
    void wheelCmdOnlyWhenWanted()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return s.ops("subscribe", "/wheel_states").size() == 1; }, 2000));
        QVERIFY(!r.bridge()->subscribed("/wheel_cmd"));
        QCOMPARE(s.ops("subscribe", "/wheel_cmd").size(), 0);
        r.setWheelCmdWanted(true);
        QVERIFY(QTest::qWaitFor([&] { return s.ops("subscribe", "/wheel_cmd").size() == 1; }, 2000));
        r.setWheelCmdWanted(false);
        QVERIFY(QTest::qWaitFor([&] { return s.ops("unsubscribe", "/wheel_cmd").size() == 1; }, 2000));
        r.setSensor("wheel", false);
        r.setWheelCmdWanted(true);
        QTest::qWait(200);
        QVERIFY(!r.bridge()->subscribed("/wheel_cmd"));
        r.setSensor("wheel", true);
        QVERIFY(QTest::qWaitFor([&] { return s.ops("subscribe", "/wheel_cmd").size() == 2; }, 2000));
    }

#ifdef Q_OS_WIN
    // B14: 3000² 지도를 받은 뒤 Robot 이 붙잡는 메모리는 이미지(36 MB) · 칸 바이트(9 MB) 수준이다.
    // 원본 QJsonObject(칸마다 16 B, 144 MB)를 들고 있으면 180 MB 를 넘는다.
    void bigMapRetainsCompactCopy()
    {
        auto privateMB = [] {
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof pmc);
            return double(pmc.PrivateUsage) / 1048576.0;
        };
        Robot r;
        const double before = privateMB();
        {
            const int w = 3000, h = 3000;
            QJsonArray data;
            for (int i = 0; i < w * h; ++i)
                data.append(i % 997 == 0 ? 100 : i % 13 == 0 ? -1 : 0);
            const QJsonObject map{{"info", QJsonObject{{"width", w}, {"height", h}, {"resolution", 0.05},
                                                       {"origin", QJsonObject{{"position", QJsonObject{{"x", 0}, {"y", 0}}},
                                                                              {"orientation", QJsonObject{{"w", 1}}}}}}},
                                  {"data", data}};
            emit r.bridge()->message("/map", map);
            QVERIFY(QTest::qWaitFor([&] { return r.mapImageSerial() == 1; }, 30000));
        }
        QTest::qWait(500);   // 원문은 다른 스레드에서 놓는다
        const double kept = privateMB() - before;
        qInfo().noquote() << QString("[perf] robot map 3000x3000 kept_mb=%1").arg(kept, 0, 'f', 1);
        QVERIFY2(kept < 100, qPrintable(QString("지도 뒤 %1 MB 를 붙잡음").arg(kept)));
        QCOMPARE(r.mapImage().size(), QSize(3000, 3000));
        QCOMPARE(r.mapImage().pixelColor(0, 2999), QColor(0, 0, 0));       // 0 번 칸(값 100)은 왼쪽 아래, 검정
        QCOMPARE(r.mapImage().pixelColor(1, 2999), QColor(255, 255, 255)); // 값 0 은 흰색

        // 색 바꿈은 들고 있는 칸 바이트로 다시 그린다
        r.setMapBinary(true);
        QVERIFY(QTest::qWaitFor([&] { return r.mapImageSerial() == 2; }, 30000));
        QCOMPARE(r.mapImage().size(), QSize(3000, 3000));
    }
#endif

    // B27: 재연결 뒤 다른 토픽이 들어오면 이전 세션의 하트비트 나이 때문에 linkLost 로 남지 않는다.
    void reconnectClearsLinkLostQuickly()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        s.publish("/link/heartbeat", {{"data", "beat"}});
        QTimer odom;
        QObject::connect(&odom, &QTimer::timeout, [&] { s.publish("/odom", odomMsg(0, 0)); });
        odom.start(100);
        QTest::qWait(300);
        QVERIFY(!r.linkLost());
        QVERIFY(QTest::qWaitFor([&] { return r.bridge()->stat("/link/heartbeat").age > 3.2; }, 5000));
        for (QWebSocket *c : std::as_const(s.clients))
            c->abort();
        QVERIFY(QTest::qWaitFor([&] { return !r.bridge()->connected(); }, 2000));
        QVERIFY(QTest::qWaitFor([&] { return r.bridge()->connected() && !s.clients.isEmpty(); }, 6000));
        QVERIFY(r.bridge()->stat("/link/heartbeat").age > 3.0);   // 하트비트는 다시 오지 않았다
        QVERIFY(QTest::qWaitFor([&] { return !r.linkLost(); }, 1000));
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1500) {
            QVERIFY(!r.linkLost());
            QTest::qWait(50);
        }
    }

    // B28: 하트비트만 끊기고 다른 토픽이 계속 들어오면 링크가 살아 있다. 모두 멈추면 linkLost.
    void heartbeatOnlyLossIsNotLinkLost()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        s.publish("/link/heartbeat", {{"data", "beat"}});
        QTimer odom;
        QObject::connect(&odom, &QTimer::timeout, [&] { s.publish("/odom", odomMsg(0, 0)); });
        odom.start(100);
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 5000) {
            QVERIFY2(!r.linkLost(), qPrintable(QString("%1 ms 에 linkLost").arg(t.elapsed())));
            QTest::qWait(50);
        }
        QVERIFY(r.bridge()->stat("/link/heartbeat").age > 4.0);
        odom.stop();
        QVERIFY(QTest::qWaitFor([&] { return r.linkLost(); }, 5000));
    }

    // B30: 레이저가 base_link 에서 떨어져 있으면 가장 가까운 장애물의 거리 · 각도를 모두 base_link 기준으로 고른다.
    void nearestInBaseFrame()
    {
        Robot r;
        emit r.bridge()->message("/tf_static", tfMsg("base_link", "laser", -0.5, 0, 0));
        QSignalSpy scan(&r, &Robot::scanChanged);
        // 레이저 앞 1.0 m → 로봇 앞 0.5 m, 레이저 뒤 0.9 m → 로봇 뒤 1.4 m
        emit r.bridge()->message("/scan", {{"header", QJsonObject{{"frame_id", "laser"}}}, {"angle_min", 0.0},
                                           {"angle_increment", M_PI}, {"range_min", 0.05}, {"range_max", 10.0},
                                           {"ranges", QJsonArray{1.0, 0.9}}});
        QVERIFY(scan.wait(500));
        QCOMPARE(r.scanCount(), 2);
        QVERIFY2(qAbs(r.nearestRange() - 0.5) < 1e-6, qPrintable(QString::number(r.nearestRange())));
        QVERIFY(qAbs(r.nearestAngle()) < 1e-6);
    }

    // B31: map 프레임 /plan 은 map→odom 이 바뀔 때마다 다시 odom 으로 투영한다.
    void mapPlanFollowsMapOdom()
    {
        Robot r;
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 1, 2, 0));
        const QJsonObject pose{{"pose", QJsonObject{{"position", QJsonObject{{"x", 2.0}, {"y", 2.5}}}}}};
        emit r.bridge()->message("/plan", {{"header", QJsonObject{{"frame_id", "map"}}}, {"poses", QJsonArray{pose}}});
        QCOMPARE(r.plan().size(), 1);
        QVERIFY(qAbs(r.plan().last().x() - 1.0) < 1e-5 && qAbs(r.plan().last().y() - 0.5) < 1e-5);
        QSignalSpy spy(&r, &Robot::planChanged);
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 2, 2, M_PI / 2));
        QCOMPARE(spy.size(), 1);
        // (2, 2.5) map → odom: dx 0, dy 0.5 를 -90° 돌리면 (0.5, 0)
        QVERIFY2(qAbs(r.plan().last().x() - 0.5) < 1e-5 && qAbs(r.plan().last().y()) < 1e-5,
                 qPrintable(QString("%1, %2").arg(r.plan().last().x()).arg(r.plan().last().y())));
        emit r.bridge()->message("/tf", tfMsg("map", "odom", 2, 2, M_PI / 2));   // 같은 변환: 다시 알리지 않는다
        QCOMPARE(spy.size(), 1);
    }

    // B43: GUI 가 멈췄다가 몰려 받은 메시지로 Hz 가 부풀지 않는다(header.stamp 간격으로 센다).
    void hzFromStampsAfterBurst()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        const qint64 base = QDateTime::currentMSecsSinceEpoch() - 1000;
        for (int i = 0; i < 33; ++i) {   // 33 Hz 로 찍힌 1 s 분량이 한꺼번에 온다
            const qint64 ms = base + i * 30;
            QJsonObject m = odomMsg(0, 0);
            m["header"] = QJsonObject{{"stamp", QJsonObject{{"sec", double(ms / 1000)}, {"nanosec", double(ms % 1000) * 1e6}}}};
            s.publish("/odom", m);
        }
        QVERIFY(QTest::qWaitFor([&] { return r.bridge()->stat("/odom").recent == 33; }, 2000));
        const double hz = r.bridge()->stat("/odom").hz;
        QVERIFY2(hz > 31 && hz < 35, qPrintable(QString("%1 Hz").arg(hz)));
    }
};

QTEST_MAIN(TstRobot)
#include "tst_robot.moc"
