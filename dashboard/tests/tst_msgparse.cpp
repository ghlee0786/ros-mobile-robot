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

    // RViz2 Map 디스플레이(map 색 체계)의 makeMapPalette 규칙으로 계산한 색과 같다. 칸 바이트 경로(cellsToImage)도 같다.
    void mapPaletteMatchesRviz2()
    {
        auto rviz = [](int v) {
            const int i = v & 0xFF;
            if (i <= 100) {
                const int g = 255 - (255 * i) / 100;
                return QColor(g, g, g);
            }
            if (i <= 127)
                return QColor(0, 255, 0);
            if (i <= 254)
                return QColor(255, (255 * (i - 128)) / (254 - 128), 0);
            return QColor(0x70, 0x89, 0x86);
        };
        const QList<int> vals{0, 50, 100, -1, 101, 128};
        QJsonArray data;
        for (int v : vals)
            data.append(v);
        const QImage img = occupancyToImage(data, int(vals.size()), 1, "map");
        const QImage fromCells = cellsToImage(occupancyCells(data), int(vals.size()), 1, "map");
        for (int k = 0; k < vals.size(); ++k) {
            QCOMPARE(px(img, k, 0), rviz(vals[k]));
            QCOMPARE(px(fromCells, k, 0), rviz(vals[k]));
        }
    }

    // 셀 (i,j) 중심 = origin + (i+0.5, j+0.5)·res(origin yaw 적용)이 판 uv 에서 그 칸 픽셀의 한가운데에 온다.
    void cellCenterAlignment()
    {
        const int w = 5, h = 3;
        const QJsonObject info{{"width", w}, {"height", h}, {"resolution", 0.1},
                               {"origin", QJsonObject{{"position", QJsonObject{{"x", 1.5}, {"y", -0.7}}},
                                                      {"orientation", quatFromYaw(qDegreesToRadians(30.0))}}}};
        QJsonArray data = filled(w * h, 0);
        data[2 * w + 3] = 100;   // 칸 (3, 2)
        const QImage img = occupancyToImage(data, w, h);
        const GridPlane plane = gridPlane(info);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const Pose2D c = gridCellCenter(info, i, j);
                const QPointF uv = planeUv(plane, c.x, c.y);
                QVERIFY(qAbs(uv.x() * w - (i + 0.5)) < 1e-6);
                QVERIFY(qAbs(uv.y() * h - (h - 1 - j + 0.5)) < 1e-6);   // 이미지 0 행 = 지도 맨 위 행
                const QColor want = i == 3 && j == 2 ? QColor(0, 0, 0) : QColor(255, 255, 255);
                QCOMPARE(px(img, int(uv.x() * w), int(uv.y() * h)), want);
            }
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
