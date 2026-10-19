#include "plotseries.h"

#include <QElapsedTimer>

namespace {
// 앱 시작(프로그램 적재) 때 시작하는 공통 시계
const QElapsedTimer g_clock = [] { QElapsedTimer t; t.start(); return t; }();
int g_nextId = 0;
} // namespace

double PlotSeries::now()
{
    return g_clock.nsecsElapsed() / 1e9;
}

PlotSeries::PlotSeries(QObject *parent)
    : QObject(parent), m_reason(QStringLiteral("plot:%1").arg(++g_nextId))
{
}

PlotSeries::~PlotSeries()
{
    unsubscribe();
}

void PlotSeries::setBridge(RosBridge *b)
{
    if (m_bridge == b)
        return;
    unsubscribe();
    if (m_bridge) {
        disconnect(m_msgConn);
        disconnect(m_listConn);
    }
    m_bridge = b;
    if (b) {
        m_msgConn = connect(b, &RosBridge::message, this, &PlotSeries::onMessage);
        m_listConn = connect(b, &RosBridge::topicListChanged, this, [this] {
            if (m_subTopic.isEmpty())
                resubscribe();
        });
    }
    resubscribe();
    emit bridgeChanged();
}

void PlotSeries::setPath(const QString &p)
{
    if (m_path == p)
        return;
    unsubscribe();
    m_path = p;
    m_mp = MessagePath();
    m_pathError.clear();
    if (!p.trimmed().isEmpty() && !m_mp.parse(p.trimmed()))
        m_pathError = QStringLiteral("%1 (위치 %2)").arg(m_mp.error()).arg(m_mp.errorPos());
    m_stampOffset = qQNaN();
    m_raw.clear();
    rebuildChain();
    resubscribe();
    emit pathChanged();
}

void PlotSeries::setTransform(const QString &t)
{
    if (m_transform == t)
        return;
    m_transform = t;
    rebuildChain();
    emit transformChanged();
}

void PlotSeries::setName(const QString &n)
{
    if (m_name != n) { m_name = n; emit nameChanged(); }
}

void PlotSeries::setColor(const QColor &c)
{
    if (m_color != c) { m_color = c; emit colorChanged(); }
}

void PlotSeries::setEnabled(bool e)
{
    if (m_enabled != e) { m_enabled = e; emit enabledChanged(); }
}

void PlotSeries::setTimeSource(const QString &s)
{
    if (m_timeSource == s)
        return;
    m_timeSource = s;
    m_stampOffset = qQNaN();
    clear();
    emit timeSourceChanged();
}

void PlotSeries::setThrottleMs(int ms)
{
    if (m_throttleMs == ms)
        return;
    m_throttleMs = ms;
    if (!m_subTopic.isEmpty())
        m_bridge->want(m_subTopic, m_bridge->wantedType(m_subTopic), m_reason, true, m_throttleMs);
    emit throttleMsChanged();
}

void PlotSeries::resubscribe()
{
    if (!m_bridge || !m_mp.ok() || !m_subTopic.isEmpty())
        return;
    const QString type = m_bridge->topicTypes().value(m_mp.topic());
    if (type.isEmpty())
        return; // 토픽 목록이 갱신되면 다시 시도한다
    m_subTopic = m_mp.topic();
    m_bridge->want(m_subTopic, type, m_reason, true, m_throttleMs);
}

void PlotSeries::unsubscribe()
{
    if (m_bridge && !m_subTopic.isEmpty())
        m_bridge->want(m_subTopic, QString(), m_reason, false);
    m_subTopic.clear();
}

void PlotSeries::rebuildChain()
{
    m_chain.clear();
    m_transformError.clear();
    if (m_mp.ok() && m_mp.modifier() == QLatin1String("derivative"))
        m_chain.push_back(std::make_unique<DerivativeTransform>());
    if (!m_transform.trimmed().isEmpty() && m_transform.trimmed() != QLatin1String("none")) {
        if (auto t = makeTransform(m_transform, &m_transformError))
            m_chain.push_back(std::move(t));
    }
    m_out.clear();
    if (!m_chain.empty()) {
        const TimeBuffer raw = m_raw;
        m_raw.clear();
        for (const QPointF &p : raw.points())
            feed(p.x(), p.y());
    }
    const QString e = m_pathError.isEmpty() ? m_transformError : m_pathError;
    if (m_error != e) {
        m_error = e;
        emit errorChanged();
    }
    emit updated();
}

void PlotSeries::push(double t, double v)
{
    feed(t, v);
    emit updated();
}

void PlotSeries::feed(double t, double v)
{
    m_raw.add(t, v);
    if (!m_chain.empty()) {
        std::optional<QPointF> p = QPointF(t, v);
        for (const auto &tr : m_chain)
            if (!(p = tr->push(*p)))
                break;
        if (p)
            m_out.add(p->x(), p->y());
    }
}

void PlotSeries::clear()
{
    m_raw.clear();
    m_out.clear();
    for (const auto &tr : m_chain)
        tr->reset();
    emit updated();
}

void PlotSeries::onMessage(const QString &topic, const QJsonObject &msg)
{
    if (topic != m_subTopic)
        return;
    const QList<double> vals = m_mp.evaluate(msg);
    if (vals.isEmpty())
        return;
    double t = now();
    if (m_timeSource == QLatin1String("header")) {
        const QJsonObject st = msg["header"].toObject()["stamp"].toObject();
        if (!st.isEmpty()) {
            const double stamp = st["sec"].toDouble() + st["nanosec"].toDouble() / 1e9;
            if (qIsNaN(m_stampOffset))
                m_stampOffset = t - stamp;
            t = stamp + m_stampOffset;
        }
    }
    // 배열 · 슬라이스로 여러 값이 나오면 선 하나에는 첫 값을 쓴다
    push(t, vals.first());
}
