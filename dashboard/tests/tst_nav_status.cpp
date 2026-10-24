// /nav/status(PC nav_goal_bridge.py JSON) 해석 · 이 목표의 상태인지 가리기 · 표시 상태 전환(이벤트) · Robot 구독
#include "msgparse.h"
#include "robot.h"

#include <QJsonDocument>
#include <QtTest>

using namespace msgparse;

namespace {
QString status(int seq, const QString &state, double gx = 1.0, double gy = 2.0, QJsonValue dist = QJsonValue::Null,
               QJsonValue eta = QJsonValue::Null, const QString &reason = {}, int code = 0)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"seq", seq}, {"state", state}, {"goal", QJsonObject{{"x", gx}, {"y", gy}, {"yaw", 0.5}}},
        {"distance_remaining", dist}, {"eta_s", eta}, {"recoveries", 0}, {"error_code", code}, {"error_msg", ""},
        {"reason", reason}, {"stamp", 1700000000.5}}).toJson(QJsonDocument::Compact));
}
} // namespace

class TstNavStatus : public QObject
{
    Q_OBJECT
private slots:
    void parseRunning()
    {
        const QVariantMap st = parseNavStatus(status(3, "running", 1.0, 2.0, 1.234, 12.4));
        QCOMPARE(st["state"].toString(), QString("running"));
        QCOMPARE(st["seq"].toInt(), 3);
        QCOMPARE(st["distance"].toDouble(), 1.234);
        QCOMPARE(st["eta"].toDouble(), 12.4);
        QCOMPARE(st["goalX"].toDouble(), 1.0);
        QVERIFY(st["hasGoal"].toBool());
        QCOMPARE(st["text"].toString(), QString("이동 중 · 남은 1.23 m · 약 12 s"));
    }

    void parseNullsAndStates()
    {
        QVariantMap st = parseNavStatus(status(1, "running"));
        QCOMPARE(st["distance"].toDouble(), -1.0);
        QCOMPARE(st["eta"].toDouble(), -1.0);
        QCOMPARE(st["text"].toString(), QString("이동 중"));
        QCOMPARE(parseNavStatus(status(1, "planning"))["text"].toString(), QString("경로 계획 중…"));
        QCOMPARE(parseNavStatus(status(1, "succeeded"))["text"].toString(), QString("도착"));
        QCOMPARE(parseNavStatus(status(1, "canceled"))["text"].toString(), QString("취소됨"));
        st = parseNavStatus(status(1, "failed", 1, 2, {}, {}, "출발점이 장애물에 막혀 있습니다", 208));
        QCOMPARE(st["text"].toString(), QString("실패 — 출발점이 장애물에 막혀 있습니다"));
        QCOMPARE(st["errorCode"].toInt(), 208);
        QVERIFY(parseNavStatus(status(1, "failed", 1, 2, {}, {}, {}, 9000))["text"].toString().contains("9000"));
        // 깨진 JSON · 목표 없음(idle)
        QCOMPARE(parseNavStatus("{nope")["state"].toString(), QString());
        const QVariantMap idle = parseNavStatus(R"({"seq":0,"state":"idle","goal":null})");
        QVERIFY(!idle["hasGoal"].toBool());
        QCOMPARE(idle["text"].toString(), QString());
    }

    void matches()
    {
        QVERIFY(navStatusMatches(parseNavStatus(status(5, "succeeded")), 1.0, 2.0, 5));
        QVERIFY(navStatusMatches(parseNavStatus(status(5, "succeeded", 1.015, 2.0)), 1.0, 2.0, 0));
        QVERIFY(!navStatusMatches(parseNavStatus(status(5, "succeeded", 1.05, 2.0)), 1.0, 2.0, 0));   // 다른 목표
        // 같은 자리에 다시 보냈을 때 5 s 마다 다시 오는 이전 목표의 끝 상태는 버린다
        QVERIFY(!navStatusMatches(parseNavStatus(status(4, "failed")), 1.0, 2.0, 5));
        // 진행 중은 늘 마지막 목표의 것 — PC 노드가 다시 떠 seq 가 1 부터여도 받는다
        QVERIFY(navStatusMatches(parseNavStatus(status(1, "planning")), 1.0, 2.0, 5));
        QVERIFY(!navStatusMatches(parseNavStatus(R"({"seq":9,"state":"idle","goal":null})"), 1.0, 2.0, 0));
        QVERIFY(!navStatusMatches(parseNavStatus("garbage"), 0, 0, 0));
    }

    void events()
    {
        const QVariantMap sent{{"state", "sent"}};
        const QVariantMap plan = parseNavStatus(status(2, "planning"));
        const QVariantMap run1 = parseNavStatus(status(2, "running", 1, 2, 1.0, 5.0));
        const QVariantMap run2 = parseNavStatus(status(2, "running", 1, 2, 0.8, 4.0));
        QCOMPARE(navStatusEvent(sent, plan), QString("Nav2 경로 계획 중"));
        QCOMPARE(navStatusEvent(plan, run1), QString("Nav2 이동 시작"));
        QCOMPARE(navStatusEvent(run1, run2), QString());   // 2 Hz 진행 갱신은 로그에 남기지 않는다
        QCOMPARE(navStatusEvent(run2, parseNavStatus(status(2, "succeeded"))), QString("Nav2 도착"));
        QCOMPARE(navStatusEvent(run2, parseNavStatus(status(2, "failed", 1, 2, {}, {}, "목표가 지도 밖입니다"))),
                 QString("Nav2 실패 — 목표가 지도 밖입니다"));
        QCOMPARE(navStatusEvent(run2, parseNavStatus(status(2, "canceled"))), QString("Nav2 목표 취소됨"));
        // 같은 상태라도 새 목표(seq)면 다시 남긴다
        QCOMPARE(navStatusEvent(parseNavStatus(status(2, "failed")), parseNavStatus(status(3, "failed", 1, 2, {}, {}, "x"))),
                 QString("Nav2 실패 — x"));
        QCOMPARE(navStatusEvent(parseNavStatus(status(2, "failed")), parseNavStatus(status(2, "failed"))), QString());
    }

    // 연결 없이도 sendGoal 은 거부되므로 Robot 의 처음 상태 · 모드 전환만 본다(구독 흐름은 시나리오 nav_ok · nav_fail)
    void robotInitial()
    {
        Robot r;
        QVERIFY(r.navStatus().isEmpty());
        QVERIFY(!r.sendGoal(1, 2, 0).isEmpty());   // 잠김 · 끊김
        QVERIFY(r.navStatus().isEmpty());
        QVERIFY(!r.bridge()->wantedTopics().contains("/nav/status"));
    }
};

QTEST_MAIN(TstNavStatus)
#include "tst_nav_status.moc"
