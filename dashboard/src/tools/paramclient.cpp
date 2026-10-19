#include "paramclient.h"

#include "rosbridge.h"

#include <QJsonArray>
#include <QJsonDocument>

static const char *kEvents = "/parameter_events";

ParamClient::ParamClient(QObject *parent) : QAbstractListModel(parent)
{
    connect(&m_queue, &ServiceQueue::pendingChanged, this, &ParamClient::busyChanged);
}

void ParamClient::setBridge(RosBridge *b)
{
    if (b == m_bridge)
        return;
    if (m_bridge) {
        if (m_subscribed)
            m_bridge->want(kEvents, {}, "params", false);
        disconnect(m_bridge, nullptr, this, nullptr);
    }
    m_subscribed = false;
    m_bridge = b;
    m_queue.setBridge(b);
    if (b)
        connect(b, &RosBridge::message, this, [this](const QString &topic, const QJsonObject &msg) {
            if (topic == QLatin1String(kEvents))
                onEvent(msg);
        });
    updateSubscription();
    emit bridgeChanged();
}

void ParamClient::setNode(const QString &n)
{
    const QString node = n.isEmpty() || n.startsWith('/') ? n : '/' + n;
    if (node == m_node)
        return;
    m_node = node;
    ++m_gen;
    beginResetModel();
    m_params.clear();
    endResetModel();
    emit countChanged();
    emit nodeChanged();
    updateSubscription();
}

void ParamClient::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    updateSubscription();
    emit activeChanged();
}

void ParamClient::updateSubscription()
{
    const bool want = m_bridge && m_active && !m_node.isEmpty();
    if (want == m_subscribed)
        return;
    m_subscribed = want;
    m_bridge->want(kEvents, "rcl_interfaces/msg/ParameterEvent", "params", want);
}

void ParamClient::setError(const QString &e)
{
    m_error = e;
    emit resultChanged();
}

int ParamClient::find(const QString &name) const
{
    for (int i = 0; i < m_params.size(); ++i)
        if (m_params[i].name == name)
            return i;
    return -1;
}

void ParamClient::refresh()
{
    if (m_node.isEmpty())
        return;
    setError({});
    const int gen = m_gen;
    auto fail = [this, gen](const QString &e) { if (gen == m_gen) setError(e); };
    m_queue.enqueue(service("list_parameters"), {{"prefixes", QJsonArray()}, {"depth", 0}},
                    [this, gen, fail](const QJsonObject &r) {
        if (gen != m_gen)
            return;
        const QJsonArray names = QJsonArray::fromStringList(parseListResult(r));
        m_queue.enqueue(service("describe_parameters"), {{"names", names}}, [this, gen, names, fail](const QJsonObject &r) {
            if (gen != m_gen)
                return;
            QList<Param> params = parseDescriptors(r);
            m_queue.enqueue(service("get_parameters"), {{"names", names}}, [this, gen, params](const QJsonObject &r) mutable {
                if (gen != m_gen)
                    return;
                const QVariantList values = parseValues(r);
                for (int i = 0; i < params.size() && i < values.size(); ++i)
                    params[i].value = values[i];
                std::sort(params.begin(), params.end(), [](const Param &a, const Param &b) { return a.name < b.name; });
                beginResetModel();
                m_params = params;
                endResetModel();
                emit countChanged();
            }, fail);
        }, fail);
    }, fail);
}

void ParamClient::set(const QString &name, const QVariant &value)
{
    const int i = find(name);
    if (i < 0 || m_node.isEmpty())
        return;
    if (!m_writable) {
        setError("쓰기 막힘(--no-publish)");
        return;
    }
    const int type = m_params[i].type;
    const int gen = m_gen;
    const QJsonObject req = makeSetRequest(name, type, value);
    const QVariant newValue = parseValue(req["parameters"][0]["value"].toObject());
    m_queue.enqueue(service("set_parameters"), req, [this, gen, name, newValue](const QJsonObject &r) {
        if (gen != m_gen)
            return;
        const auto res = parseSetResult(r);
        m_hasResult = true, m_lastOk = res.first, m_lastReason = res.second;
        m_error.clear();
        const int j = find(name);
        if (res.first && j >= 0) {
            m_params[j].value = newValue;
            emit dataChanged(index(j), index(j));
        }
        emit resultChanged();
    }, [this, gen](const QString &e) {
        if (gen != m_gen)
            return;
        m_hasResult = true, m_lastOk = false, m_lastReason = e;
        setError(e);
    });
}

void ParamClient::onEvent(const QJsonObject &ev)
{
    if (ev["node"].toString() != m_node)
        return;
    for (const char *key : {"new_parameters", "changed_parameters"})
        for (const QJsonValue &p : ev[key].toArray()) {
            const int i = find(p["name"].toString());
            if (i < 0)
                continue;
            m_params[i].value = parseValue(p["value"].toObject());
            emit dataChanged(index(i), index(i));
        }
    for (const QJsonValue &p : ev["deleted_parameters"].toArray()) {
        const int i = find(p["name"].toString());
        if (i < 0)
            continue;
        beginRemoveRows({}, i, i);
        m_params.removeAt(i);
        endRemoveRows();
        emit countChanged();
    }
}

// ---- rcl_interfaces 변환 ----------------------------------------------------------------------

QStringList ParamClient::parseListResult(const QJsonObject &resp)
{
    QStringList names;
    for (const QJsonValue &v : resp["result"]["names"].toArray())
        names.append(v.toString());
    return names;
}

QList<ParamClient::Param> ParamClient::parseDescriptors(const QJsonObject &resp)
{
    QList<Param> out;
    for (const QJsonValue &v : resp["descriptors"].toArray()) {
        Param p;
        p.name = v["name"].toString();
        p.type = v["type"].toInt();
        p.description = v["description"].toString();
        p.readOnly = v["read_only"].toBool();
        for (const char *key : {"floating_point_range", "integer_range"}) {
            const QJsonArray r = v[key].toArray();
            if (r.isEmpty())
                continue;
            p.hasRange = true;
            p.min = r[0]["from_value"].toDouble();
            p.max = r[0]["to_value"].toDouble();
            p.step = r[0]["step"].toDouble();
        }
        out.append(p);
    }
    return out;
}

QVariant ParamClient::parseValue(const QJsonObject &pv)
{
    switch (pv["type"].toInt()) {
    case Bool: return pv["bool_value"].toBool();
    case Integer: return pv["integer_value"].toVariant().toLongLong();
    case Double: return pv["double_value"].toDouble();
    case String: return pv["string_value"].toString();
    case ByteArray: return pv["byte_array_value"].toArray().toVariantList();
    case BoolArray: return pv["bool_array_value"].toArray().toVariantList();
    case IntegerArray: return pv["integer_array_value"].toArray().toVariantList();
    case DoubleArray: return pv["double_array_value"].toArray().toVariantList();
    case StringArray: return pv["string_array_value"].toArray().toVariantList();
    default: return {};
    }
}

QVariantList ParamClient::parseValues(const QJsonObject &resp)
{
    QVariantList out;
    for (const QJsonValue &v : resp["values"].toArray())
        out.append(parseValue(v.toObject()));
    return out;
}

QJsonObject ParamClient::makeValue(int type, const QVariant &v)
{
    QJsonObject o{{"type", type}};
    auto list = [&v] {   // 배열은 QML 배열이나 "[1, 2]" 같은 JSON 문자열로 받는다
        if (v.typeId() == QMetaType::QString)
            return QJsonDocument::fromJson(v.toString().toUtf8()).array();
        return QJsonArray::fromVariantList(v.toList());
    };
    switch (type) {
    case Bool:
        o["bool_value"] = v.typeId() == QMetaType::QString ? v.toString().trimmed().toLower() == "true" : v.toBool();
        break;
    case Integer: {
        bool ok = false;
        const qint64 n = v.toString().trimmed().toLongLong(&ok);
        o["integer_value"] = ok ? n : qint64(qRound64(v.toString().trimmed().toDouble()));
        break;
    }
    case Double: o["double_value"] = v.toString().trimmed().toDouble(); break;
    case String: o["string_value"] = v.toString(); break;
    case ByteArray: o["byte_array_value"] = list(); break;
    case BoolArray: o["bool_array_value"] = list(); break;
    case IntegerArray: o["integer_array_value"] = list(); break;
    case DoubleArray: o["double_array_value"] = list(); break;
    case StringArray: o["string_array_value"] = list(); break;
    }
    return o;
}

QJsonObject ParamClient::makeSetRequest(const QString &name, int type, const QVariant &v)
{
    return {{"parameters", QJsonArray{QJsonObject{{"name", name}, {"value", makeValue(type, v)}}}}};
}

QPair<bool, QString> ParamClient::parseSetResult(const QJsonObject &resp)
{
    const QJsonObject r = resp["results"][0].toObject();
    return {r["successful"].toBool(), r["reason"].toString()};
}

QString ParamClient::typeName(int type)
{
    static const char *n[] = {"not_set", "bool", "integer", "double", "string", "byte[]", "bool[]",
                              "integer[]", "double[]", "string[]"};
    return type >= 0 && type <= 9 ? n[type] : QString::number(type);
}

QString ParamClient::valueText(const QVariant &v)
{
    if (v.typeId() == QMetaType::QVariantList)
        return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(v.toList())).toJson(QJsonDocument::Compact));
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? "true" : "false";
    return v.toString();
}

// ---- 모델 -------------------------------------------------------------------------------------

int ParamClient::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_params.size();
}

QVariant ParamClient::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_params.size())
        return {};
    const Param &p = m_params.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return p.name;
    case TypeRole: return p.type;
    case TypeNameRole: return typeName(p.type);
    case ValueRole: return p.value;
    case ValueTextRole: return valueText(p.value);
    case HasRangeRole: return p.hasRange;
    case MinRole: return p.min;
    case MaxRole: return p.max;
    case StepRole: return p.step;
    case ReadOnlyRole: return p.readOnly;
    case DescriptionRole: return p.description;
    case EditorRole:   // QML 이 고를 편집기: 범위가 있으면 slider, bool 은 switch, 나머지는 text
        return p.hasRange && (p.type == Integer || p.type == Double) ? "slider" : p.type == Bool ? "switch" : "text";
    }
    return {};
}

QHash<int, QByteArray> ParamClient::roleNames() const
{
    return {{NameRole, "name"}, {TypeRole, "type"}, {TypeNameRole, "typeName"}, {ValueRole, "value"},
            {ValueTextRole, "valueText"}, {HasRangeRole, "hasRange"}, {MinRole, "min"}, {MaxRole, "max"},
            {StepRole, "step"}, {ReadOnlyRole, "readOnly"}, {DescriptionRole, "description"},
            {EditorRole, "editor"}};
}
