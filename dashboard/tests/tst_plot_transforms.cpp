// SISO 증분 변환: 미분 · 이동평균 · 이동 RMS · 적분 · 스케일 · abs · dt · 이상치 · 팩토리
#include "plot/transforms.h"

#include <QtTest>
#include <cmath>

static const double kPi = std::acos(-1.0);

static QList<QPointF> run(Transform &t, const QList<QPointF> &in)
{
    QList<QPointF> out;
    for (const QPointF &p : in)
        if (auto r = t.push(p))
            out << *r;
    return out;
}

static QList<QPointF> sine(double dt, double t1)
{
    QList<QPointF> pts;
    for (double t = 0; t <= t1; t += dt)
        pts << QPointF(t, std::sin(t));
    return pts;
}

class TstPlotTransforms : public QObject
{
    Q_OBJECT
private slots:
    void derivative()
    {
        auto d = makeTransform("d/dt");
        QVERIFY(d);
        const QList<QPointF> out = run(*d, sine(0.01, 10));
        QVERIFY(out.size() > 900);
        for (const QPointF &p : out)
            QVERIFY2(qAbs(p.y() - std::cos(p.x())) < 1e-2, qPrintable(QString::number(p.x())));
        // 고정 dt
        auto f = makeTransform("d/dt(0.5)");
        const QList<QPointF> o2 = run(*f, {{0, 0}, {1, 1}, {3, 2}});
        QCOMPARE(o2.size(), 2);
        QCOMPARE(o2[0].y(), 2.0);
        QCOMPARE(o2[1].y(), 2.0);
        // 같은 시각 · 뒤로 간 점은 건너뛴다
        DerivativeTransform r;
        QVERIFY(!r.push({1, 1}));
        QVERIFY(!r.push({1, 5}));
        QVERIFY(!r.push({0.5, 5}));
        QCOMPARE(r.push({2, 3})->y(), 2.0);
        r.reset();
        QVERIFY(!r.push({0, 0}));
    }

    void movingAverage()
    {
        auto a = makeTransform("avg(5)");
        QList<QPointF> in;
        for (int i = 1; i <= 10; ++i)
            in << QPointF(i, i);
        QList<QPointF> out = run(*a, in);
        QCOMPARE(out.size(), 10);
        QCOMPARE(out[0], QPointF(1, 1));
        QCOMPARE(out[1], QPointF(2, 1.5));
        QCOMPARE(out[9], QPointF(10, 8));
        auto c = makeTransform("avg(5,center)");
        out = run(*c, in);
        QCOMPARE(out[9], QPointF(8, 8));
        // 사인 한 주기의 이동 RMS = 1/√2
        auto r = makeTransform("rms(1000)");
        QList<QPointF> s;
        for (int i = 0; i < 2000; ++i)
            s << QPointF(i * 0.001, std::sin(2 * kPi * i / 1000));
        out = run(*r, s);
        QVERIFY(qAbs(out.last().y() - std::sqrt(0.5)) < 1e-6);
    }

    void integral()
    {
        auto t = makeTransform("int");
        QList<QPointF> out = run(*t, {{0, 2}, {1, 2}, {3, 2}});
        QCOMPARE(out.last(), QPointF(3, 6));
        QCOMPARE(out.first(), QPointF(0, 0));
        t->reset();
        out.clear();
        for (const QPointF &p : sine(0.001, kPi / 2))
            out << QPointF(p.x(), std::cos(p.x()));
        out = run(*t, out);
        QVERIFY(qAbs(out.last().y() - std::sin(out.last().x())) < 1e-6);
    }

    void scaleAbsDt()
    {
        auto s = makeTransform("scale(100,1,2)");
        QCOMPARE(*s->push({1, 0.5}), QPointF(3, 51));
        auto s2 = makeTransform("scale(100,0)");
        QCOMPARE(*s2->push({1, 0.5}), QPointF(1, 50));
        auto a = makeTransform("abs");
        QCOMPARE(*a->push({1, -3}), QPointF(1, 3));
        auto d = makeTransform("dt");
        QVERIFY(!d->push({1, 0}));
        QCOMPARE(*d->push({1.25, 0}), QPointF(1.25, 0.25));
    }

    void outlier()
    {
        auto o = makeTransform("outlier(10)");
        const QList<QPointF> in = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 100}, {5, 5}, {6, 6}, {7, 7}};
        const QList<QPointF> out = run(*o, in);
        for (const QPointF &p : out)
            QVERIFY(p.y() < 50);
        QCOMPARE(out.size(), in.size() - 2); // 튄 점 하나 + 마지막(한 점 지연)
        QCOMPARE(out.last(), QPointF(6, 6));
    }

    void factory()
    {
        QString err;
        QVERIFY(!makeTransform("nope(3)", &err));
        QVERIFY(!err.isEmpty());
        for (const char *n : {"d/dt", "derivative", "avg(5)", "rms(3)", "int", "integral", "scale(2,0)", "abs", "dt", "outlier(100)"})
            QVERIFY2(makeTransform(n), n);
    }
};

QTEST_GUILESS_MAIN(TstPlotTransforms)
#include "tst_plot_transforms.moc"
