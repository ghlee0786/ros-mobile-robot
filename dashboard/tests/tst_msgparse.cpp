// msgparse 순수 함수: yaw · 쿼터니언, OccupancyGrid 이미지(방향 · 팔레트), map↔odom, 공분산, LaserScan
#include "msgparse.h"

#include <QtMath>
#include <QtTest>

using namespace msgparse;

namespace {
QColor px(const QImage &img, int x, int y) { return img.pixelColor(x, y); }
QJsonArray filled(int n, int v)
{
    QJsonArray a;
    for (int i = 0; i < n; ++i)
        a.append(v);
    return a;
}
} // namespace

class TstMsgParse : public QObject
{
    Q_OBJECT
private slots:
    void yawQuatRoundTrip()
    {
        for (double yaw : {0.0, 0.3, -1.2, M_PI / 2, 3.0, -3.0}) {
            const QJsonObject q = quatFromYaw(yaw);
            QVERIFY(qAbs(yawOf(q) - yaw) < 1e-9);
            QCOMPARE(q["z"].toDouble(), std::sin(yaw / 2));
            QCOMPARE(q["w"].toDouble(), std::cos(yaw / 2));
        }
    }

    // origin (0,0), 해상도 0.05, (+1 m, 0) 셀만 점유 → 이미지 맨 아래 줄(0 행), 왼쪽에서 20 번째 픽셀
    void occupancyDirection()
    {
        const int w = 40, h = 40;
        QJsonArray data = filled(w * h, 0);
        data[0 * w + 20] = 100;   // 행 0 = y 0, 열 20 = x 1.0 m
        const QImage img = occupancyToImage(data, w, h);
        QCOMPARE(img.size(), QSize(w, h));
        QCOMPARE(px(img, 20, h - 1), QColor(0, 0, 0));
        QCOMPARE(px(img, 20, 0), QColor(255, 255, 255));
        QCOMPARE(px(img, 19, h - 1), QColor(255, 255, 255));
    }

    void mapPalette()
    {
        const QJsonArray data{0, 100, 50, -1, 110, -56};   // -56 = 200 (0..255 로 보면)
        const QImage img = occupancyToImage(data, 6, 1, "map");
        QCOMPARE(px(img, 0, 0), QColor(255, 255, 255));
        QCOMPARE(px(img, 1, 0), QColor(0, 0, 0));
        QCOMPARE(px(img, 2, 0), QColor(128, 128, 128));
        QCOMPARE(px(img, 3, 0), QColor(0x70, 0x89, 0x86));
        QCOMPARE(px(img, 4, 0), QColor(0, 255, 0));
        QCOMPARE(px(img, 5, 0).red(), 255);
        QCOMPARE(px(img, 5, 0).blue(), 0);
    }

    void costmapPalette()
    {
        const QJsonArray data{0, 50, 99, 100, -1};
        const QImage img = occupancyToImage(data, 5, 1, "costmap");
        QCOMPARE(px(img, 0, 0).alpha(), 0);
        QCOMPARE(px(img, 1, 0), QColor(127, 0, 128));
        QCOMPARE(px(img, 2, 0), QColor(0, 255, 255));
        QCOMPARE(px(img, 3, 0), QColor(255, 0, 255));
        QCOMPARE(px(img, 4, 0), QColor(0x70, 0x89, 0x86));
    }

    void binaryAndRaw()
    {
        const QJsonArray data{10, 70, -1};
        const QImage bin = occupancyToImage(data, 3, 1, "map", 65);
        QCOMPARE(px(bin, 0, 0), QColor(255, 255, 255));
        QCOMPARE(px(bin, 1, 0), QColor(0, 0, 0));
        QCOMPARE(px(bin, 2, 0), QColor(0x70, 0x89, 0x86));
        const QImage raw = occupancyToImage(data, 3, 1, "raw");
        QCOMPARE(px(raw, 1, 0), QColor(70, 70, 70));
        QCOMPARE(px(raw, 2, 0), QColor(255, 255, 255));
    }

    void shortDataIgnored()
    {
        QVERIFY(occupancyToImage(filled(10, 0), 4, 4).isNull());
        QVERIFY(occupancyToImage({}, 0, 0).isNull());
    }

    void mapOdomRoundTrip()
    {
        const Pose2D mo{1.5, -2.0, 0.7};
        for (const Pose2D p : {Pose2D{0, 0, 0}, Pose2D{3, 4, -1}, Pose2D{-2.5, 0.1, 2.5}}) {
            const Pose2D back = odomToMap(mo, mapToOdom(mo, p));
            QVERIFY(qAbs(back.x - p.x) < 1e-6);
            QVERIFY(qAbs(back.y - p.y) < 1e-6);
            QVERIFY(qAbs(back.yaw - p.yaw) < 1e-6);
        }
        const Pose2D q = odomToMap({1, 2, M_PI / 2}, {1, 0, 0});   // odom (1,0) → map (1, 3)
        QVERIFY(qAbs(q.x - 1) < 1e-9 && qAbs(q.y - 3) < 1e-9);
    }

    void covariance()
    {
        const QJsonArray c = initialPoseCovariance();
        QCOMPARE(c.size(), 36);
        QCOMPARE(c[0].toDouble(), 0.25);
        QCOMPARE(c[7].toDouble(), 0.25);
        QVERIFY(qAbs(c[35].toDouble() - 0.0685) < 1e-4);
        double rest = 0;
        for (int i = 0; i < 36; ++i)
            if (i != 0 && i != 7 && i != 35)
                rest += qAbs(c[i].toDouble());
        QCOMPARE(rest, 0.0);
    }

    void scanWithMount()
    {
        const QJsonObject m{{"angle_min", 0.0}, {"angle_increment", M_PI / 2}, {"range_min", 0.1}, {"range_max", 10.0},
                            {"ranges", QJsonArray{1.0, 2.0, QJsonValue(), 0.2}}};
        const Scan s = parseScan(m, {0.1, 0, M_PI}, 0.3);   // 뒤로 돌려 단 레이저, 0.3 m 안은 숨김
        QCOMPARE(s.points.size(), 2);
        QVERIFY(qAbs(s.points[0].x() - (0.1 - 1.0)) < 1e-5 && qAbs(s.points[0].y()) < 1e-5);
        QVERIFY(qAbs(s.points[1].x() - 0.1) < 1e-5 && qAbs(s.points[1].y() + 2.0) < 1e-5);
        QCOMPARE(s.nearestRange, 1.0);
    }
};

QTEST_GUILESS_MAIN(TstMsgParse)
#include "tst_msgparse.moc"
