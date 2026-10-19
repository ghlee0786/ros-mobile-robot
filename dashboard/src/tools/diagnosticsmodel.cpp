#include "diagnosticsmodel.h"

#include "rosbridge.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSettings>

// 1 Hz 라 throttle 은 두지 않고 작은 queue 로 준다.
static const char *kDiag = "/diagnostics";
static const qint64 kStaleMs = 5000, kLostMs = 10000;

DiagnosticsModel::DiagnosticsModel(QObject *parent) : QAbstractListModel(parent)
{
    m_pins = QSettings().value("inspector/diagPins").toStringList();
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] { checkStale(QDateTime::currentMSecsSinceEpoch()); });
}

void DiagnosticsModel::setBridge(RosBridge *b)
{
    if (b == m_bridge)
        return;
    if (m_bridge) {
        if (m_subscribed)
            m_bridge->want(kDiag, {}, "diag", false);
        disconnect(m_bridge, nullptr, this, nullptr);
    }
    m_subscribed = false;
    m_bridge = b;
    if (b)
        connect(b, &RosBridge::message, this, [this](const QString &topic, const QJsonObject &msg) {
            if (topic == QLatin1String(kDiag))
                ingest(msg, QDateTime::currentMSecsSinceEpoch());
        });
    updateSubscription();
    emit bridgeChanged();
}

void DiagnosticsModel::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    updateSubscription();
    on ? m_timer.start() : m_timer.stop();
    emit activeChanged();
}

void DiagnosticsModel::updateSubscription()
{
    const bool want = m_bridge && m_active;
    if (want == m_subscribed)
        return;
    m_subscribed = want;
    m_bridge->want(kDiag, "diagnostic_msgs/msg/DiagnosticArray", "diag", want, 0, 5);
}

void DiagnosticsModel::setFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    rebuild();
    emit filterChanged();
}

QString DiagnosticsModel::levelName(int level)
{
    static const char *n[] = {"OK", "WARN", "ERROR", "STALE"};
    return level >= 0 && level <= 3 ? n[level] : QString::number(level);
}

QString DiagnosticsModel::groupOf(int level, bool pinned) const
{
    if (pinned)
        return "고정";
    static const char *g[] = {"Ok", "Warn", "Error", "Stale"};
    return level >= 0 && level <= 3 ? g[level] : "Error";
}

int DiagnosticsModel::levelFromState(const QString &state)
{
    const QString s = state.toLower();
    if (s == "ok" || s == "수신 중")
        return Ok;
    if (s == "warn" || s == "확인 중")
        return Warn;
    if (s == "error" || s == "데이터 없음")
        return Error;
    return Stale;   // 꺼짐(구독 안 함) · 모르는 상태
}

void DiagnosticsModel::ingest(const QJsonObject &array, qint64 nowMs)
{
    m_lastStream = nowMs;
    m_streamLost = false;
    for (const QJsonValue &v : array["status"].toArray()) {
        const QJsonObject s = v.toObject();
        Item it;
        it.name = s["name"].toString();
        it.hardwareId = s["hardware_id"].toString();
        it.key = it.hardwareId.isEmpty() ? it.name : it.hardwareId + ": " + it.name;
        it.level = qBound(0, s["level"].toInt(), 3);
        it.message = s["message"].toString();
        for (const QJsonValue &kv : s["values"].toArray())
            it.values.append(QVariantMap{{"key", kv["key"].toString()}, {"value", kv["value"].toString()}});
        it.lastMs = nowMs;
        m_real[it.key] = it;
    }
    rebuild();
}

void DiagnosticsModel::checkStale(qint64 nowMs)
{
    bool changed = false;
    for (Item &it : m_real)
        if (it.level != Stale && nowMs - it.lastMs > kStaleMs) {
            it.level = Stale;
            changed = true;
        }
    const bool lost = m_lastStream > 0 && nowMs - m_lastStream > kLostMs;
    if (lost != m_streamLost) {
        m_streamLost = lost;
        changed = true;
    }
    if (changed)
        rebuild();
}

void DiagnosticsModel::setSynthetic(const QVariantList &rows)
{
    m_synth.clear();
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        Item it;
        it.name = m.value("label", m.value("name")).toString();
        it.key = "합성: " + it.name;
        it.level = levelFromState(m["state"].toString());
        it.message = m["detail"].toString();
        it.values = {QVariantMap{{"key", "state"}, {"value", m["state"].toString()}},
                     QVariantMap{{"key", "topic"}, {"value", m["topic"].toString()}}};
        it.synthetic = true;
        m_synth.append(it);
    }
    if (m_real.isEmpty())
        rebuild();
}

void DiagnosticsModel::togglePin(const QString &key)
{
    m_pins.contains(key) ? void(m_pins.removeAll(key)) : m_pins.append(key);
    QSettings().setValue("inspector/diagPins", m_pins);
    rebuild();
}

int DiagnosticsModel::countLevel(int level) const
{
    int n = 0;
    if (!m_real.isEmpty()) {
        for (const Item &it : m_real)
            n += it.level == level;
    } else {
        for (const Item &it : m_synth)
            n += it.level == level;
    }
    return n;
}

QVariantMap DiagnosticsModel::groupCounts() const
{
    QVariantMap c;
    for (const Item &it : m_rows) {
        const QString g = groupOf(it.level, m_pins.contains(it.key));
        c[g] = c.value(g).toInt() + 1;
    }
    return c;
}

void DiagnosticsModel::rebuild()
{
    QList<Item> rows = m_real.isEmpty() ? m_synth : m_real.values();
    if (!m_filter.isEmpty())
        rows.removeIf([this](const Item &it) {
            return !it.key.contains(m_filter, Qt::CaseInsensitive) && !it.message.contains(m_filter, Qt::CaseInsensitive);
        });
    auto rank = [this](const Item &it) { return m_pins.contains(it.key) ? 0 : 4 - it.level; };   // 고정·Stale·Error·Warn·Ok
    std::stable_sort(rows.begin(), rows.end(), [&](const Item &a, const Item &b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a.key < b.key;
    });
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countsChanged();
}

int DiagnosticsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant DiagnosticsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Item &it = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case KeyRole: return it.key;
    case NameRole: return it.name;
    case HardwareIdRole: return it.hardwareId;
    case LevelRole: return it.level;
    case LevelNameRole: return levelName(it.level);
    case MessageRole: return it.message;
    case ValuesRole: return it.values;
    case GroupRole: return groupOf(it.level, m_pins.contains(it.key));
    case PinnedRole: return m_pins.contains(it.key);
    case SyntheticRole: return it.synthetic;
    }
    return {};
}

QHash<int, QByteArray> DiagnosticsModel::roleNames() const
{
    return {{KeyRole, "key"}, {NameRole, "name"}, {HardwareIdRole, "hardwareId"}, {LevelRole, "level"},
            {LevelNameRole, "levelName"}, {MessageRole, "message"}, {ValuesRole, "values"},
            {GroupRole, "group"}, {PinnedRole, "pinned"}, {SyntheticRole, "synthetic"}};
}
