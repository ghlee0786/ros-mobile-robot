#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSettings>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RobotDashboard");
    QCoreApplication::setApplicationName("RobotDashboard");

    // 자동 시험(ROBOT_DASHBOARD_TEST=1): 사용자 설정과 섞이지 않게 따로 저장하고, 실제 로봇에 자동으로
    // 붙지 않게 첫 실행 주소를 닿지 않는 곳(127.0.0.1:9)으로 둔다.
    if (qEnvironmentVariableIntValue("ROBOT_DASHBOARD_TEST") == 1) {
        QCoreApplication::setOrganizationName("RobotDashboardTest");
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
    return app.exec();
}
