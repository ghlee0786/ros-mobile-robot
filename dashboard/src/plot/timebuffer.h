#pragma once

#include <QList>
#include <QPair>
#include <QPointF>
#include <deque>
#include <limits>
#include <optional>

// 시간 기준 링버퍼. 점(x=시각 s, y=값)은 시각 순으로 유지하고, 마지막 점보다 maxSpanSec 넘게
// 오래된 앞쪽 점은 버린다(∞ 이면 버리지 않는다).
class TimeBuffer
{
public:
    struct Stats {
        int count = 0;
        double min = 0, max = 0, p2p = 0, mean = 0, rms = 0, std = 0, meanAbs = 0, meanInterval = 0;
    };

    explicit TimeBuffer(double maxSpanSec = 120) : m_maxSpan(maxSpanSec) {}

    void setMaxSpan(double sec) { m_maxSpan = sec; trim(); }
    double maxSpan() const { return m_maxSpan; }

    void add(double t, double v);
    void clear() { m_d.clear(); }
    int size() const { return int(m_d.size()); }
    bool isEmpty() const { return m_d.empty(); }
    const std::deque<QPointF> &points() const { return m_d; }
    QPointF first() const { return m_d.front(); }
    QPointF last() const { return m_d.back(); }

    QList<QPointF> window(double t0, double t1) const;
    std::optional<QPointF> valueAt(double t) const;
    // M4: pixels/2 개 열마다 첫 · min · max · 끝 점만 남긴다(결과 ≤ pixels×2 점).
    // steps 이면 각 점 앞에 (t_i, y_{i-1}) 를 끼워 계단으로 만든다(점 수는 최대 두 배).
    QList<QPointF> decimate(double t0, double t1, int pixels, bool steps = false) const;
    // 보이는 점의 min/max 에 2.5% 여백. 없으면 [-1, 1], 한 값뿐이면 ±1.
    QPair<double, double> yRange(double t0, double t1) const;
    Stats stats(double t0, double t1) const;

    static QList<QPointF> toSteps(const QList<QPointF> &pts);

private:
    using It = std::deque<QPointF>::const_iterator;
    QPair<It, It> range(double t0, double t1) const;
    void trim();

    std::deque<QPointF> m_d;
    double m_maxSpan;
};
