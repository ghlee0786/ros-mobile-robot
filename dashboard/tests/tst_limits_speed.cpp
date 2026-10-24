// 속도 한계(SpeedLimits): 순수 함수, 워치독 파라미터 읽기 · 쓰기(모의 rosbridge), 자물쇠, QSettings, 키보드 배율 묶기.
#include "mockserver.h"
#include "robot.h"
#include "speedlimits.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

namespace {
// 모의 cmd_watchdog: max_* 를 get · set 하고 0 ~ 펌웨어 한계 밖은 거부한다. supported=false 면 옛 워치독(type 0).
struct Watchdog {
    QHash<QString, double> p{{"max_forward", 0.25}, {"max_reverse", 0.15}, {"max_angular", 0.6}};
    bool supported = true;
    int sets = 0;

    void attach(MockServer &s)
    {
        s.onMessage = [this](QWebSocket *c, const QJsonObject &o) {
            if (o["op"].toString() != "call_service")
                return;
            const QString srv = o["service"].toString();
            QJsonObject values;
            if (srv == "/cmd_watchdog/get_parameters") {
                QJsonArray out;
                for (const QJsonValue &n : o["args"].toObject()["names"].toArray()) {
                    if (supported && p.contains(n.toString()))
                        out.append(QJsonObject{{"type", 3}, {"double_value", p[n.toString()]}});
                    else
                        out.append(QJsonObject{{"type", 0}});
                }
                values = {{"values", out}};
            } else if (srv == "/cmd_watchdog/set_parameters") {
                ++sets;
                QJsonArray res;
                for (const QJsonValue &v : o["args"].toObject()["parameters"].toArray()) {
                    const QString n = v["name"].toString();
                    const double x = v["value"]["double_value"].toDouble();
                    const int i = SpeedLimits::indexOf(n);
                    const bool ok = supported && i >= 0 && x >= 0 && x <= SpeedLimits::firmware(i);
                    if (ok)
                        p[n] = x;
                    res.append(QJsonObject{{"successful", ok}, {"reason", ok ? "" : "범위 밖"}});
                }
                values = {{"results", res}};
            } else {
                return;
            }
            MockServer::send(c, {{"op", "service_response"}, {"id", o["id"]}, {"result", true}, {"values", values}});
        };
    }
};

bool connectRobot(Robot &r, const MockServer &s)
{
    r.connectTo("127.0.0.1", s.port());
    return QTest::qWaitFor([&] { return r.bridge()->connected() && !s.clients.isEmpty(); }, 5000);
}

QVariantMap row(Robot &r, int i) { return r.limits()->rows().at(i).toMap(); }

QJsonObject lastTwist(const MockServer &s)
{
    const auto l = s.ops("publish", "/teleop/cmd_vel");
    return l.isEmpty() ? QJsonObject() : l.last()["msg"].toObject();
}
} // namespace

class TstLimitsSpeed : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("RobotDashboardTest");
        QCoreApplication::setApplicationName("RobotDashboardLimitsTest");
        qputenv("ROBOT_DASHBOARD_DIR", m_dir.path().toUtf8());
        qputenv("ROBOT_DASHBOARD_NO_PUBLISH", "0");
    }
    void init() { QSettings().remove("speedLimits"); }
    void cleanupTestCase() { QSettings().clear(); }

    void pureFunctions()
    {
        QCOMPARE(SpeedLimits::indexOf("max_reverse"), int(SpeedLimits::Reverse));
        QCOMPARE(SpeedLimits::indexOf("max_speed"), -1);
        QCOMPARE(SpeedLimits::bound(SpeedLimits::Forward, 0.5), 0.35);
        QCOMPARE(SpeedLimits::bound(SpeedLimits::Angular, 0.9), 0.8);
        QCOMPARE(SpeedLimits::bound(SpeedLimits::Reverse, -0.1), 0.0);
        QVERIFY(std::isnan(SpeedLimits::bound(SpeedLimits::Forward, qQNaN())));
        // 키보드: 앞은 min(최대 속도, 최대 전진), 뒤는 min(최대 속도, 최대 후진), 회전은 min(배율, 최대 회전)
        QCOMPARE(SpeedLimits::linearCommand(1, 0.35, 0.25, 0.15), 0.25);
        QCOMPARE(SpeedLimits::linearCommand(-1, 0.35, 0.25, 0.15), -0.15);
        QCOMPARE(SpeedLimits::linearCommand(0.5, 0.1, 0.25, 0.15), 0.05);
        QCOMPARE(SpeedLimits::angularCommand(-1, 0.8, 0.6), -0.6);
        QCOMPARE(SpeedLimits::angularCommand(1, 0.8, 0.0), 0.0);

        double v[SpeedLimits::Count];
        const auto d = [](double x) { return QJsonObject{{"type", 3}, {"double_value", x}}; };
        QVERIFY(SpeedLimits::parseValues({{"values", QJsonArray{d(0.2), d(0.1), d(0.5)}}}, v));
        QCOMPARE(v[SpeedLimits::Angular], 0.5);
        QVERIFY(!SpeedLimits::parseValues({{"values", QJsonArray{QJsonObject{{"type", 0}}, d(0.1), d(0.5)}}}, v));
        QVERIFY(!SpeedLimits::parseValues({{"values", QJsonArray{d(0.2)}}}, v));
        QVERIFY(SpeedLimits::parseValues({{"values", QJsonArray{QJsonObject{{"type", 2}, {"integer_value", 0}}, d(0.1), d(0.5)}}}, v));
        QCOMPARE(v[SpeedLimits::Forward], 0.0);
    }

    // 처음 연결하면 워치독 값이 기준(QSettings 의 마지막 값은 덮어쓴다). 끊기면 경고.
    void firstConnectTakesWatchdog()
    {
        QSettings().setValue("speedLimits/max_forward", 0.1);
        MockServer s;
        Watchdog wd;
        wd.p["max_forward"] = 0.3;
        wd.attach(s);
        Robot r;
        QCOMPARE(r.limits()->maxForward(), 0.1);
        QVERIFY(row(r, 0)["warn"].toBool());   // 연결 전: 워치독 값을 모른다
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return r.limits()->support() == 1; }, 3000));
        QCOMPARE(r.limits()->maxForward(), 0.3);
        QVERIFY(!row(r, 0)["warn"].toBool());
        QCOMPARE(QSettings().value("speedLimits/max_forward").toDouble(), 0.3);
    }

    // 바꾸면 모아서 set_parameters 로 보내고 워치독 값을 다시 읽는다. 범위 밖은 펌웨어 한계로 자른다.
    void setSendsToWatchdog()
    {
        MockServer s;
        Watchdog wd;
        wd.attach(s);
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return r.limits()->support() == 1; }, 3000));
        r.limits()->setLimit("max_forward", 0.10);
        r.limits()->setLimit("max_forward", 0.20);   // 끄는 동안의 값들은 한 번에
        r.limits()->setLimit("max_angular", 5.0);
        QCOMPARE(r.limits()->maxAngular(), 0.8);
        QVERIFY(QTest::qWaitFor([&] { return wd.p["max_forward"] == 0.20 && wd.p["max_angular"] == 0.8; }, 3000));
        QCOMPARE(wd.sets, 1);
        QVERIFY(QTest::qWaitFor([&] { return !row(r, 0)["warn"].toBool() && row(r, 0)["actual"].toDouble() == 0.20; }, 3000));
    }

    // 자물쇠: 잠긴 줄은 setLimit 을 무시하고, 고정 상태와 값은 QSettings 에 남는다.
    void lockIgnoresChangesAndPersists()
    {
        {
            Robot r;
            r.limits()->setLimit("max_reverse", 0.10);
            r.limits()->setLocked("max_reverse", true);
            r.limits()->setLimit("max_reverse", 0.30);
            QCOMPARE(r.limits()->maxReverse(), 0.10);
            QVERIFY(row(r, 1)["locked"].toBool());
        }
        Robot r2;
        QCOMPARE(r2.limits()->maxReverse(), 0.10);
        QVERIFY(r2.limits()->locked(SpeedLimits::Reverse));
        r2.limits()->setLocked("max_reverse", false);
        r2.limits()->setLimit("max_reverse", 0.30);
        QCOMPARE(r2.limits()->maxReverse(), 0.30);
    }

    // 워치독이 다른 값을 돌려주면(거부 · 다른 곳에서 바꿈) 경고색.
    void rejectedShowsWarning()
    {
        MockServer s;
        Watchdog wd;
        wd.attach(s);
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return r.limits()->support() == 1; }, 3000));
        wd.p["max_angular"] = 0.4;   // MCP 등이 바꿨다
        r.limits()->refresh();
        QVERIFY(QTest::qWaitFor([&] { return row(r, 2)["warn"].toBool(); }, 3000));
        QCOMPARE(row(r, 2)["actual"].toDouble(), 0.4);
    }

    // 옛 워치독: 미지원(-1), 줄 경고 없이 대시보드 배율만 묶는다.
    void oldWatchdogUnsupported()
    {
        MockServer s;
        Watchdog wd;
        wd.supported = false;
        wd.attach(s);
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return r.limits()->support() == -1; }, 3000));
        QVERIFY(!row(r, 0)["warn"].toBool());
        r.limits()->setLimit("max_forward", 0.12);
        QTest::qWait(500);
        QCOMPARE(wd.sets, 0);
        QCOMPARE(r.limits()->maxForward(), 0.12);
    }

    // 최대 속도 슬라이더는 최대 전진을 넘지 않고, 키보드 전진 · 후진 · 회전은 각 한계로 잘린다.
    void keyboardBoundByLimits()
    {
        MockServer s;
        Watchdog wd;
        wd.attach(s);
        Robot r;
        QVERIFY(connectRobot(r, s));
        QVERIFY(QTest::qWaitFor([&] { return r.limits()->support() == 1; }, 3000));
        r.setMaxSpeed(0.35);
        QCOMPARE(r.maxSpeed(), 0.25);
        r.limits()->setLimit("max_forward", 0.2);
        QCOMPARE(r.maxSpeed(), 0.2);
        r.limits()->setLimit("max_reverse", 0.1);
        r.limits()->setLimit("max_angular", 0.5);
        r.armTeleop(true);
        r.drive(1, 1);
        QVERIFY(QTest::qWaitFor([&] { return lastTwist(s)["linear"].toObject()["x"].toDouble() > 0; }, 2000));
        QCOMPARE(lastTwist(s)["linear"].toObject()["x"].toDouble(), 0.2);
        QCOMPARE(lastTwist(s)["angular"].toObject()["z"].toDouble(), 0.5);
        r.drive(-1, -1);
        QVERIFY(QTest::qWaitFor([&] { return lastTwist(s)["linear"].toObject()["x"].toDouble() < 0; }, 2000));
        QCOMPARE(lastTwist(s)["linear"].toObject()["x"].toDouble(), -0.1);
        QCOMPARE(lastTwist(s)["angular"].toObject()["z"].toDouble(), -0.5);
        r.drive(0, 0);
    }
};

QTEST_MAIN(TstLimitsSpeed)
#include "tst_limits_speed.moc"
