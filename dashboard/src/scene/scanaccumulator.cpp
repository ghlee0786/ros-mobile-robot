#include "scanaccumulator.h"
#include "scenemath.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

ScanAccumulator::ScanAccumulator(QObject *parent) : SceneSource(parent)
{
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_batches.size() > 1)   // 묶음이 하나면 바뀔 것이 없다
            prune(now());
    });
    connect(this, &SceneSource::activeChanged, this, [this] {
        if (active())
            m_tick.start();
        else
            m_tick.stop(), m_batches.clear(), rebuild(now());
    });
}

void ScanAccumulator::setDecaySec(double s)
{
    m_decay = qMax(0.0, s);
    prune(now());
    emit settingsChanged();
}

void ScanAccumulator::setColorMode(const QString &m)
{
    m_mode = m;
    rebuild(now());
    emit settingsChanged();
}

void ScanAccumulator::setColor(const QColor &c)
{
    m_color = c;
    rebuild(now());
    emit settingsChanged();
}

void ScanAccumulator::handle(const QString &topic, const QJsonObject &msg)
{
    if (topic == "/scan")
        addScan(msg, now());
    else if (topic == "/tf_static")
        for (const QJsonValue &v : msg["transforms"].toArray()) {
            const QJsonObject t = v.toObject(), tr = t["transform"].toObject();
            if (t["header"].toObject()["frame_id"].toString().remove(QChar('/')) == "base_link")
                m_mounts[t["child_frame_id"].toString().remove(QChar('/'))] = {
                    tr["translation"].toObject()["x"].toDouble(), tr["translation"].toObject()["y"].toDouble(),
                    scene::yawOf(tr["rotation"].toObject())};
        }
}

void ScanAccumulator::addScan(const QJsonObject &m, qint64 nowMs)
{
    const QJsonArray ranges = m["ranges"].toArray(), inten = m["intensities"].toArray();
    const double a0 = m["angle_min"].toDouble(), da = m["angle_increment"].toDouble();
    const double rmin = qMax(m["range_min"].toDouble(), kSelfHide), rmax = m["range_max"].toDouble(1e9);
    const msgparse::Pose2D mount = m_mounts.value(m["header"].toObject()["frame_id"].toString().remove(QChar('/')));
    const double c = std::cos(m_rt), s = std::sin(m_rt);
    Batch b{nowMs, {}, {}, {}};
    for (int i = 0; i < ranges.size() && b.pts.size() < kMaxPoints; ++i) {
        const double r = ranges[i].toDouble();   // inf/NaN 은 null 로 와서 0
        if (!(r >= rmin && r <= rmax))
            continue;
        const double a = a0 + i * da;
        const msgparse::Pose2D p = msgparse::transform2D(mount, {r * std::cos(a), r * std::sin(a), 0});
        const double px = p.x, py = p.y;
        b.pts.append(QVector3D(float(m_rx + c * px - s * py), float(m_ry + s * px + c * py), 0));
        b.range.append(float(r));
        if (inten.size() == ranges.size())
            b.intensity.append(float(inten[i].toDouble()));
    }
    m_batches.append(b);
    setReceived(true);
    prune(nowMs);
}

void ScanAccumulator::prune(qint64 nowMs)
{
    while (m_batches.size() > 1 && nowMs - m_batches.first().t > qint64(m_decay * 1000))
        m_batches.removeFirst();
    qsizetype total = 0;
    for (const Batch &b : std::as_const(m_batches))
        total += b.pts.size();
    while (m_batches.size() > 1 && total > kMaxPoints)
        total -= m_batches.takeFirst().pts.size();
    rebuild(nowMs);
}

// 묶음 안에서 최소~최대를 0..1 로 펴서 무지개 색을 고른다(RViz 자동 범위).
static void addRainbow(QList<QColor> &out, const QList<float> &v, float alpha)
{
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    const float span = v.isEmpty() ? 0 : *hi - *lo;
    for (float x : v) {
        QColor c = scene::rainbow(span > 0 ? (x - *lo) / span : 0);
        c.setAlphaF(alpha);
        out.append(c);
    }
}

void ScanAccumulator::rebuild(qint64 nowMs)
{
    m_points.clear();
    m_colors.clear();
    for (const Batch &b : std::as_const(m_batches)) {
        const bool last = &b == &m_batches.last();
        const float alpha = last || m_decay <= 0 ? 1.0f
                          : qBound(kMinAlpha, float(1 - (nowMs - b.t) / (m_decay * 1000)), 1.0f);
        m_points.append(b.pts);
        if (m_mode == "range")
            addRainbow(m_colors, b.range, alpha);
        else if (m_mode == "intensity" && !b.intensity.isEmpty())
            addRainbow(m_colors, b.intensity, alpha);
        else {
            QColor c = m_color;
            c.setAlphaF(alpha);
            m_colors.append(QList<QColor>(b.pts.size(), c));
        }
    }
    emit pointsChanged();
}
