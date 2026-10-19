#include "timebuffer.h"

#include <algorithm>
#include <cmath>

namespace {
bool lessX(const QPointF &a, double t) { return a.x() < t; }
bool lessT(double t, const QPointF &a) { return t < a.x(); }
} // namespace

void TimeBuffer::add(double t, double v)
{
    if (!std::isfinite(t) || !std::isfinite(v))
        return;
    if (m_d.empty() || t >= m_d.back().x())
        m_d.emplace_back(t, v);
    else
        m_d.insert(std::upper_bound(m_d.begin(), m_d.end(), t, lessT), QPointF(t, v));
    trim();
}

void TimeBuffer::trim()
{
    if (!std::isfinite(m_maxSpan))
        return;
    while (!m_d.empty() && m_d.back().x() - m_d.front().x() > m_maxSpan)
        m_d.pop_front();
}

QPair<TimeBuffer::It, TimeBuffer::It> TimeBuffer::range(double t0, double t1) const
{
    const It a = std::lower_bound(m_d.begin(), m_d.end(), t0, lessX);
    const It b = std::upper_bound(a, m_d.end(), t1, lessT);
    return {a, b};
}

QList<QPointF> TimeBuffer::window(double t0, double t1) const
{
    const auto [a, b] = range(t0, t1);
    return QList<QPointF>(a, b);
}

std::optional<QPointF> TimeBuffer::valueAt(double t) const
{
    if (m_d.empty())
        return std::nullopt;
    const It it = std::lower_bound(m_d.begin(), m_d.end(), t, lessX);
    if (it == m_d.end())
        return m_d.back();
    if (it == m_d.begin())
        return *it;
    const It prev = it - 1;
    return (t - prev->x() <= it->x() - t) ? *prev : *it;
}

QList<QPointF> TimeBuffer::toSteps(const QList<QPointF> &pts)
{
    QList<QPointF> out;
    out.reserve(pts.size() * 2);
    for (qsizetype i = 0; i < pts.size(); ++i) {
        if (i > 0)
            out << QPointF(pts[i].x(), pts[i - 1].y());
        out << pts[i];
    }
    return out;
}

QList<QPointF> TimeBuffer::decimate(double t0, double t1, int pixels, bool steps) const
{
    const auto [a, b] = range(t0, t1);
    const qsizetype n = b - a;
    QList<QPointF> out;
    if (n <= qsizetype(pixels) * 2 || t1 <= t0) {
        out = QList<QPointF>(a, b);
    } else {
        const int cols = std::max(1, pixels / 2);
        const double scale = cols / (t1 - t0);
        out.reserve(cols * 4);
        It it = a;
        while (it != b) {
            const int col = std::min(cols - 1, int((it->x() - t0) * scale));
            It first = it, lo = it, hi = it, last = it;
            for (; it != b && std::min(cols - 1, int((it->x() - t0) * scale)) == col; ++it) {
                if (it->y() < lo->y()) lo = it;
                if (it->y() > hi->y()) hi = it;
                last = it;
            }
            // 시각 순으로 겹치지 않게 넣는다
            It pick[4] = {first, lo, hi, last};
            std::sort(pick, pick + 4, [](It x, It y) { return x < y; });
            for (int k = 0; k < 4; ++k)
                if (k == 0 || pick[k] != pick[k - 1])
                    out << *pick[k];
        }
    }
    return steps ? toSteps(out) : out;
}

QPair<double, double> TimeBuffer::yRange(double t0, double t1) const
{
    const auto [a, b] = range(t0, t1);
    if (a == b)
        return {-1, 1};
    const auto [lo, hi] = std::minmax_element(a, b, [](const QPointF &x, const QPointF &y) { return x.y() < y.y(); });
    const double span = hi->y() - lo->y();
    if (span <= 0)
        return {lo->y() - 1, hi->y() + 1};
    return {lo->y() - span * 0.025, hi->y() + span * 0.025};
}

TimeBuffer::Stats TimeBuffer::stats(double t0, double t1) const
{
    const auto [a, b] = range(t0, t1);
    Stats s;
    s.count = int(b - a);
    if (!s.count)
        return s;
    double sum = 0, sum2 = 0, sumAbs = 0;
    s.min = s.max = a->y();
    for (It it = a; it != b; ++it) {
        const double y = it->y();
        s.min = std::min(s.min, y);
        s.max = std::max(s.max, y);
        sum += y;
        sum2 += y * y;
        sumAbs += std::abs(y);
    }
    s.p2p = s.max - s.min;
    s.mean = sum / s.count;
    s.rms = std::sqrt(sum2 / s.count);
    s.std = std::sqrt(std::max(0.0, sum2 / s.count - s.mean * s.mean));
    s.meanAbs = sumAbs / s.count;
    s.meanInterval = s.count > 1 ? ((b - 1)->x() - a->x()) / (s.count - 1) : 0;
    return s;
}
