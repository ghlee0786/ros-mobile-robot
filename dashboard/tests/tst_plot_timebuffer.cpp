// 시간 기준 링버퍼: 창 자르기 · 끼워 넣기 · valueAt · M4 · yRange · stats · steps
#include "plot/timebuffer.h"

#include <QtTest>
#include <cmath>

static const double kPi = std::acos(-1.0);

class TstPlotTimeBuffer : public QObject
{
    Q_OBJECT
private slots:
    void spanTrim()
    {
        TimeBuffer b(10);
        for (int i = 0; i <= 30; ++i)
            b.add(i, i);
        QCOMPARE(b.first().x(), 20.0);
        QCOMPARE(b.last().x(), 30.0);
        QCOMPARE(b.size(), 11);
        TimeBuffer inf(qInf());
        for (int i = 0; i < 1000; ++i)
            inf.add(i * 10, i);
        QCOMPARE(inf.size(), 1000);
        TimeBuffer def;
        QCOMPARE(def.maxSpan(), 120.0);
        def.add(qQNaN(), 1);
        def.add(1, qInf());
        QVERIFY(def.isEmpty());
    }

    void window()
    {
        TimeBuffer b;
        for (int i = 0; i < 10; ++i)
            b.add(i, i * 2);
        const QList<QPointF> w = b.window(2.5, 6);
        QCOMPARE(w.size(), 4);
        QCOMPARE(w.first(), QPointF(3, 6));
        QCOMPARE(w.last(), QPointF(6, 12));
        QVERIFY(b.window(20, 30).isEmpty());
        QCOMPARE(b.window(-5, 100).size(), 10);
    }

    void outOfOrderInsert()
    {
        TimeBuffer b;
        b.add(1, 1);
        b.add(3, 3);
        b.add(2, 2);
        b.add(0, 0);
        b.add(3, 33);   // 같은 시각은 뒤에
        QCOMPARE(b.size(), 5);
        const auto &p = b.points();
        for (size_t i = 1; i < p.size(); ++i)
            QVERIFY(p[i - 1].x() <= p[i].x());
        QCOMPARE(p.back(), QPointF(3, 33));
        QCOMPARE(p.front(), QPointF(0, 0));
    }

    void valueAt()
    {
        TimeBuffer b;
        QVERIFY(!b.valueAt(1));
        for (int i = 0; i < 10; ++i)
            b.add(i, i * 10);
        QCOMPARE(*b.valueAt(3.4), QPointF(3, 30));
        QCOMPARE(*b.valueAt(3.6), QPointF(4, 40));
        QCOMPARE(*b.valueAt(-5), QPointF(0, 0));
        QCOMPARE(*b.valueAt(50), QPointF(9, 90));
        QCOMPARE(*b.valueAt(7), QPointF(7, 70));
    }

    void m4()
    {
        TimeBuffer b(qInf());
        const int n = 100000;
        for (int i = 0; i < n; ++i)
            b.add(i * 0.001, std::sin(i * 0.01) + (i == 54321 ? 10 : 0) - (i == 12345 ? 7 : 0));
        const int px = 300;
        const QList<QPointF> d = b.decimate(0, 100, px);
        QVERIFY(d.size() <= px * 2);
        QVERIFY(d.size() > px);
        // 극값 보존: 튀는 점 두 개가 남아야 한다
        double lo = qInf(), hi = -qInf();
        for (const QPointF &p : d) {
            lo = std::min(lo, p.y());
            hi = std::max(hi, p.y());
        }
        QCOMPARE(hi, b.points()[54321].y());
        QCOMPARE(lo, b.points()[12345].y());
        // 첫 · 끝 점과 시각 순서 유지
        QCOMPARE(d.first(), b.points().front());
        QCOMPARE(d.last(), b.points().back());
        for (int i = 1; i < d.size(); ++i)
            QVERIFY(d[i - 1].x() <= d[i].x());
        // 점이 적으면 그대로
        QCOMPARE(b.decimate(0, 0.1, px).size(), 101);
    }

    void yRange()
    {
        TimeBuffer b;
        QCOMPARE(b.yRange(0, 10), qMakePair(-1.0, 1.0));
        b.add(1, 0);
        b.add(2, 10);
        b.add(3, 100);   // 창 밖
        const auto r = b.yRange(0, 2.5);
        QCOMPARE(r.first, -0.25);
        QCOMPARE(r.second, 10.25);
        QCOMPARE(b.yRange(5, 6), qMakePair(-1.0, 1.0));
        QCOMPARE(b.yRange(2.5, 3.5), qMakePair(99.0, 101.0));
    }

    void stats()
    {
        TimeBuffer b;
        b.add(0, 1);
        b.add(0.5, 2);
        b.add(1.0, 3);
        b.add(1.5, -4);
        TimeBuffer::Stats s = b.stats(0, 10);
        QCOMPARE(s.count, 4);
        QCOMPARE(s.min, -4.0);
        QCOMPARE(s.max, 3.0);
        QCOMPARE(s.p2p, 7.0);
        QCOMPARE(s.mean, 0.5);
        QVERIFY(qAbs(s.rms - std::sqrt(30.0 / 4)) < 1e-12);
        QVERIFY(qAbs(s.std - std::sqrt(30.0 / 4 - 0.25)) < 1e-12);
        QCOMPARE(s.meanAbs, 2.5);
        QCOMPARE(s.meanInterval, 0.5);

        // 사인파 한 주기: 평균 0, rms = std = 1/√2, 평균 절댓값 2/π
        TimeBuffer w(qInf());
        const int n = 1000;
        for (int i = 0; i < n; ++i)
            w.add(double(i) / n, std::sin(2 * kPi * i / n));
        s = w.stats(0, 1);
        QCOMPARE(s.count, n);
        QVERIFY(qAbs(s.mean) < 1e-9);
        QVERIFY(qAbs(s.rms - std::sqrt(0.5)) < 1e-9);
        QVERIFY(qAbs(s.std - std::sqrt(0.5)) < 1e-9);
        QVERIFY(qAbs(s.meanAbs - 2 / kPi) < 1e-4);
        QVERIFY(qAbs(s.max - 1) < 1e-9);
        QVERIFY(qAbs(s.min + 1) < 1e-9);
        QVERIFY(qAbs(s.meanInterval - 1.0 / n) < 1e-12);
        QCOMPARE(w.stats(5, 6).count, 0);
    }

    void steps()
    {
        TimeBuffer b;
        b.add(0, 1);
        b.add(1, 5);
        b.add(2, 3);
        const QList<QPointF> s = b.decimate(0, 2, 100, true);
        const QList<QPointF> expect = {{0, 1}, {1, 1}, {1, 5}, {2, 5}, {2, 3}};
        QCOMPARE(s, expect);
    }
};

QTEST_GUILESS_MAIN(TstPlotTimeBuffer)
#include "tst_plot_timebuffer.moc"
