#include "plotfeeder.h"

#include <QFile>
#include <QTextStream>
#include <QtGraphs/QXYSeries>
#include <algorithm>
#include <cmath>

PlotFeeder::PlotFeeder(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &PlotFeeder::refresh);
    m_timer.start();
}

QQmlListProperty<PlotSeries> PlotFeeder::seriesList() { return {this, &m_series}; }
QQmlListProperty<QObject> PlotFeeder::targetList() { return {this, &m_targets}; }

void PlotFeeder::setWindowSec(double s)
{
    if (m_windowSec != s) { m_windowSec = s; emit windowSecChanged(); }
}

void PlotFeeder::setPaused(bool p)
{
    if (m_paused != p) { m_paused = p; emit pausedChanged(); }
}

void PlotFeeder::setActive(bool a)
{
    if (m_active == a)
        return;
    m_active = a;
    if (a) {
        m_timer.start();
        refresh();
    } else {
        m_timer.stop();
    }
    emit activeChanged();
}

void PlotFeeder::setX(double a, double b)
{
    if (m_xMin == a && m_xMax == b)
        return;
    m_xMin = a;
    m_xMax = b;
    emit xRangeChanged();
}

void PlotFeeder::setXMin(double v) { setX(v, m_xMax); }
void PlotFeeder::setXMax(double v) { setX(m_xMin, v); }

void PlotFeeder::setAutoY(bool a)
{
    if (m_autoY != a) { m_autoY = a; emit autoYChanged(); }
}

void PlotFeeder::setYMin(double v)
{
    if (!(m_yMin == v || (qIsNaN(m_yMin) && qIsNaN(v)))) { m_yMin = v; emit yFixedChanged(); }
}

void PlotFeeder::setYMax(double v)
{
    if (!(m_yMax == v || (qIsNaN(m_yMax) && qIsNaN(v)))) { m_yMax = v; emit yFixedChanged(); }
}

void PlotFeeder::setPixelWidth(int px)
{
    px = std::max(2, px);
    if (m_pixelWidth != px) { m_pixelWidth = px; emit pixelWidthChanged(); }
}

void PlotFeeder::setIntervalMs(int ms)
{
    if (m_timer.interval() != ms) { m_timer.setInterval(ms); emit intervalMsChanged(); }
}

void PlotFeeder::updateX()
{
    if (m_paused)
        return;
    const double now = PlotSeries::now();
    if (m_windowSec > 0) {
        setX(now - m_windowSec, now);
        return;
    }
    double first = now;
    for (const PlotSeries *s : std::as_const(m_series))
        if (!s->data().isEmpty())
            first = std::min(first, s->data().first().x());
    setX(first, now);
}

void PlotFeeder::updateY(double dtSec)
{
    double lo = qInf(), hi = -qInf();
    for (const PlotSeries *s : std::as_const(m_series)) {
        if (!s->enabled() || s->data().window(m_xMin, m_xMax).isEmpty())
            continue;
        const auto r = s->data().yRange(m_xMin, m_xMax);
        lo = std::min(lo, r.first);
        hi = std::max(hi, r.second);
    }
    if (lo > hi)
        lo = -1, hi = 1;
    // 넓어질 때는 바로, 좁아질 때는 0.5 s 지수 감쇠로 천천히
    const double k = 1 - std::exp(-dtSec / 0.5);
    auto follow = [&](double cur, double target, bool widen) {
        if (!m_yInit || widen)
            return target;
        return cur + (target - cur) * k;
    };
    double a = m_yMinOut, b = m_yMaxOut;
    if (m_autoY) {
        a = follow(a, lo, lo < a);
        b = follow(b, hi, hi > b);
        m_yInit = true;
    }
    if (std::isfinite(m_yMin)) a = m_yMin;
    if (std::isfinite(m_yMax)) b = m_yMax;
    if (b <= a)
        b = a + 1;
    if (a != m_yMinOut || b != m_yMaxOut) {
        m_yMinOut = a;
        m_yMaxOut = b;
        emit yRangeChanged();
    }
}

QList<QPointF> PlotFeeder::visiblePoints(int i) const
{
    if (i < 0 || i >= m_series.size() || !m_series[i]->enabled())
        return {};
    return m_series[i]->data().decimate(m_xMin, m_xMax, m_pixelWidth, m_series[i]->steps());
}

void PlotFeeder::refresh()
{
    if (!m_active)
        return;
    const double dt = m_since.isValid() ? m_since.restart() / 1000.0 : 0;
    if (!m_since.isValid())
        m_since.start();
    updateX();
    updateY(dt);
    for (int i = 0; i < m_targets.size(); ++i)
        if (auto *xy = qobject_cast<QXYSeries *>(m_targets[i]))
            xy->replace(visiblePoints(i));
    emit refreshed();
}

QVariantList PlotFeeder::valuesAt(double t) const
{
    QVariantList out;
    for (const PlotSeries *s : std::as_const(m_series)) {
        const auto p = s->data().valueAt(t);
        if (!s->enabled() || !p)
            continue;
        QVariantMap m{{"name", s->name()}, {"color", s->color()}, {"value", p->y()}, {"t", p->x()}};
        if (hasReference())
            m["delta"] = p->y() - s->data().valueAt(m_ref)->y();
        out << m;
    }
    return out;
}

void PlotFeeder::setReference(double t)
{
    m_ref = t;
    emit referenceChanged();
}

void PlotFeeder::clearReference()
{
    m_ref = qQNaN();
    emit referenceChanged();
}

QVariantList PlotFeeder::statsInView() const
{
    QVariantList out;
    for (const PlotSeries *s : std::as_const(m_series)) {
        const TimeBuffer::Stats st = s->data().stats(m_xMin, m_xMax);
        out << QVariantMap{{"name", s->name()}, {"count", st.count}, {"min", st.min}, {"max", st.max},
                           {"mean", st.mean}, {"rms", st.rms}, {"std", st.std},
                           {"hz", st.meanInterval > 0 ? 1 / st.meanInterval : 0.0}};
    }
    return out;
}

bool PlotFeeder::exportCsv(const QString &path, bool visibleOnly) const
{
    struct Row { double x; QString line; };
    QList<Row> rows;
    auto quote = [](QString s) {
        if (s.contains(',') || s.contains('"'))
            s = '"' + s.replace('"', "\"\"") + '"';
        return s;
    };
    for (const PlotSeries *s : std::as_const(m_series)) {
        const bool header = s->timeSource() == QLatin1String("header");
        const QList<QPointF> pts = visibleOnly ? s->data().window(m_xMin, m_xMax)
                                               : QList<QPointF>(s->data().points().begin(), s->data().points().end());
        const QString tail = ',' + quote(s->topic()) + ',' + quote(s->path()) + ',';
        for (const QPointF &p : pts) {
            const QString x = QString::number(p.x(), 'f', 6);
            const QString recv = header ? QString() : x;
            const QString stamp = header ? QString::number(p.x() - s->headerOffset(), 'f', 9) : QString();
            rows.append({p.x(), x + ',' + recv + ',' + stamp + tail + QString::number(p.y(), 'g', 10)});
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.x < b.x; });
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    ts << "x,receive time,header.stamp,topic,path,value\n";
    for (const Row &r : std::as_const(rows))
        ts << r.line << '\n';
    return ts.status() == QTextStream::Ok;
}

void PlotFeeder::zoomTo(double t0, double t1, double y0, double y1)
{
    setPaused(true);
    setX(std::min(t0, t1), std::max(t0, t1));
    if (std::isfinite(y0) && std::isfinite(y1)) {
        setYMin(std::min(y0, y1));
        setYMax(std::max(y0, y1));
    }
}

void PlotFeeder::zoomOut(bool horizontal, bool vertical)
{
    if (horizontal)
        setPaused(false);
    if (vertical) {
        setYMin(qQNaN());
        setYMax(qQNaN());
        setAutoY(true);
    }
}

void PlotFeeder::pan(double dx, double dy)
{
    if (dx != 0) {
        setPaused(true);
        setX(m_xMin + dx, m_xMax + dx);
    }
    if (dy != 0) {
        setYMin(m_yMinOut + dy);
        setYMax(m_yMaxOut + dy);
    }
}
