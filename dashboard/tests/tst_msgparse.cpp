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
        QVERIFY(qAbs(s.nearestRange - 0.9) < 1e-9);   // 거리도 base 기준: 점 (-0.9, 0)
    }

    // /scan_compact: label 파싱 · mm→m · 0 은 무효(null)
    void compactLabel()
    {
        const QJsonObject c{{"layout", QJsonObject{{"dim", QJsonArray{QJsonObject{
                                {"label", "laser -3.1240000 0.003483000 0.150 25.000 1700000000 123456789"},
                                {"size", 3}, {"stride", 3}}}}}},
                            {"data", QJsonArray{1500, 0, 65000}}};
        const QJsonObject s = scanFromCompact(c);
        QCOMPARE(s["header"].toObject()["frame_id"].toString(), QString("laser"));
        QCOMPARE(s["header"].toObject()["stamp"].toObject()["sec"].toInteger(), 1700000000);
        QCOMPARE(s["header"].toObject()["stamp"].toObject()["nanosec"].toInteger(), 123456789);
        QCOMPARE(s["angle_min"].toDouble(), -3.124);
        QCOMPARE(s["angle_increment"].toDouble(), 0.003483);
        QCOMPARE(s["range_min"].toDouble(), 0.15);
        QCOMPARE(s["range_max"].toDouble(), 25.0);
        const QJsonArray r = s["ranges"].toArray();
        QCOMPARE(r.size(), 3);
        QCOMPARE(r[0].toDouble(), 1.5);
        QVERIFY(r[1].isNull());   // 0 mm = 반사 없음
        QCOMPARE(r[2].toDouble(), 65.0);
        // range_min 이 0 이어도 0 mm 는 점이 되지 않는다
        QJsonObject z = c;
        z["layout"] = QJsonObject{{"dim", QJsonArray{QJsonObject{{"label", "base_link 0 0.1 0 100 0 0"}}}}};
        QCOMPARE(parseScan(scanFromCompact(z)).points.size(), 2);
        // frame_id 가 비어도(앞 공백) 숫자 칸을 읽는다 · label 이 모자라면 빈 객체
        z["layout"] = QJsonObject{{"dim", QJsonArray{QJsonObject{{"label", " 0.5 0.1 0.2 10 1 2"}}}}};
        QCOMPARE(scanFromCompact(z)["angle_min"].toDouble(), 0.5);
        QCOMPARE(scanFromCompact(z)["header"].toObject()["frame_id"].toString(), QString());
        z["layout"] = QJsonObject{{"dim", QJsonArray{QJsonObject{{"label", "laser 0 0.1"}}}}};
        QVERIFY(scanFromCompact(z).isEmpty());
        QVERIFY(scanFromCompact({}).isEmpty());
    }

    // 같은 스캔을 /scan 과 /scan_compact(파이 scan_compact_node 와 같은 식)로 받으면 같은 점이 나온다
    void compactSameAsScan()
    {
        const int n = 360;
        const double a0 = -M_PI, da = 2 * M_PI / n, rmin = 0.15, rmax = 12.0;
        QJsonArray ranges, mm;
        for (int i = 0; i < n; ++i) {
            const double r = i % 17 == 0 ? qInf() : i % 23 == 0 ? 0.1 : 0.25 + 0.01 * (i % 200);
            ranges.append(std::isfinite(r) ? QJsonValue(r) : QJsonValue());   // rosbridge 는 inf 를 null 로
            mm.append(std::isfinite(r) && rmin <= r && r <= rmax ? int(r * 1000) : 0);
        }
        const QJsonObject scan{{"header", QJsonObject{{"frame_id", "laser"}}}, {"angle_min", a0}, {"angle_increment", da},
                               {"range_min", rmin}, {"range_max", rmax}, {"ranges", ranges}};
        const QJsonObject compact{
            {"layout", QJsonObject{{"dim", QJsonArray{QJsonObject{
                {"label", QString("laser %1 %2 %3 %4 0 0").arg(a0, 0, 'f', 7).arg(da, 0, 'f', 9).arg(rmin, 0, 'f', 3).arg(rmax, 0, 'f', 3)},
                {"size", n}, {"stride", n}}}}}},
            {"data", mm}};
        const Pose2D mount{-0.12, 0, 0.2};
        const Scan a = parseScan(scan, mount, 0.3), b = parseScan(scanFromCompact(compact), mount, 0.3);
        QVERIFY(a.points.size() > 100);
        QCOMPARE(b.points.size(), a.points.size());
        for (int i = 0; i < a.points.size(); ++i)
            QVERIFY2((a.points[i] - b.points[i]).length() < 0.002, qPrintable(QString::number(i)));
        QVERIFY(qAbs(a.nearestRange - b.nearestRange) < 0.001);
        QVERIFY(qAbs(a.nearestAngle - b.nearestAngle) < 0.1);
    }
};

QTEST_GUILESS_MAIN(TstMsgParse)
#include "tst_msgparse.moc"
