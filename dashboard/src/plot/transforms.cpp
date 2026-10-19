#include "transforms.h"

#include <QRegularExpression>
#include <cmath>

std::optional<QPointF> DerivativeTransform::step(QPointF p)
{
    const std::optional<QPointF> prev = m_prev;
    m_prev = p;
    if (!prev)
        return std::nullopt;
    const double dt = m_fixedDt > 0 ? m_fixedDt : p.x() - prev->x();
    if (dt <= 0)
        return std::nullopt;
    return QPointF((p.x() + prev->x()) / 2, (p.y() - prev->y()) / dt);
}

std::optional<QPointF> MovingAverageTransform::step(QPointF p)
{
    const double y = m_rms ? p.y() * p.y() : p.y();
    m_win.push_back(QPointF(p.x(), y));
    m_sum += y;
    if (int(m_win.size()) > m_n) {
        m_sum -= m_win.front().y();
        m_win.pop_front();
    }
    const double mean = m_sum / m_win.size();
    const double t = m_centered ? m_win[m_win.size() / 2].x() : p.x();
    return QPointF(t, m_rms ? std::sqrt(std::max(0.0, mean)) : mean);
}

std::optional<QPointF> IntegralTransform::step(QPointF p)
{
    if (m_prev)
        m_acc += (p.x() - m_prev->x()) * (p.y() + m_prev->y()) / 2;
    m_prev = p;
    return QPointF(p.x(), m_acc);
}

std::optional<QPointF> TimeSincePreviousTransform::step(QPointF p)
{
    const std::optional<double> prev = m_prev;
    m_prev = p.x();
    if (!prev)
        return std::nullopt;
    return QPointF(p.x(), p.x() - *prev);
}

std::optional<QPointF> OutlierRemovalTransform::step(QPointF p)
{
    m_win.push_back(p);
    if (m_win.size() > 4)
        m_win.pop_front();
    if (m_win.size() < 4)
        return m_win.size() == 1 ? std::nullopt : std::optional<QPointF>(m_win[m_win.size() - 2]);
    const double d0 = m_win[0].y() - m_win[1].y();
    const double d1 = m_win[1].y() - m_win[2].y();
    const double d2 = m_win[2].y() - m_win[3].y();
    if (d1 * d2 < 0 && std::max(std::abs(d1), std::abs(d2)) > m_factor * std::abs(d0))
        return std::nullopt;
    return m_win[2];
}

std::unique_ptr<Transform> makeTransform(const QString &spec, QString *error)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*([A-Za-z/]+)\s*(?:\((.*)\))?\s*$)"));
    const auto m = re.match(spec);
    QList<double> args;
    QStringList words;
    if (m.hasMatch())
        for (const QString &a : m.captured(2).split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const double d = a.trimmed().toDouble(&ok);
            if (ok) args << d; else words << a.trimmed();
        }
    const QString name = m.captured(1).toLower();
    auto arg = [&](int i, double def) { return i < args.size() ? args[i] : def; };
    if (name == "d/dt" || name == "derivative")
        return std::make_unique<DerivativeTransform>(arg(0, 0));
    if (name == "avg" || name == "movingaverage")
        return std::make_unique<MovingAverageTransform>(int(arg(0, 10)), words.contains("center"));
    if (name == "rms" || name == "movingrms")
        return std::make_unique<MovingAverageTransform>(int(arg(0, 10)), words.contains("center"), true);
    if (name == "int" || name == "integral")
        return std::make_unique<IntegralTransform>();
    if (name == "scale")
        return std::make_unique<ScaleOffsetTransform>(arg(0, 1), arg(1, 0), arg(2, 0));
    if (name == "abs")
        return std::make_unique<AbsTransform>();
    if (name == "dt" || name == "timesinceprevious")
        return std::make_unique<TimeSincePreviousTransform>();
    if (name == "outlier" || name == "outlierremoval")
        return std::make_unique<OutlierRemovalTransform>(arg(0, 100));
    if (error)
        *error = QStringLiteral("알 수 없는 변환 '%1'").arg(spec);
    return nullptr;
}
