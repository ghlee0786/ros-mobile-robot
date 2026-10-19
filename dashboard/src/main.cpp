#include "appconfig.h"
#include "testhooks.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RobotDashboard");
    QCoreApplication::setApplicationName("RobotDashboard");

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "로봇 대시보드. 인자 없이 켜면 마지막 주소로 자동 연결한다.\n"
        "시험 모드(--url 이나 --quit-after 를 줌)에서는 로그를 stderr 로도 낸다.\n"
        "actions 형식: [{\"t\":초,\"call\":\"robot.drive\",\"args\":[1,0]}, {\"t\":초,\"set\":\"scene.cam\",\"value\":\"위에서\"},\n"
        "  {\"t\":초,\"shot\":\"a.png\"}, {\"t\":초,\"key\":\"W\",\"hold_ms\":2000}, {\"wait\":\"bridge.connected\",\"timeout_s\":10}]\n"
        "  대상(objectName): win · robot · bridge(robot.bridge) · scene · inspector · dash. 각 항목은 앞 항목이 끝난 뒤, QML 로드 t 초 뒤에 돈다.\n"
        "종료 코드: 0 정상 · 1 QML 로드 실패 · 2 경고(--fail-on-warning) · 3 actions 실패."));
    p.addHelpOption();
    const QCommandLineOption url("url", "이 주소로 연결한다(마지막 주소 대신). --settings 가 없으면 설정을 RobotDashboardTest 조직에 따로 쓴다.", "ws://HOST:PORT");
    const QCommandLineOption settings("settings", "설정을 레지스트리 대신 INI 로 격리한다. DIR/NAME.ini 를 주면 실제 파일은 DIR/NAME/RobotDashboardTest/RobotDashboard.ini.", "PATH.ini");
    const QCommandLineOption noAuto("no-autoconnect", "자동 연결하지 않는다.");
    const QCommandLineOption page("page", "첫 화면(0 대시보드 · 1 데이터 점검 · 2 연결). 주면 4 s 뒤 연결 화면으로 가지 않는다.", "N");
    const QCommandLineOption size("size", "창 크기(저장된 크기보다 우선).", "WxH");
    const QCommandLineOption actions("actions", "시간에 맞춰 실행할 동작 목록(JSON).", "FILE.json");
    const QCommandLineOption shot("screenshot", "종료 직전 창을 PNG 로 저장한다.", "PATH.png");
    const QCommandLineOption state("state", "종료 직전 상태(win · bridge · robot · scene · warnings · proc · render 등)를 JSON 으로 쓴다.", "PATH.json");
    const QCommandLineOption quitAfter("quit-after", "QML 로드 SEC 초 뒤 screenshot · state 를 쓰고 끝낸다.", "SEC");
    const QCommandLineOption log("log", "모든 로그를 시각과 함께 파일에 쓴다.", "PATH.txt");
    const QCommandLineOption failOnWarning("fail-on-warning", "Binding loop · TypeError 같은 QML 경고가 나오면 종료 코드 2.");
    const QCommandLineOption trace("trace", "win · bridge · robot 속성을 주기적으로 JSON 줄로 쓴다.", "FILE");
    const QCommandLineOption traceHz("trace-hz", "--trace 주기(기본 2).", "HZ", "2");
    const QCommandLineOption dataDir("data-dir", "ROBOT_DASHBOARD_DIR=DIR 로 둔다(model · records 위치).", "DIR");
    const QCommandLineOption noModel("no-model", "ROBOT_DASHBOARD_NO_MODEL=1 로 둔다(모델 메시를 읽지 않음).");
    const QCommandLineOption noPublish("no-publish", "ROBOT_DASHBOARD_NO_PUBLISH=1 로 둔다(로봇에 명령을 보내지 않음).");
    p.addOptions({url, settings, noAuto, page, size, actions, shot, state, quitAfter, log, failOnWarning,
                  trace, traceHz, dataDir, noModel, noPublish});
    p.process(app);

    AppConfig *cfg = AppConfig::instance();
    if (p.isSet(url)) {
        const QUrl u(p.value(url));
        cfg->host = u.host();
        cfg->port = u.port(9090);
    }
    cfg->autoConnect = !p.isSet(noAuto);
    cfg->page = p.isSet(page) ? p.value(page).toInt() : -1;
    cfg->testMode = p.isSet(url) || p.isSet(quitAfter);

    TestOptions opt;
    opt.logPath = p.value(log);
    opt.screenshot = p.value(shot);
    opt.statePath = p.value(state);
    opt.actionsPath = p.value(actions);
    opt.tracePath = p.value(trace);
    opt.traceHz = p.value(traceHz).toDouble();
    opt.quitAfter = p.isSet(quitAfter) ? p.value(quitAfter).toDouble() : -1;
    opt.failOnWarning = p.isSet(failOnWarning);
    opt.testMode = cfg->testMode;
    if (p.isSet(size)) {
        const QStringList wh = p.value(size).split('x');
        if (wh.size() == 2)
            opt.size = QSize(wh[0].toInt(), wh[1].toInt());
    }
    TestHooks hooks(opt);

    if (p.isSet(dataDir))
        qputenv("ROBOT_DASHBOARD_DIR", p.value(dataDir).toLocal8Bit());
    if (p.isSet(noModel))
        qputenv("ROBOT_DASHBOARD_NO_MODEL", "1");
    if (p.isSet(noPublish))
        qputenv("ROBOT_DASHBOARD_NO_PUBLISH", "1");

    // --settings: 모든 QSettings()(QML Settings 포함)가 DIR/NAME/RobotDashboardTest/RobotDashboard.ini 를 쓰고
    // 레지스트리는 건드리지 않는다. 조직 이름은 바꾸지 않는다 — 조직마다 셰이더 캐시가 따로 생겨 첫 화면이 10 s 넘게 늦어진다.
    const bool test = qEnvironmentVariableIntValue("ROBOT_DASHBOARD_TEST") == 1;
    if (p.isSet(settings)) {
        const QFileInfo f(p.value(settings));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, f.absolutePath() + '/' + f.completeBaseName());
    }
    if (test || p.isSet(url) || p.isSet(settings))
        QCoreApplication::setOrganizationName("RobotDashboardTest");

    // 자동 시험(ROBOT_DASHBOARD_TEST=1): 사용자 설정과 섞이지 않게 따로 저장하고, 실제 로봇에 자동으로
    // 붙지 않게 첫 실행 주소를 닿지 않는 곳(127.0.0.1:9)으로 둔다.
    if (test) {
        QSettings s;
        if (!s.contains("host")) {
            s.setValue("host", "127.0.0.1");
            s.setValue("port", 9);
        }
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("RobotDashboard", "Main");
    auto *win = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!win)
        return 1;
    hooks.start(win);
    return hooks.exitCode(app.exec());
}
