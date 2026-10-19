// 코스트맵 팔레트(0 투명)와 map 프레임 → odom 변환, footprint 닫힌 선.
#include "scene/costmaplayer.h"

#include <QJsonArray>
#include <QtTest>
#include <cmath>

static QJsonObject mapOdom(double x, double y, double yaw)
{
    const QJsonObject tr{{"header", QJsonObject{{"frame_id", "map"}}}, {"child_frame_id", "odom"},
                         {"transform", QJsonObject{{"translation", QJsonObject{{"x", x}, {"y", y}, {"z", 0}}},
                                                   {"rotation", QJsonObject{{"z", std::sin(yaw / 2)},
                                                                            {"w", std::cos(yaw / 2)}}}}}};
    return {{"transforms", QJsonArray{tr}}};
}

static QJsonObject grid(const QString &frame, int w, int h, double res, const QJsonArray &data)
{
    return {{"header", QJsonObject{{"frame_id", frame}}},
            {"info", QJsonObject{{"width", w}, {"height", h}, {"resolution", res},
                                 {"origin", QJsonObject{{"position", QJsonObject{{"x", 0}, {"y", 0}, {"z", 0}}},
                                                        {"orientation", QJsonObject{{"w", 1}}}}}}},
            {"data", data}};
}

class TstSceneCostmap : public QObject
{
    Q_OBJECT
private slots:
    void palette()
    {
        QCOMPARE(CostmapLayer::costColor(0).alpha(), 0);   // 비용 0 은 투명
        QCOMPARE(CostmapLayer::costColor(100), QColor(255, 0, 255));
        QCOMPARE(CostmapLayer::costColor(99), QColor(0, 255, 255));
        QCOMPARE(CostmapLayer::costColor(-1).alpha(), 255);
        QCOMPARE(CostmapLayer::costColor(1).blue(), 253);
        QCOMPARE(CostmapLayer::costColor(98).red(), 249);
    }

    void imageRowsFlipped()
    {
        // 지도 0 행(아래)이 이미지 맨 아래 행
        const QImage img = CostmapLayer::toImage(2, 2, {100, 0, 0, 0});
        QCOMPARE(img.format(), QImage::Format_ARGB32);
        QCOMPARE(img.pixelColor(0, 1), QColor(255, 0, 255));
        QCOMPARE(img.pixelColor(0, 0).alpha(), 0);
    }

    void odomFrameUnchanged()
    {
        CostmapLayer c;
        c.setTopic("/local_costmap/costmap");
        c.feed("/local_costmap/costmap", grid("odom", 4, 2, 0.5, QJsonArray{0, 0, 0, 0, 0, 0, 0, 100}));
        QVERIFY(c.received());
        QCOMPARE(c.center(), QVector3D(1, 0.5f, CostmapLayer::kZ));
        QCOMPARE(c.size(), QSizeF(2, 1));
        QCOMPARE(c.yaw(), 0.0);
    }

    void mapFrameToOdom()
    {
        CostmapLayer c;   // 기본 토픽 /global_costmap/costmap
        c.feed("/tf", mapOdom(1, 0, M_PI / 2));
        c.feed("/global_costmap/costmap", grid("map", 2, 2, 1, QJsonArray{0, 0, 0, 0}));
        // map 중심 (1, 1) - (1, 0) = (0, 1) → -90° → odom (1, 0)
        QVERIFY(qAbs(c.center().x() - 1) < 1e-5f && qAbs(c.center().y()) < 1e-5f);
        QVERIFY(qAbs(c.yaw() + 90) < 1e-9);
    }

    void ignoresOtherTopicsAndBadData()
    {
        CostmapLayer c;
        c.feed("/local_costmap/costmap", grid("odom", 1, 1, 1, QJsonArray{0}));
        c.feed("/global_costmap/costmap", grid("odom", 2, 2, 1, QJsonArray{0}));   // 데이터 부족
        QVERIFY(!c.received());
    }

    void defaultOffAndSubscribeWhenActive()
    {
        RosBridge b;
        CostmapLayer c;
        c.setBridge(&b);
        QVERIFY(!b.subscribed("/global_costmap/costmap"));
        c.setActive(true);
        QVERIFY(b.subscribed("/global_costmap/costmap"));
        c.setTopic("/local_costmap/costmap");
        QVERIFY(!b.subscribed("/global_costmap/costmap") && b.subscribed("/local_costmap/costmap"));
        c.setActive(false);
        QVERIFY(!b.subscribed("/local_costmap/costmap") && !b.subscribed("/tf"));
    }

    void footprintClosed()
    {
        FootprintLayer f;
        const QJsonArray pts{QJsonObject{{"x", 0}, {"y", 0}}, QJsonObject{{"x", 0.2}, {"y", 0}},
                             QJsonObject{{"x", 0.2}, {"y", 0.2}}};
        f.feed("/local_costmap/published_footprint",
               {{"header", QJsonObject{{"frame_id", "odom"}}}, {"polygon", QJsonObject{{"points", pts}}}});
        QVERIFY(f.received());
        QVERIFY(f.points().size() > 10);
        QCOMPARE(f.points().first(), f.points().last());   // 처음 꼭짓점으로 닫힌다
    }
};

QTEST_MAIN(TstSceneCostmap)
#include "tst_scene_costmap.moc"
