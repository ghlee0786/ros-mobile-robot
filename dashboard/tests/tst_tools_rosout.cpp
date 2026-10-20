// B49: 로그 탭이 /rosout 을 구독할 때, 실기처럼 로그가 몰려 왔다 멈추면 '/rosout 지연' 이벤트가 쌓이지 않아야 한다.
// /rosout 은 일이 있을 때만 오는 토픽이다. 같은 모양으로 오다 멈춘 /odom 은 여전히 지연 이벤트를 남긴다(대조).
#include "mockserver.h"
#include "robot.h"
#include "tools/logmodel.h"

#include <QTemporaryDir>
#include <QtTest>

class TstToolsRosout : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    static QStringList lateEvents(const Robot &r, const QString &topic)
    {
        return r.events().filter(topic + " ").filter("지연");
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("RobotDashboardTest");
        QCoreApplication::setApplicationName("RobotDashboardUnitTest");
        qputenv("ROBOT_DASHBOARD_DIR", m_dir.path().toUtf8());
    }

    void burstThenQuiet()
    {
        MockServer s;
        Robot r;
        r.connectTo("127.0.0.1", s.port());
        QVERIFY(QTest::qWaitFor([&] { return r.bridge()->connected() && !s.clients.isEmpty(); }, 5000));
        LogModel log;
        log.setBridge(r.bridge());
        log.setActive(true);
        QVERIFY(r.bridge()->subscribed("/rosout"));
        QVERIFY(r.bridge()->subscribed("/odom"));

        const QJsonObject line{{"level", 20}, {"name", "straight_drive"}, {"msg", "ready"},
                               {"stamp", QJsonObject{{"sec", 1}, {"nanosec", 0}}}};
        const QJsonObject odom{{"pose", QJsonObject{{"pose", QJsonObject{{"orientation", QJsonObject{{"w", 1}}}}}}}};
        for (int i = 0; i < 8; ++i) {   // 10 Hz 로 0.8 s 동안 오다가 멈춘다
            s.publish("/rosout", line);
            s.publish("/odom", odom);
            QTest::qWait(100);
        }
        QVERIFY(QTest::qWaitFor([&] { return !lateEvents(r, "/odom").isEmpty(); }, 3000));
        QTest::qWait(500);
        QCOMPARE(lateEvents(r, "/rosout"), QStringList());
        QCOMPARE(log.rowCount(), 8);
    }
};

QTEST_MAIN(TstToolsRosout)
#include "tst_tools_rosout.moc"
