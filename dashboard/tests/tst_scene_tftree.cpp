// TF 트리: 고정 프레임까지 연쇄 변환, 부모 쪽(map) 프레임 역변환, 정적 제외 페이드 단계.
#include "scene/tftree.h"

#include <QJsonArray>
#include <QQuaternion>
#include <QtTest>

static QJsonObject tf(const QString &parent, const QString &child, double x, double y, double z, double yawDeg)
{
    const QQuaternion q = QQuaternion::fromAxisAndAngle(0, 0, 1, float(yawDeg));
    return {{"header", QJsonObject{{"frame_id", parent}}},
            {"child_frame_id", child},
            {"transform", QJsonObject{{"translation", QJsonObject{{"x", x}, {"y", y}, {"z", z}}},
                                      {"rotation", QJsonObject{{"x", q.x()}, {"y", q.y()}, {"z", q.z()}, {"w", q.scalar()}}}}}};
}

static QJsonObject msg(const QJsonArray &transforms)
{
    return {{"transforms", transforms}};
}

static QVariantMap frame(const TfTree &t, const QString &name)
{
    for (const QVariant &v : t.frames())
        if (v.toMap()["name"] == name)
            return v.toMap();
    return {};
}

static bool near(QVector3D a, QVector3D b)
{
    return (a - b).length() < 1e-4f;
}

class TstSceneTfTree : public QObject
{
    Q_OBJECT
private slots:
    void chain()
    {
        TfTree t;
        t.ingest(msg({tf("odom", "base_link", 1, 0, 0, 90)}), false, 0);
        t.ingest(msg({tf("base_link", "laser", 0.1, 0, 0.2, 0)}), true, 0);
        t.update(0);
        QCOMPARE(t.frameCount(), 3);
        QVERIFY(near(frame(t, "odom")["position"].value<QVector3D>(), {}));
        QVERIFY(near(frame(t, "base_link")["position"].value<QVector3D>(), {1, 0, 0}));
        // base_link 이 90° 돌아 있으므로 laser 의 +x 0.1 은 odom 의 +y 0.1
        QVERIFY(near(frame(t, "laser")["position"].value<QVector3D>(), {1, 0.1f, 0.2f}));
        QVERIFY(near(frame(t, "laser")["parentPosition"].value<QVector3D>(), {1, 0, 0}));
        QCOMPARE(frame(t, "laser")["linked"].toBool(), true);
        const QQuaternion q = frame(t, "laser")["rotation"].value<QQuaternion>();
        QVERIFY(near(q.rotatedVector({1, 0, 0}), {0, 1, 0}));
    }

    void parentSideOfFixed()
    {
        TfTree t;
        t.ingest(msg({tf("map", "odom", 2, 0, 0, 90), tf("odom", "base_link", 0, 0, 0, 0)}), false, 0);
        t.update(0);
        // odom 기준 map 원점: (map→odom)^-1 → (0, 2)
        QVERIFY(near(frame(t, "map")["position"].value<QVector3D>(), {0, 2, 0}));
        QCOMPARE(frame(t, "map")["linked"].toBool(), false);
        t.setFixedFrame("map");
        QVERIFY(near(frame(t, "base_link")["position"].value<QVector3D>(), {2, 0, 0}));
    }

    void disconnectedFramesSkipped()
    {
        TfTree t;
        t.ingest(msg({tf("odom", "base_link", 0, 0, 0, 0), tf("world", "other", 1, 1, 0, 0)}), false, 0);
        t.update(0);
        QCOMPARE(t.frameCount(), 2);
        QVERIFY(frame(t, "other").isEmpty());
    }

    void fadeSteps()
    {
        const double T = 15;
        auto f = [&](double age, bool st = false) { return TfTree::fade(age, T, st); };
        QCOMPARE(f(0).gray, 0.0); QCOMPARE(f(0).alpha, 1.0);
        QCOMPARE(f(4.9).gray, 0.0);
        QCOMPARE(f(7.5).gray, 0.5); QCOMPARE(f(7.5).alpha, 1.0);
        QCOMPARE(f(10).gray, 1.0); QCOMPARE(f(10).alpha, 1.0);
        QCOMPARE(f(12.5).alpha, 0.5);
        QCOMPARE(f(20).alpha, 0.0);
        QCOMPARE(f(100, true).alpha, 1.0); QCOMPARE(f(100, true).gray, 0.0);   // 정적은 페이드 없음
    }

    void framesFadeByAge()
    {
        TfTree t;
        t.ingest(msg({tf("odom", "base_link", 0, 0, 0, 0)}), false, 0);
        t.ingest(msg({tf("base_link", "laser", 0, 0, 0, 0)}), true, 0);
        t.update(12500);
        QCOMPARE(frame(t, "base_link")["alpha"].toDouble(), 0.5);
        QCOMPARE(frame(t, "laser")["alpha"].toDouble(), 1.0);
        QCOMPARE(frame(t, "odom")["alpha"].toDouble(), 1.0);   // 뿌리 프레임
    }

    void subscribesWithReasonAndThrottle()
    {
        RosBridge b;
        TfTree t;
        t.setBridge(&b);
        QVERIFY(!b.subscribed("/tf"));   // 기본은 꺼짐
        t.setActive(true);
        QVERIFY(b.subscribed("/tf") && b.subscribed("/tf_static"));
        t.setActive(false);
        QVERIFY(!b.subscribed("/tf") && !b.subscribed("/tf_static"));
    }
};

QTEST_MAIN(TstSceneTfTree)
#include "tst_scene_tftree.moc"
