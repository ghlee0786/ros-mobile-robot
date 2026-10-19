#include "logmodel.h"

#include "rosbridge.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QSettings>

// /rosout 은 throttle 0 에 queue 를 크게(버스트에 줄이 빠지지 않게) 준다.
static const char *kRosout = "/rosout";

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {}

void LogModel::setBridge(RosBridge *b)
{
    if (b == m_bridge)
        return;
    if (m_bridge) {
        if (m_subscribed)
            m_bridge->want(kRosout, {}, "log", false);
        disconnect(m_bridge, nullptr, this, nullptr);
    }
    m_subscribed = false;
    m_bridge = b;
    if (b)
        connect(b, &RosBridge::message, this, [this](const QString &topic, const QJsonObject &msg) {
            if (topic == QLatin1String(kRosout) && m_active)
                append(msg);
        });
    updateSubscription();
    emit bridgeChanged();
}

void LogModel::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    updateSubscription();
    emit activeChanged();
}

void LogModel::setPaused(bool p)
{
    if (p == m_paused)
        return;
    m_paused = p;
    emit pausedChanged();
}

void LogModel::setCapacity(int n)
{
    n = qMax(1, n);
    if (n == m_capacity)
        return;
    m_capacity = n;
    if (m_rows.size() > n) {
        beginRemoveRows({}, 0, m_rows.size() - n - 1);
        m_rows.remove(0, m_rows.size() - n);
        endRemoveRows();
    }
    emit countChanged();
}

void LogModel::updateSubscription()
{
    const bool want = m_bridge && m_active;
    if (want == m_subscribed)
        return;
    m_subscribed = want;
    m_bridge->want(kRosout, "rcl_interfaces/msg/Log", "log", want, 0, 200);
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QString LogModel::levelName(int level)
{
    switch (level) {
    case 10: return "DEBUG";
    case 20: return "INFO";
    case 30: return "WARN";
    case 40: return "ERROR";
    case 50: return "FATAL";
    default: return QString::number(level);
    }
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Entry &e = m_rows.at(index.row());
    switch (role) {
    case StampRole: return e.stamp;
    case LevelRole: return e.level;
    case LevelNameRole: return levelName(e.level);
    case NameRole: return e.name;
    case Qt::DisplayRole:
    case MsgRole: return e.msg;
    case FileRole: return e.file;
    case FunctionRole: return e.function;
    case LineRole: return e.line;
    case TimeTextRole: return QDateTime::fromMSecsSinceEpoch(qint64(e.stamp * 1000)).toString("HH:mm:ss.zzz");
    }
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {{StampRole, "stamp"}, {LevelRole, "level"}, {LevelNameRole, "levelName"}, {NameRole, "name"},
            {MsgRole, "msg"}, {FileRole, "file"}, {FunctionRole, "function"}, {LineRole, "line"},
            {TimeTextRole, "timeText"}};
}

void LogModel::append(const QJsonObject &m)
{
    if (m_paused)
        return;
    const QJsonObject st = m["stamp"].toObject();
    Entry e{st["sec"].toDouble() + st["nanosec"].toDouble() / 1e9, m["level"].toInt(), m["name"].toString(),
            m["msg"].toString(), m["file"].toString(), m["function"].toString(), m["line"].toInt()};
    if (m_rows.size() >= m_capacity) {
        beginRemoveRows({}, 0, 0);
        m_rows.removeFirst();
        endRemoveRows();
    }
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.append(e);
    endInsertRows();
    emit countChanged();
    if (!m_nodes.contains(e.name)) {
        m_nodes.append(e.name);
        m_nodes.sort();
        emit nodesChanged();
    }
}

void LogModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

// ---------------------------------------------------------------------------------------------

LogFilterModel::LogFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    m_rules = QSettings().value("inspector/logRules").toList();
    compileRules();
    connect(this, &QAbstractItemModel::rowsInserted, this, &LogFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &LogFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &LogFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &LogFilterModel::countChanged);
}

void LogFilterModel::setMinLevel(int l)
{
    if (l == m_minLevel)
        return;
    m_minLevel = l;
    invalidateRowsFilter();
    emit filterChanged();
}

void LogFilterModel::setTerms(const QStringList &t)
{
    QStringList lower;
    for (const QString &s : t)
        if (!s.trimmed().isEmpty())
            lower.append(s.trimmed().toLower());
    if (lower == m_terms)
        return;
    m_terms = lower;
    invalidateRowsFilter();
    emit filterChanged();
}

void LogFilterModel::setHiddenNodes(const QStringList &n)
{
    if (n == m_hidden)
        return;
    m_hidden = n;
    invalidateRowsFilter();
    emit filterChanged();
}

void LogFilterModel::setRules(const QVariantList &r)
{
    if (r == m_rules)
        return;
    m_rules = r;
    QSettings().setValue("inspector/logRules", r);
    compileRules();
    invalidateRowsFilter();
    emit filterChanged();
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {HighlightRole});
}

void LogFilterModel::compileRules()
{
    m_compiled.clear();
    for (const QVariant &v : std::as_const(m_rules)) {
        const QVariantMap m = v.toMap();
        m_compiled.append({m["kind"].toString() == "exclude",
                           QRegularExpression(m["pattern"].toString(), QRegularExpression::CaseInsensitiveOption),
                           m["node"].toString(), m["minLevel"].toInt()});
    }
}

bool LogFilterModel::ruleMatches(const Rule &r, const LogModel::Entry &e) const
{
    return r.re.isValid() && e.level >= r.minLevel && (r.node.isEmpty() || r.node == e.name)
           && (r.re.pattern().isEmpty() || r.re.match(e.msg).hasMatch());
}

bool LogFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    const auto *src = qobject_cast<const LogModel *>(sourceModel());
    if (!src)
        return true;
    const LogModel::Entry &e = src->entry(sourceRow);
    if (e.level < m_minLevel || m_hidden.contains(e.name))
        return false;
    for (const Rule &r : m_compiled)
        if (r.exclude && ruleMatches(r, e))
            return false;
    if (m_terms.isEmpty())
        return true;
    const QString name = e.name.toLower(), msg = e.msg.toLower();
    for (const QString &t : m_terms)
        if (name.contains(t) || msg.contains(t))
            return true;
    return false;
}

QVariant LogFilterModel::data(const QModelIndex &index, int role) const
{
    if (role != HighlightRole)
        return QSortFilterProxyModel::data(index, role);
    const auto *src = qobject_cast<const LogModel *>(sourceModel());
    if (!src || !index.isValid())
        return false;
    const LogModel::Entry &e = src->entry(mapToSource(index).row());
    for (const Rule &r : m_compiled)
        if (!r.exclude && ruleMatches(r, e))
            return true;
    return false;
}

QHash<int, QByteArray> LogFilterModel::roleNames() const
{
    QHash<int, QByteArray> r = QSortFilterProxyModel::roleNames();
    r[HighlightRole] = "highlight";
    return r;
}

QString LogFilterModel::tsv(const QList<int> &rows) const
{
    const auto *src = qobject_cast<const LogModel *>(sourceModel());
    if (!src)
        return {};
    QStringList lines;
    for (int row : rows) {
        if (row < 0 || row >= rowCount())
            continue;
        const QModelIndex si = mapToSource(index(row, 0));
        const LogModel::Entry &e = src->entry(si.row());
        lines.append(QStringList{si.data(LogModel::TimeTextRole).toString(), LogModel::levelName(e.level), e.name,
                                 QString(e.msg).replace('\t', ' ').replace('\n', ' '), e.file,
                                 e.function, QString::number(e.line)}.join('\t'));
    }
    return lines.join('\n');
}

void LogFilterModel::copyTsv(const QList<int> &rows) const
{
    QGuiApplication::clipboard()->setText(tsv(rows));
}
