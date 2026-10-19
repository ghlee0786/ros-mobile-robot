// LiDAR 잔상: 만료 · 마지막 묶음 유지 · 점 상한 · 받은 시점 자세로 고정 · 무지개 색.
#include "scene/scanaccumulator.h"
#include "scene/colorpointinstancing.h"
#include "scene/scenemath.h"

#include <QJsonArray>
#include <QtTest>

static QJsonObject scan(int n, double r = 1.0, double a0 = 0, double da = 0.01)
{
    QJsonArray ranges, inten;
    for (int i = 0; i < n; ++i)
        ranges.append(r + i * 0.001), inten.append(i);
    return {{"angle_min", a0}, {"angle_increment", da}, {"range_min", 0.05}, {"range_max", 12.0},
            {"ranges", ranges}, {"intensities", inten}};
}

class TstSceneScanAcc : public QObject
{
    Q_OBJECT
private slots:
    void decayZeroKeepsOnlyLast()
    {
        ScanAccumulator a;
        a.addScan(scan(10), 0);
        a.addScan(scan(7), 100);
        QCOMPARE(a.pointCount(), 7);
    }

    void expiresButKeepsLastBatch()
    {
        ScanAccumulator a;
        a.setDecaySec(1);
        a.addScan(scan(10), 0);
        a.addScan(scan(5), 500);
        QCOMPARE(a.pointCount(), 15);
        a.prune(1200);   // 첫 묶음 만료
        QCOMPARE(a.pointCount(), 5);
        a.prune(100000);   // 마지막 묶음은 남는다
        QCOMPARE(a.pointCount(), 5);
        QCOMPARE(a.colors().first().alphaF(), 1.0f);
    }

    void olderBatchFades()
    {
        ScanAccumulator a;
        a.setDecaySec(10);
        a.addScan(scan(1), 0);
        a.addScan(scan(1), 5000);
        QVERIFY(qAbs(a.colors()[0].alphaF() - 0.5f) < 0.01f);
        QCOMPARE(a.colors()[1].alphaF(), 1.0f);
    }

    void pointLimit()
    {
        ScanAccumulator a;
        a.setDecaySec(10);
        for (int i = 0; i < 10; ++i)
            a.addScan(scan(3000), i * 10);
        QVERIFY(a.pointCount() <= ScanAccumulator::kMaxPoints);
        QCOMPARE(a.pointCount(), 18000);
        QJsonObject big = scan(30000, 1.0, 0, 0.0001);
        big["range_max"] = 100.0;
        a.addScan(big, 200);   // 한 묶음이 상한을 넘으면 잘린다
        QCOMPARE(a.pointCount(), ScanAccumulator::kMaxPoints);
    }

    void fixedAtReceivePose()
    {
        ScanAccumulator a;
        a.setDecaySec(10);
        a.setProperty("robotX", 1.0);
        a.setProperty("robotY", 2.0);
        a.setProperty("robotTheta", M_PI / 2);
        QJsonObject s = scan(1, 1.0);   // 레이저 앞 1 m
        a.addScan(s, 0);
        a.setProperty("robotX", 5.0);   // 나중에 로봇이 움직여도 점은 그대로
        a.addScan(scan(0), 10);
        QCOMPARE(a.pointCount(), 1);
        const QVector3D p = a.points().first();
        QVERIFY(qAbs(p.x() - 1.0f) < 1e-5f);
        QVERIFY(qAbs(p.y() - 3.0f) < 1e-5f);
    }

    void selfHideAndInvalid()
    {
        ScanAccumulator a;
        QJsonObject s = scan(0);
        s["ranges"] = QJsonArray{0.1, QJsonValue(), 20.0, 2.0};
        a.addScan(s, 0);
        QCOMPARE(a.pointCount(), 1);
    }

    void rainbowValues()
    {
        // RViz getRainbowColor: 0 은 자홍(1,0,1), 1 은 빨강(1,0,0)
        auto rgb = [](double v) { const QColor c = scene::rainbow(v); return QVector3D(c.redF(), c.greenF(), c.blueF()); };
        QCOMPARE(rgb(0), QVector3D(1, 0, 1));
        QCOMPARE(rgb(0.2), QVector3D(0, 0, 1));
        QCOMPARE(rgb(0.4), QVector3D(0, 1, 1));
        QCOMPARE(rgb(0.6), QVector3D(0, 1, 0));
        QCOMPARE(rgb(0.8), QVector3D(1, 1, 0));
        QCOMPARE(rgb(1), QVector3D(1, 0, 0));
        QCOMPARE(rgb(-3), rgb(0));
        QCOMPARE(rgb(7), rgb(1));
    }

    void colorModes()
    {
        ScanAccumulator a;
        a.setColor(QColor(10, 20, 30));
        a.addScan(scan(3), 0);
        QCOMPARE(a.colors()[1], QColor(10, 20, 30));
        a.setColorMode("range");   // 묶음 안 최소 → 0, 최대 → 1
        QCOMPARE(a.colors().first(), scene::rainbow(0));
        QCOMPARE(a.colors().last(), scene::rainbow(1));
        a.setColorMode("intensity");
        QCOMPARE(a.colors().last(), scene::rainbow(1));
        QJsonObject s = scan(2);
        s.remove("intensities");   // intensity 가 없으면 단색
        a.addScan(s, 10);
        QCOMPARE(a.colors().first(), QColor(10, 20, 30));
    }

    void instancingFollowsSource()
    {
        ScanAccumulator a;
        a.addScan(scan(4), 0);
        ColorPointInstancing inst;
        inst.setSource(&a);
        int n = 0;
        QCOMPARE(inst.instanceBuffer(&n).size(), qsizetype(4 * sizeof(QQuick3DInstancing::InstanceTableEntry)));
        QCOMPARE(n, 4);
        QVERIFY(inst.hasTransparency());
    }

    void laserMountFromTfStatic()
    {
        // base_link → laser 가 yaw 180° 면 레이저 앞 1 m 는 로봇 뒤 1 m
        ScanAccumulator a;
        const QJsonObject tr{{"header", QJsonObject{{"frame_id", "base_link"}}}, {"child_frame_id", "laser"},
                             {"transform", QJsonObject{{"translation", QJsonObject{{"x", 0.1}, {"y", 0}, {"z", 0}}},
                                                       {"rotation", QJsonObject{{"z", 1}, {"w", 0}}}}}};
        a.feed("/tf_static", {{"transforms", QJsonArray{tr}}});
        QJsonObject s = scan(1, 1.0);
        s["header"] = QJsonObject{{"frame_id", "laser"}};
        a.addScan(s, 0);
        QVERIFY(qAbs(a.points().first().x() + 0.9f) < 1e-5f);
        s["header"] = QJsonObject{{"frame_id", "base_link"}};   // base_link 이면 그대로
        a.addScan(s, 10);
        QVERIFY(qAbs(a.points().first().x() - 1.0f) < 1e-5f);
    }

    void gradientPointsWithoutSource()
    {
        // source 가 없으면 points 를 앞(오래된 쪽)일수록 흐리게
        ColorPointInstancing inst;
        inst.setColor(QColor(0, 255, 0));
        inst.setPoints({QVector3D(0, 0, 0), QVector3D(1, 0, 0)});
        int n = 0;
        const QByteArray buf = inst.instanceBuffer(&n);
        QCOMPARE(n, 2);
        const auto *e = reinterpret_cast<const QQuick3DInstancing::InstanceTableEntry *>(buf.constData());
        QVERIFY(e[0].color.w() < e[1].color.w());
        QCOMPARE(e[1].color.w(), 1.0f);
    }
};

QTEST_MAIN(TstSceneScanAcc)
#include "tst_scene_scanacc.moc"
