// 조종 잠금(teleopArmed): 잠긴 동안 drive() 입력은 버리고 0 을 10 Hz 로 계속 보낸다.
// 연결 · 재연결 · estop · 무입력이면 저절로 잠긴다.
#include "mockserver.h"
#include "robot.h"

#include <QTemporaryDir>
#include <QtTest>

namespace {
bool connectRobot(Robot &r, const MockServer &s)
{
    r.connectTo("127.0.0.1", s.port());
    return QTest::qWaitFor([&] { return r.bridge()->connected() && !s.clients.isEmpty(); }, 5000);
}

QList<double> cmdLinear(const MockServer &s)
{
    QList<double> out;
    for (const QJsonObject &o : s.ops("publish", "/teleop/cmd_vel"))
        out << o["msg"].toObject()["linear"].toObject()["x"].toDouble();
    return out;
}
} // namespace

class TstArm : public QObject
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

    // 처음에는 잠김. 잠긴 채 drive(1,0) 은 버리고 0 만 10 Hz 로 나간다.
    void lockedDropsInput()
    {
        MockServer s;
        Robot r;
        QVERIFY(!r.teleopArmed());
        QVERIFY(connectRobot(r, s));
        QVERIFY(!r.teleopArmed());
        QTest::qWait(300);
        s.received.clear();
        r.drive(1, 0);
        QTest::qWait(1000);
        const QList<double> v = cmdLinear(s);
        QVERIFY2(v.size() >= 7 && v.size() <= 13, qPrintable(QString::number(v.size())));
        for (double x : v)
            QCOMPARE(x, 0.0);
    }

    // 잠금을 풀면 입력이 나간다. 송신은 drive 즉시 한 번 + 10 Hz 주기다.
    void armedSends()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        QSignalSpy spy(&r, &Robot::teleopArmedChanged);
        r.armTeleop(true);
        QVERIFY(r.teleopArmed());
        QCOMPARE(spy.size(), 1);
        QTest::qWait(300);   // 앞서 보낸 0 이 서버에 다 닿게 둔다
        s.received.clear();
        r.drive(1, 0);
        QTest::qWait(1000);
        const QList<double> v = cmdLinear(s);
        QVERIFY2(v.size() >= 8 && v.size() <= 13, qPrintable(QString::number(v.size())));
        for (double x : v)
            QVERIFY(qAbs(x - r.maxSpeed()) < 1e-9);
    }

    // estop 은 잠그고, 그 뒤 drive 는 버린다.
    void estopLocks()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        r.armTeleop(true);
        r.drive(1, 0);
        r.estop();
        QVERIFY(!r.teleopArmed());
        r.setMode("키보드 조종");
        QTest::qWait(300);
        s.received.clear();
        r.drive(1, 0);
        QTest::qWait(400);
        const QList<double> v = cmdLinear(s);
        QVERIFY(!v.isEmpty());
        for (double x : v)
            QCOMPARE(x, 0.0);
    }

    // 무입력이 armIdleMs 를 넘으면 잠긴다. 누르고 있는 동안(0 이 아닌 입력)은 잠그지 않는다.
    void idleLocks()
    {
        Robot r;
        r.setArmIdleMs(300);
        r.armTeleop(true);
        r.drive(1, 0);
        QTest::qWait(700);
        QVERIFY(r.teleopArmed());
        r.drive(0, 0);
        QVERIFY(QTest::qWaitFor([&] { return !r.teleopArmed(); }, 1500));
    }

    // 재연결(다른 서버로 붙음)하면 잠긴다.
    void reconnectLocks()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        r.armTeleop(true);
        MockServer other;
        QVERIFY(connectRobot(r, other));
        QVERIFY(!r.teleopArmed());
    }

    // B7: linkLost 동안에는 잠금 해제를 거부하고 비0 을 보내지 않는다.
    void armRefusedWhileLinkLost()
    {
        MockServer s;   // 데이터를 보내지 않는다 → 3 s 뒤 linkLost
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return r.linkLost(); }, 5000));
        s.received.clear();
        r.armTeleop(true);
        QVERIFY(!r.teleopArmed());
        r.drive(1, 0);
        QTest::qWait(400);
        const QList<double> v = cmdLinear(s);
        QVERIFY(!v.isEmpty());
        for (double x : v)
            QCOMPARE(x, 0.0);
    }

    // B8: NaN · 무한대 입력은 0 으로 본다(qBound 가 NaN 을 -1 로 만들지 않게).
    void nonFiniteInputIsZero()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        r.armTeleop(true);
        QTest::qWait(200);
        s.received.clear();
        r.drive(qQNaN(), 0);
        QTest::qWait(250);
        r.drive(0, qQNaN());
        QTest::qWait(250);
        r.drive(qInf(), -qInf());
        QTest::qWait(250);
        const auto cmds = s.ops("publish", "/teleop/cmd_vel");
        QVERIFY(cmds.size() >= 6);
        for (const QJsonObject &o : cmds) {
            const QJsonObject m = o["msg"].toObject();
            QCOMPARE(m["linear"].toObject()["x"].toDouble(), 0.0);
            QCOMPARE(m["angular"].toObject()["z"].toDouble(), 0.0);
        }
    }

    // B34: 키보드 조종 cmd_vel 은 평균 10 Hz(간격 95..105 ms) — Windows 기본 CoarseTimer 는 109 ms 로 나갔다.
    void teleopRateIs10Hz()
    {
        MockServer s;
        QElapsedTimer clock;
        clock.start();
        QList<qint64> at;
        s.onMessage = [&](QWebSocket *, const QJsonObject &o) {
            if (o["op"].toString() == "publish" && o["topic"].toString() == "/teleop/cmd_vel")
                at.append(clock.elapsed());
        };
        Robot r;
        QVERIFY(connectRobot(r, s));
        QTest::qWait(300);
        at.clear();
        QTest::qWait(3200);
        QVERIFY(at.size() >= 20);
        const double avg = double(at.last() - at.first()) / (at.size() - 1);
        QVERIFY2(avg >= 95 && avg <= 105, qPrintable(QString("평균 간격 %1 ms").arg(avg, 0, 'f', 1)));
    }

    // B2 · B3 · B9: 직진 · 목표는 잠금 해제 + 키보드 조종(목표는 정지 아님) + 직진 진행 중 아님 + |거리| ≤ 2 m 일 때만.
    // 비상 정지 뒤에는 모드를 되돌려도 다시 잠금을 풀기 전까지 나가지 않고, sendGoal 이 정지 모드를 바꾸지 않는다.
    void moveCommandsNeedArmAndMode()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        auto count = [&](const QString &topic) { return s.ops("publish", topic).size(); };
        r.straight(1.0);
        r.sendGoal(1, 0, 0);   // 잠김
        r.armTeleop(true);
        r.estop();
        r.sendGoal(1, 0, 0);   // 정지 모드
        QCOMPARE(r.mode(), QString("정지"));
        r.setMode("키보드 조종");
        r.straight(1.0);       // 비상 정지로 잠김
        r.sendGoal(1, 0, 0);
        QCOMPARE(r.mode(), QString("키보드 조종"));
        r.armTeleop(true);
        for (double d : {5.0, -2.5, qQNaN(), qInf()})
            r.straight(d);     // 범위 밖 · 숫자 아님
        QTest::qWait(300);
        QCOMPARE(count("/straight_drive/goal"), 0);
        QCOMPARE(count("/goal_pose"), 0);

        r.straight(-1.0);      // 이제 나간다
        QVERIFY(QTest::qWaitFor([&] { return count("/straight_drive/goal") == 1; }, 2000));
        QCOMPARE(s.ops("publish", "/straight_drive/goal").last()["msg"].toObject()["x"].toDouble(), -1.0);
        emit r.bridge()->message("/straight_drive/status", {{"data", R"({"state":"started","distance":-1.0,"speed":0.08})"}});
        QVERIFY(r.straightRunning());
        r.straight(1.0);       // 직진 진행 중
        r.sendGoal(1, 0, 0);
        QTest::qWait(300);
        QCOMPARE(count("/straight_drive/goal"), 1);
        QCOMPARE(count("/goal_pose"), 0);
    }

    // B11 · B12: 자율주행(목표 진행) 중 직진은 막고, 자율주행에서 벗어나면 Nav2 목표를 취소한다.
    void leavingAutoCancelsGoal()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        auto cancels = [&] {
            int n = 0;
            for (const QJsonObject &o : s.ops("call_service"))
                n += o["service"].toString() == "/navigate_to_pose/_action/cancel_goal";
            return n;
        };
        r.armTeleop(true);
        r.sendGoal(1, 0, 0);
        QCOMPARE(r.mode(), QString("자율주행"));
        r.straight(1.0);
        QTest::qWait(300);
        QCOMPARE(s.ops("publish", "/goal_pose").size(), 1);
        QCOMPARE(s.ops("publish", "/straight_drive/goal").size(), 0);
        QCOMPARE(cancels(), 0);
        r.setMode("키보드 조종");
        QVERIFY(QTest::qWaitFor([&] { return cancels() == 1; }, 1000));
    }

    // B10: --no-publish 에서는 정지 모드 · 비상 정지도 call_service cancel_goal 을 보내지 않는다.
    void noPublishSendsNoCancel()
    {
        qputenv("ROBOT_DASHBOARD_NO_PUBLISH", "1");
        MockServer s;
        Robot r;
        qputenv("ROBOT_DASHBOARD_NO_PUBLISH", "0");
        QVERIFY(connectRobot(r, s));
        r.setMode("자율주행");
        r.setMode("정지");
        r.setMode("키보드 조종");
        r.estop();
        QTest::qWait(400);
        for (const QJsonObject &o : s.ops("call_service"))
            QVERIFY2(o["service"].toString().startsWith("/rosapi/"), qPrintable(o["service"].toString()));
        QCOMPARE(s.ops("publish").size(), 0);
        QCOMPARE(s.ops("advertise").size(), 0);
    }

    // B1: 수신이 1 s 넘게 없는(반쯤 열렸을 수 있는) 연결에서 누른 정지는 경고한다. 평소에는 경고하지 않는다.
    void estopWarnsWhenRxStale()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        s.publish("/odom", {});
        QTest::qWait(100);
        r.estop();
        QVERIFY(!r.lastSendDropped());
        QTest::qWait(1500);   // 모의 서버는 더 보내지 않는다
        r.estop();
        QVERIFY(r.lastSendDropped());
    }

    // B1: 끊긴 동안 누른 비상 정지는 재연결 즉시 다시 보낸다(advertise 뒤에 0 Twist ×3 · cancel · cancel_goal).
    void reconnectResendsStop()
    {
        MockServer s;
        Robot r;
        QVERIFY(connectRobot(r, s));
        r.armTeleop(true);
        for (QWebSocket *c : std::as_const(s.clients))
            c->abort();
        QVERIFY(QTest::qWaitFor([&] { return !r.bridge()->connected(); }, 2000));
        r.estop();
        QVERIFY(r.lastSendDropped());
        s.received.clear();
        QVERIFY(QTest::qWaitFor([&] { return r.bridge()->connected() && !s.clients.isEmpty(); }, 6000));
        QVERIFY(QTest::qWaitFor([&] { return s.ops("publish", "/straight_drive/cancel").size() == 1; }, 1000));
        int adv = -1, cancel = -1, zeros = 0, cancelGoal = 0;
        for (int i = 0; i < s.received.size(); ++i) {
            const QJsonObject &o = s.received[i];
            const QString op = o["op"].toString(), topic = o["topic"].toString();
            if (op == "advertise" && topic == "/straight_drive/cancel")
                adv = i;
            if (op == "publish" && topic == "/straight_drive/cancel")
                cancel = i;
            if (op == "publish" && topic == "/teleop/cmd_vel")
                zeros += o["msg"].toObject()["linear"].toObject()["x"].toDouble() == 0
                         && o["msg"].toObject()["angular"].toObject()["z"].toDouble() == 0;
            if (op == "call_service" && o["service"].toString() == "/navigate_to_pose/_action/cancel_goal")
                ++cancelGoal;
        }
        QVERIFY(adv >= 0 && cancel > adv);
        QVERIFY(zeros >= 3);
        QCOMPARE(cancelGoal, 1);
        QCOMPARE(r.mode(), QString("정지"));
        QVERIFY(!r.teleopArmed());
    }
};

QTEST_MAIN(TstArm)
#include "tst_arm.moc"
