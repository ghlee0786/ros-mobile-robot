// 업데이트 확인(src/update/updater): semver 비교 · 릴리스 JSON 해석 · 흉내 낸 HTTP 응답에 따른 알림 상태.
#include "update/updater.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray releaseJson(const QString &tag)
{
    return QString(R"({"tag_name":"%1","name":"%1","body":"## 바뀐 점\n- 업데이트 알림",
        "assets":[{"name":"notes.txt","browser_download_url":"http://x/notes.txt","size":3},
                  {"name":"RobotDashboard-Setup-%2.exe","browser_download_url":"http://x/RobotDashboard-Setup-%2.exe","size":12345}]})")
        .arg(tag, QString(tag).remove(0, 1)).toUtf8();
}

// 요청 하나마다 정해 둔 상태 코드 · 본문을 돌려주는 최소 HTTP 서버
class FakeHttp : public QTcpServer
{
public:
    int status = 200;
    QByteArray body;
    QByteArray lastRequest;
    FakeHttp()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                lastRequest += s->readAll();
                if (!lastRequest.contains("\r\n\r\n"))
                    return;
                s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                         + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        });
    }
    QByteArray url() const { return "http://127.0.0.1:" + QByteArray::number(serverPort()) + "/releases/latest"; }
};
}

class TestUpdater : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    // 한 번 조회하고 끝날 때까지 기다린다
    static void runCheck(Updater &u, bool manual = false)
    {
        u.check(manual);
        QTRY_VERIFY_WITH_TIMEOUT(u.state() != "checking", 10000);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("RobotDashboardTest");
        QCoreApplication::setApplicationName("tst_update");
        QCoreApplication::setApplicationVersion("0.1.0");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        qputenv("ROBOT_DASHBOARD_TEST", "1");   // 생성자의 자동 조회를 끈다(주소를 주기 전에 만든다)
    }
    void init() { QSettings().clear(); }

    void compare_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");
        QTest::newRow("patch") << "v0.1.0" << "v0.1.1" << -1;
        QTest::newRow("minor") << "v0.1.1" << "v0.2.0" << -1;
        QTest::newRow("major") << "v1.0.0" << "v0.9.9" << 1;
        QTest::newRow("prefix-v") << "v0.1.0" << "0.1.0" << 0;
        QTest::newRow("numeric-not-text") << "v0.10.0" << "v0.9.0" << 1;
        QTest::newRow("short") << "v1.2" << "1.2.0" << 0;
        QTest::newRow("prerelease-ignored") << "v0.2.0-rc1" << "0.2.0" << 0;
        QTest::newRow("build-ignored") << "0.2.0+abc" << "v0.2.0" << 0;
        QTest::newRow("prerelease-newer-core") << "v0.2.0-beta" << "0.1.9" << 1;
    }
    void compare()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(Updater::compareVersions(a, b), expected);
        QCOMPARE(Updater::compareVersions(b, a), -expected);
    }

    void parse()
    {
        bool ok = false;
        const Updater::Release r = Updater::parseRelease(releaseJson("v0.2.0"), &ok);
        QVERIFY(ok);
        QCOMPARE(r.tag, "v0.2.0");
        QVERIFY(r.body.contains("업데이트 알림"));
        QCOMPARE(r.assetName, "RobotDashboard-Setup-0.2.0.exe");
        QCOMPARE(r.assetUrl, "http://x/RobotDashboard-Setup-0.2.0.exe");
        QCOMPARE(r.assetSize, 12345);

        Updater::parseRelease("{broken", &ok);
        QVERIFY(!ok);
        Updater::parseRelease(R"({"message":"Not Found"})", &ok);
        QVERIFY(!ok);
    }

    void http_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("available");
        QTest::newRow("200 새 버전") << 200 << releaseJson("v0.2.0") << "available" << true;
        QTest::newRow("200 같은 버전") << 200 << releaseJson("v0.1.0") << "current" << false;
        QTest::newRow("200 옛 버전") << 200 << releaseJson("v0.0.9") << "current" << false;
        QTest::newRow("404") << 404 << QByteArray(R"({"message":"Not Found"})") << "failed" << false;
        QTest::newRow("403 rate limit") << 403 << QByteArray(R"({"message":"API rate limit exceeded"})") << "failed" << false;
        QTest::newRow("깨진 JSON") << 200 << QByteArray("{\"tag_name\": ") << "failed" << false;
    }
    void http()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, state);
        QFETCH(bool, available);
        FakeHttp srv;
        srv.status = status;
        srv.body = body;
        qputenv("ROBOT_DASHBOARD_UPDATE_URL", srv.url());
        Updater u;
        runCheck(u);
        QCOMPARE(u.state(), state);
        QCOMPARE(u.available(), available);
        QCOMPARE(u.message(), QString());   // 자동 확인은 실패해도 화면에 아무것도 띄우지 않는다
        QVERIFY(srv.lastRequest.toLower().contains("user-agent: robotdashboard/0.1.0"));
        qunsetenv("ROBOT_DASHBOARD_UPDATE_URL");
    }

    void skipAndLater()
    {
        FakeHttp srv;
        srv.body = releaseJson("v0.2.0");
        qputenv("ROBOT_DASHBOARD_UPDATE_URL", srv.url());
        {
            Updater u;
            runCheck(u);
            QVERIFY(u.available());
            u.later();
            QVERIFY(!u.available());
            u.skip();
            QCOMPARE(QSettings().value("skipVersion").toString(), "v0.2.0");
        }
        Updater u;
        runCheck(u);
        QCOMPARE(u.state(), "skipped");   // 건너뛴 버전은 다시 알리지 않는다
        QVERIFY(!u.available());
        runCheck(u, true);                // 직접 확인하면 다시 보인다
        QVERIFY(u.available());
        srv.body = releaseJson("v0.3.0");
        Updater u2;
        runCheck(u2);
        QVERIFY(u2.available());           // 더 새 버전은 다시 알린다
        qunsetenv("ROBOT_DASHBOARD_UPDATE_URL");
    }

    void manualMessages()
    {
        FakeHttp srv;
        srv.body = releaseJson("v0.1.0");
        qputenv("ROBOT_DASHBOARD_UPDATE_URL", srv.url());
        Updater u;
        runCheck(u, true);
        QVERIFY(u.message().contains("최신 버전"));
        srv.status = 403;
        runCheck(u, true);
        QVERIFY(u.message().contains("403"));
        qunsetenv("ROBOT_DASHBOARD_UPDATE_URL");
    }

    void blockedWhileDriving()
    {
        FakeHttp srv;
        srv.body = releaseJson("v0.2.0");
        qputenv("ROBOT_DASHBOARD_UPDATE_URL", srv.url());
        Updater u;
        runCheck(u);
        u.setProperty("driving", true);
        u.downloadAndInstall();
        QVERIFY(!u.downloading());
        QCOMPARE(u.message(), "주행을 멈춘 뒤 설치하세요");
        qunsetenv("ROBOT_DASHBOARD_UPDATE_URL");
    }

    void checkSetting()
    {
        Updater u;
        QVERIFY(u.checkEnabled());
        u.setCheckEnabled(false);
        QCOMPARE(QSettings().value("updateCheck").toBool(), false);
        QVERIFY(!u.checkEnabled());
    }
};

QTEST_MAIN(TestUpdater)
#include "tst_update_updater.moc"
