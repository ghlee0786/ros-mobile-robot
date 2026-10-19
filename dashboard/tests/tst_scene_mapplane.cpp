// OccupancyGrid → 3D 판(중심 · 크기 · 회전)과 텍스처 좌표: 셀 (i, j) 의 중심 = origin + (i+0.5, j+0.5)·res 가
// 판 위에서 이미지 열 i · 행 (h-1-j) 의 가운데에 오는지(지도 · 코스트맵 공통 계산).
#include "msgparse.h"

#include <QJsonArray>
#include <QtTest>
#include <cmath>

using namespace msgparse;

static QJsonObject info(int w, int h, double res, double ox, double oy, double yaw)
{
    return {{"width", w}, {"height", h}, {"resolution", res},
            {"origin", QJsonObject{{"position", QJsonObject{{"x", ox}, {"y", oy}, {"z", 0}}},
                                   {"orientation", quatFromYaw(yaw)}}}};
}

class TstSceneMapPlane : public QObject
{
    Q_OBJECT
private slots:
    void cells_data()
    {
        QTest::addColumn<int>("w");
        QTest::addColumn<int>("h");
        QTest::addColumn<double>("res");
        QTest::addColumn<double>("ox");
        QTest::addColumn<double>("oy");
        QTest::addColumn<double>("yawDeg");
        QTest::newRow("정사각형") << 4 << 4 << 0.5 << 0.0 << 0.0 << 0.0;
        QTest::newRow("모의 서버 기본 방") << 120 << 100 << 0.05 << -2.5 << -2.5 << 0.0;
        QTest::newRow("파이 slam_toolbox 직사각형") << 181 << 57 << 0.05 << -4.37 << -1.21 << 0.0;
        QTest::newRow("origin yaw 90°") << 6 << 3 << 0.1 << 1.0 << -2.0 << 90.0;
        QTest::newRow("홀수 크기 · yaw 30°") << 7 << 5 << 0.05 << -0.3 << 0.4 << 30.0;
    }

    void cells()
    {
        QFETCH(int, w);
        QFETCH(int, h);
        QFETCH(double, res);
        QFETCH(double, ox);
        QFETCH(double, oy);
        QFETCH(double, yawDeg);
        const double yaw = qDegreesToRadians(yawDeg);
        const QJsonObject in = info(w, h, res, ox, oy, yaw);
        const GridPlane p = gridPlane(in);
        QCOMPARE(p.width, w * res);
        QCOMPARE(p.height, h * res);
        QVERIFY(std::abs(p.center.yaw - yaw) < 1e-9);
        // 판 중심 = 가운데 셀 중심들의 평균 = 모서리 셀 (0,0) 과 (w-1,h-1) 중심의 가운데
        const Pose2D a = gridCellCenter(in, 0, 0), b = gridCellCenter(in, w - 1, h - 1);
        QVERIFY(std::abs(p.center.x - (a.x + b.x) / 2) < 1e-9);
        QVERIFY(std::abs(p.center.y - (a.y + b.y) / 2) < 1e-9);
        // 셀 (0,0) 중심은 origin 에서 (0.5, 0.5)·res (origin 회전 적용)
        QVERIFY(std::abs(a.x - (ox + res / 2 * (std::cos(yaw) - std::sin(yaw)))) < 1e-9);
        QVERIFY(std::abs(a.y - (oy + res / 2 * (std::sin(yaw) + std::cos(yaw)))) < 1e-9);

        // 셀 하나만 점유한 지도: 그 셀 중심의 uv 가 이미지의 그 점유 픽셀 가운데에 온다
        for (const QPoint cell : {QPoint(0, 0), QPoint(w - 1, 0), QPoint(0, h - 1), QPoint(w - 1, h - 1), QPoint(w / 2, h / 3)}) {
            QJsonArray data;
            for (int k = 0; k < w * h; ++k)
                data.append(k == cell.y() * w + cell.x() ? 100 : 0);
            const QImage img = occupancyToImage(data, w, h);
            const Pose2D c = gridCellCenter(in, cell.x(), cell.y());
            const QPointF uv = planeUv(p, c.x, c.y);
            const double px = uv.x() * w, py = uv.y() * h;
            QVERIFY2(std::abs(px - (cell.x() + 0.5)) < 1e-6 && std::abs(py - (h - 1 - cell.y() + 0.5)) < 1e-6,
                     qPrintable(QString("셀 (%1,%2) → 픽셀 (%3,%4)").arg(cell.x()).arg(cell.y()).arg(px).arg(py)));
            QCOMPARE(img.pixelColor(int(px), int(py)).lightness(), 0);   // 점유 = 검정
        }
        // 판 모서리 = uv 0 · 1 (지도 바깥 테두리와 판 가장자리가 같다)
        const Pose2D corner = transform2D({ox, oy, yaw}, {0, 0, 0});
        const QPointF uv0 = planeUv(p, corner.x, corner.y);
        QVERIFY(std::abs(uv0.x()) < 1e-9 && std::abs(uv0.y() - 1) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TstSceneMapPlane)
#include "tst_scene_mapplane.moc"
