// DiagnosticsModel: 'hardware_id: name' 묶기 · 그룹 순서 · 5 s STALE · 10 s 끊김 · 센서 상태 합성 · 필터.
#include "rosbridge.h"
#include "tools/diagnosticsmodel.h"

#include <QJsonArray>
#include <QtTest>

static QJsonObject status(int level, const QString &hw, const QString &name, const QString &msg = {})
{
    return {{"level", level}, {"hardware_id", hw}, {"name", name}, {"message", msg},
            {"values", QJsonArray{QJsonObject{{"key", "temp"}, {"value", "41.5"}}}}};
}

static QJsonObject array(const QJsonArray &s)
{
    return {{"header", QJsonObject{}}, {"status", s}};
}

class TstToolsDiag : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RobotDashboardUnitTest");
        QSettings().remove("inspector");
    }

    void groupsAndStale()
    {
        DiagnosticsModel m;
        m.ingest(array({status(0, "pico", "motor"), status(2, "lidar", "scan", "no data"), status(1, "imu", "rate")}), 1000);
        QCOMPARE(m.rowCount(), 3);
        QVERIFY(!m.synthetic());
        QCOMPARE(m.index(0).data(DiagnosticsModel::KeyRole).toString(), QString("lidar: scan"));   // Error 먼저
        QCOMPARE(m.index(0).data(DiagnosticsModel::GroupRole).toString(), QString("Error"));
        QCOMPARE(m.index(1).data(DiagnosticsModel::GroupRole).toString(), QString("Warn"));
        QCOMPARE(m.index(2).data(DiagnosticsModel::GroupRole).toString(), QString("Ok"));
        QCOMPARE(m.index(2).data(DiagnosticsModel::ValuesRole).toList().size(), 1);
        QCOMPARE(m.errorCount(), 1);

        // 같은 키는 덮어쓴다
        m.ingest(array({status(0, "lidar", "scan")}), 4000);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.errorCount(), 0);

        m.checkStale(5900);   // pico·imu 는 4.9 s — 아직
        QCOMPARE(m.staleCount(), 0);
        m.checkStale(6100);   // 5 s 넘음 → STALE
        QCOMPARE(m.staleCount(), 2);
        QCOMPARE(m.index(0).data(DiagnosticsModel::GroupRole).toString(), QString("Stale"));
        QCOMPARE(m.groupCounts().value("Stale").toInt(), 2);
        QCOMPARE(m.groupCounts().value("Ok").toInt(), 1);
        QVERIFY(!m.streamLost());
        m.checkStale(14100);   // 마지막 수신 4000 → 10 s 넘게 끊김
        QVERIFY(m.streamLost());
        m.ingest(array({}), 14200);
        QVERIFY(!m.streamLost());

        m.togglePin("pico: motor");
        QCOMPARE(m.index(0).data(DiagnosticsModel::KeyRole).toString(), QString("pico: motor"));
        QCOMPARE(m.index(0).data(DiagnosticsModel::GroupRole).toString(), QString("고정"));
        m.togglePin("pico: motor");

        m.setFilter("IMU");
        QCOMPARE(m.rowCount(), 1);
    }

    void synthetic()
    {
        QCOMPARE(DiagnosticsModel::levelFromState("수신 중"), int(DiagnosticsModel::Ok));
        QCOMPARE(DiagnosticsModel::levelFromState("확인 중"), int(DiagnosticsModel::Warn));
        QCOMPARE(DiagnosticsModel::levelFromState("데이터 없음"), int(DiagnosticsModel::Error));
        QCOMPARE(DiagnosticsModel::levelFromState("꺼짐"), int(DiagnosticsModel::Stale));
        QCOMPARE(DiagnosticsModel::levelFromState("ERROR"), int(DiagnosticsModel::Error));

        DiagnosticsModel m;
        m.setSynthetic({QVariantMap{{"label", "LiDAR"}, {"state", "수신 중"}, {"detail", "10 Hz"}},
                        QVariantMap{{"label", "IMU"}, {"state", "데이터 없음"}},
                        QVariantMap{{"label", "하트비트"}, {"state", "ok"}}});
        QVERIFY(m.synthetic());
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.index(0).data(DiagnosticsModel::NameRole).toString(), QString("IMU"));
        QVERIFY(m.index(0).data(DiagnosticsModel::SyntheticRole).toBool());
        QCOMPARE(m.errorCount(), 1);

        m.ingest(array({status(0, "pico", "motor")}), 1000);   // 실제 진단이 오면 합성 항목은 숨긴다
        QVERIFY(!m.synthetic());
        QCOMPARE(m.rowCount(), 1);
    }

    void subscribesOnlyWhenActive()
    {
        RosBridge b;
        DiagnosticsModel m;
        m.setBridge(&b);
        QVERIFY(!b.subscribed("/diagnostics"));
        m.setActive(true);
        QVERIFY(b.subscribed("/diagnostics"));
        emit b.message("/diagnostics", array({status(0, "pico", "motor")}));
        QCOMPARE(m.rowCount(), 1);
        m.setActive(false);
        QVERIFY(!b.subscribed("/diagnostics"));
    }
};

QTEST_MAIN(TstToolsDiag)
#include "tst_tools_diag.moc"
