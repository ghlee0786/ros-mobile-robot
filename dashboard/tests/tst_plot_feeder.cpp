// PlotSeries(push 경로) + PlotFeeder: LineSeries 에 replace 되는 점 수 · X 창 · Y 범위 · 트래커 · 통계 · CSV · 확대
#include "plot/plotfeeder.h"
#include "plot/plotseries.h"

#include <QTemporaryDir>
#include <QtGraphs/QLineSeries>
#include <QtTest>
#include <cmath>

class TstPlotFeeder : public QObject
{
    Q_OBJECT
private slots:
    void replaceDecimated()
    {
        PlotSeries s;
        for (int i = 0; i < 20000; ++i)
            s.push(i * 0.001, std::sin(i * 0.01));
        QCOMPARE(s.count(), 20000);
        QLineSeries line;
        PlotFeeder f;
        f.setPaused(true);
        f.setXMin(0);
        f.setXMax(20);
        f.setPixelWidth(400);
        f.addSeries(&s);
        f.addTarget(&line);
        f.refresh();
        QVERIFY(line.count() > 0);
        QVERIFY(line.count() <= 800);
        QCOMPARE(line.count(), f.visiblePoints(0).size());
        QCOMPARE(line.at(0), QPointF(0, 0));

        // 보이지 않으면 갱신하지 않는다
        f.setActive(false);
        f.setXMin(4.9995);
        f.setXMax(5.1005);
        f.refresh();
        QVERIFY(line.count() > 300);
        f.setActive(true);   // 다시 보이면 바로 갱신
        QCOMPARE(line.count(), 101);

        // 꺼진 선은 비운다
        s.setEnabled(false);
        f.refresh();
        QCOMPARE(line.count(), 0);
    }

    void liveWindow()
    {
        PlotSeries s;
        const double now = PlotSeries::now();
        for (int i = 0; i < 300; ++i)
            s.push(now - 30 + i * 0.1, i);
        PlotFeeder f;
        f.addSeries(&s);
        f.setWindowSec(10);
        f.refresh();
        QVERIFY(qAbs(f.xMax() - PlotSeries::now()) < 1.0);
        QVERIFY(qAbs(f.xMax() - f.xMin() - 10) < 1e-9);
        const QList<QPointF> v = f.visiblePoints(0);
        QVERIFY(v.size() >= 99 && v.size() <= 101);
        f.setWindowSec(0);   // 전체
        f.refresh();
        QCOMPARE(f.xMin(), now - 30);
        QCOMPARE(f.visiblePoints(0).size(), 300);
    }

    void yRange()
    {
        PlotSeries s;
        s.push(0, 0);
        s.push(1, 10);
        PlotFeeder f;
        f.setPaused(true);
        f.setXMin(0);
        f.setXMax(2);
        f.addSeries(&s);
        f.refresh();
        QCOMPARE(f.yMinOut(), -0.25);
        QCOMPARE(f.yMaxOut(), 10.25);
        // 넓어지면 바로
        s.push(1.5, 100);
        f.refresh();
        QCOMPARE(f.yMaxOut(), 102.5);
        // 좁아지면 천천히
        f.setXMax(1.2);
        QTest::qWait(50);
        f.refresh();
        QVERIFY(f.yMaxOut() < 102.5);
        QVERIFY(f.yMaxOut() > 10.25);
        // 한쪽만 고정
        f.setYMin(-5);
        f.refresh();
        QCOMPARE(f.yMinOut(), -5.0);
        f.zoomOut(false, true);
        QVERIFY(qIsNaN(f.yMin()));
        // 데이터 없음
        PlotFeeder e;
        e.refresh();
        QCOMPARE(e.yMinOut(), -1.0);
        QCOMPARE(e.yMaxOut(), 1.0);
    }

    void trackerStatsCsv()
    {
        PlotSeries a, b;
        a.setName("a");
        b.setName("b");
        b.setTransform("scale(2,0)");
        QVERIFY(b.error().isEmpty());
        for (int i = 0; i <= 100; ++i) {
            a.push(i * 0.01, i);
            b.push(i * 0.01, i);
        }
        QCOMPARE(b.lastValue(), 200.0);
        PlotFeeder f;
        f.addSeries(&a);
        f.addSeries(&b);
        f.zoomTo(0, 1, -1, 300);
        QVERIFY(f.paused());
        f.refresh();
        QCOMPARE(f.yMinOut(), -1.0);
        QCOMPARE(f.yMaxOut(), 300.0);

        QVariantList v = f.valuesAt(0.5);
        QCOMPARE(v.size(), 2);
        QCOMPARE(v[1].toMap()["value"].toDouble(), 100.0);
        QVERIFY(!v[0].toMap().contains("delta"));
        f.setReference(0.2);
        v = f.valuesAt(0.5);
        QCOMPARE(v[0].toMap()["delta"].toDouble(), 30.0);
        QCOMPARE(v[1].toMap()["delta"].toDouble(), 60.0);
        f.clearReference();
        QVERIFY(!f.hasReference());

        const QVariantList st = f.statsInView();
        QCOMPARE(st[0].toMap()["count"].toInt(), 101);
        QCOMPARE(st[0].toMap()["mean"].toDouble(), 50.0);
        QVERIFY(qAbs(st[0].toMap()["hz"].toDouble() - 100) < 1e-6);

        QTemporaryDir dir;
        const QString path = dir.filePath("plot.csv");
        f.pan(0.5, 0);
        QCOMPARE(f.xMin(), 0.5);
        QVERIFY(f.exportCsv(path, true));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.first(), QString("x,receive time,header.stamp,topic,path,value"));
        QCOMPARE(lines.size(), 1 + 2 * 51);
        QVERIFY(f.exportCsv(path, false));
        file.close();
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts).size(), 1 + 2 * 101);

        f.zoomOut(true, false);
        QVERIFY(!f.paused());
    }

    void seriesPathAndTransform()
    {
        PlotSeries s;
        s.setPath("/odom.pose[");
        QVERIFY(!s.error().isEmpty());
        s.setPath("/wheel_states.velocity[name==\"right_wheel_joint\"]");
        QVERIFY(s.error().isEmpty());
        QCOMPARE(s.topic(), QString("/wheel_states"));
        QCOMPARE(s.throttleMs(), -1);
        s.setTransform("bogus");
        QVERIFY(!s.error().isEmpty());
        // 변환을 바꾸면 쌓인 원 데이터로 다시 계산한다
        s.setTransform("");
        for (int i = 0; i < 10; ++i)
            s.push(i, i * i);
        s.setTransform("d/dt");
        QVERIFY(s.error().isEmpty());
        QCOMPARE(s.count(), 9);
        QCOMPARE(s.lastValue(), 17.0);
        // @derivative 모디파이어 + 변환은 이어서 적용
        s.setPath("/t.x.@derivative");
        s.setTransform("abs");
        s.push(0, 0);
        s.push(1, -3);
        QCOMPARE(s.lastValue(), 3.0);

        // 토픽 목록이 없으면 구독하지 않는다(연결 없는 bridge)
        RosBridge bridge;
        s.setBridge(&bridge);
        QVERIFY(!bridge.subscribed("/t"));
    }
};

QTEST_MAIN(TstPlotFeeder)
#include "tst_plot_feeder.moc"
