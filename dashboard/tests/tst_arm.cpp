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
};

QTEST_MAIN(TstArm)
#include "tst_arm.moc"
