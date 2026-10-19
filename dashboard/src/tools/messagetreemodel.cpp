#include "messagetreemodel.h"

#include "rosbridge.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <functional>

struct MessageTreeModel::Node {
    QString key, path, type, text;
    bool numeric = false, more = false;
    Node *parent = nullptr;
    int row = 0;
    std::vector<std::unique_ptr<Node>> children;
};

static QString typeName(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Object: return "object";
    case QJsonValue::Array: return "array";
    case QJsonValue::Double: return "number";
    case QJsonValue::String: return "string";
    case QJsonValue::Bool: return "bool";
    default: return "null";
    }
}

static QString valueText(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Object: return QString("{%1}").arg(v.toObject().size());
    case QJsonValue::Array: return QString("[%1]").arg(v.toArray().size());
    case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 10);
    case QJsonValue::String: return v.toString();
    case QJsonValue::Bool: return v.toBool() ? "true" : "false";
    default: return "null";
    }
}

static QString childPath(const QString &base, const QString &key)
{
    return base.isEmpty() ? key : base + '.' + key;
}

// 잎 필드를 경로 → 값으로 편다(비교용).
static void flatten(const QString &path, const QJsonValue &v, QHash<QString, QJsonValue> &out)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.isEmpty())
            out[path] = v;
        for (auto it = o.begin(); it != o.end(); ++it)
            flatten(childPath(path, it.key()), it.value(), out);
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (a.isEmpty())
            out[path] = v;
        for (int i = 0; i < a.size(); ++i)
            flatten(path + QString("[%1]").arg(i), a[i], out);
    } else {
        out[path] = v;
    }
}

MessageTreeModel::MessageTreeModel(QObject *parent) : QAbstractItemModel(parent), m_root(std::make_unique<Node>()) {}

MessageTreeModel::~MessageTreeModel()
{
    updateSubscription(false);
}

void MessageTreeModel::setBridge(RosBridge *b)
{
    if (b == m_bridge)
        return;
    updateSubscription(false);
    if (m_bridge)
        disconnect(m_bridge, nullptr, this, nullptr);
    m_bridge = b;
    if (b)
        connect(b, &RosBridge::message, this, [this](const QString &topic, const QJsonObject &msg) {
            if (!m_topic.isEmpty() && topic == m_topic)
                setMessage(msg);
        });
    updateSubscription(!m_topic.isEmpty());
    emit bridgeChanged();
}

void MessageTreeModel::setTopic(const QString &t)
{
    if (t == m_topic)
        return;
    updateSubscription(false);
    m_topic = t;
    clear();
    updateSubscription(!t.isEmpty());
    emit topicChanged();
}

void MessageTreeModel::setCompare(bool on)
{
    if (on == m_compare)
        return;
    m_compare = on;
    emit compareChanged();
    std::function<void(Node *, const QModelIndex &)> touch = [&](Node *n, const QModelIndex &idx) {
        if (n->children.empty())
            return;
        emit dataChanged(index(0, 0, idx), index(int(n->children.size()) - 1, 2, idx), {ChangedRole});
        for (auto &c : n->children)
            touch(c.get(), index(c->row, 0, idx));
    };
    touch(m_root.get(), {});
}

void MessageTreeModel::updateSubscription(bool on)
{
    if (!on) {
        if (m_bridge && !m_subTopic.isEmpty())
            m_bridge->want(m_subTopic, {}, "inspect", false);
        m_subTopic.clear();
        return;
    }
    if (!m_bridge || !m_subTopic.isEmpty())
        return;
    QString type = m_bridge->topicTypes().value(m_topic);
    if (type.isEmpty())
        type = m_bridge->wantedType(m_topic);
    m_subTopic = m_topic;
    m_bridge->want(m_topic, type, "inspect", true, 200);
}

std::unique_ptr<MessageTreeModel::Node> MessageTreeModel::build(const QString &key, const QString &path,
                                                                 const QJsonValue &v, Node *parent, int row) const
{
    auto n = std::make_unique<Node>();
    n->key = key, n->path = path, n->type = typeName(v), n->text = valueText(v);
    n->numeric = v.isDouble();
    n->parent = parent, n->row = row;
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            n->children.push_back(build(it.key(), childPath(path, it.key()), it.value(), n.get(), int(n->children.size())));
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        const int shown = qMin<int>(a.size(), kArrayLimit);
        for (int i = 0; i < shown; ++i)
            n->children.push_back(build(QString("[%1]").arg(i), path + QString("[%1]").arg(i), a[i], n.get(), i));
        if (a.size() > shown) {
            auto m = std::make_unique<Node>();
            m->key = QString("… %1개 더").arg(a.size() - shown);
            m->more = true, m->parent = n.get(), m->row = shown;
            n->children.push_back(std::move(m));
        }
    }
    return n;
}

bool MessageTreeModel::sameShape(const Node *a, const Node *b) const
{
    if (a->key != b->key || a->type != b->type || a->children.size() != b->children.size())
        return false;
    for (size_t i = 0; i < a->children.size(); ++i)
        if (!sameShape(a->children[i].get(), b->children[i].get()))
            return false;
    return true;
}

void MessageTreeModel::copyValues(Node *dst, Node *src)
{
    for (size_t i = 0; i < dst->children.size(); ++i) {
        Node *d = dst->children[i].get(), *s = src->children[i].get();
        d->text = s->text, d->numeric = s->numeric;
        copyValues(d, s);
    }
}

void MessageTreeModel::computeDiff(const QJsonObject &prev, const QJsonObject &cur)
{
    m_changed.clear();
    m_diffCount = 0;
    if (prev.isEmpty())
        return;
    QHash<QString, QJsonValue> a, b;
    flatten(m_topic, prev, a);
    flatten(m_topic, cur, b);
    for (auto it = b.cbegin(); it != b.cend(); ++it) {
        if (a.value(it.key(), QJsonValue(QJsonValue::Undefined)) == it.value())
            continue;
        ++m_diffCount;
        // 잎과 그 조상(접힌 행에서도 보이게)을 칠한다
        QString p = it.key();
        while (!p.isEmpty() && p != m_topic && !m_changed.contains(p)) {
            m_changed.insert(p);
            const qsizetype cut = qMax(p.lastIndexOf('.'), p.lastIndexOf('['));
            if (cut <= 0)
                break;
            p = p.left(cut);
        }
    }
}

void MessageTreeModel::setMessage(const QJsonObject &msg)
{
    m_prev = m_cur;
    m_cur = msg;
    ++m_messageCount;
    computeDiff(m_prev, m_cur);
    auto root = build({}, m_topic, msg, nullptr, 0);
    if (!m_prev.isEmpty() && sameShape(root.get(), m_root.get())) {
        // 구조가 같으면 값만 바꿔 TreeView 의 펼침 상태를 지킨다
        copyValues(m_root.get(), root.get());
        std::function<void(Node *, const QModelIndex &)> touch = [&](Node *n, const QModelIndex &idx) {
            if (n->children.empty())
                return;
            emit dataChanged(index(0, 0, idx), index(int(n->children.size()) - 1, 2, idx));
            for (auto &c : n->children)
                touch(c.get(), index(c->row, 0, idx));
        };
        touch(m_root.get(), {});
    } else {
        beginResetModel();
        m_root = std::move(root);
        endResetModel();
    }
    emit messageChanged();
}

void MessageTreeModel::clear()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    endResetModel();
    m_cur = m_prev = {};
    m_changed.clear();
    m_diffCount = m_messageCount = 0;
    emit messageChanged();
}

QString MessageTreeModel::pathOf(const QModelIndex &idx) const
{
    return idx.isValid() ? static_cast<Node *>(idx.internalPointer())->path : QString();
}

void MessageTreeModel::copyPath(const QModelIndex &idx) const
{
    QGuiApplication::clipboard()->setText(pathOf(idx));
}

QModelIndex MessageTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    const Node *p = parent.isValid() ? static_cast<Node *>(parent.internalPointer()) : m_root.get();
    if (row < 0 || row >= int(p->children.size()) || column < 0 || column > 2)
        return {};
    return createIndex(row, column, p->children[row].get());
}

QModelIndex MessageTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    const Node *p = static_cast<Node *>(child.internalPointer())->parent;
    if (!p || p == m_root.get())
        return {};
    return createIndex(p->row, 0, const_cast<Node *>(p));
}

int MessageTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;
    const Node *p = parent.isValid() ? static_cast<Node *>(parent.internalPointer()) : m_root.get();
    return int(p->children.size());
}

int MessageTreeModel::columnCount(const QModelIndex &) const
{
    return 3;
}

QVariant MessageTreeModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid())
        return {};
    const Node *n = static_cast<Node *>(idx.internalPointer());
    switch (role) {
    case Qt::DisplayRole:
        if (n->more)
            return idx.column() == 0 ? n->key : QString();
        return idx.column() == 0 ? n->key : idx.column() == 1 ? n->text : n->type;
    case KeyRole: return n->key;
    case ValueRole: return n->more ? QString() : n->text;
    case TypeRole: return n->more ? QString() : n->type;
    case PathRole: return n->path;
    case ChangedRole: return m_compare && m_changed.contains(n->path);
    case NumericRole: return n->numeric;
    case MoreRole: return n->more;
    }
    return {};
}

QVariant MessageTreeModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char *h[] = {"키", "값", "형식"};
    return section >= 0 && section < 3 ? QString(h[section]) : QVariant();
}

QHash<int, QByteArray> MessageTreeModel::roleNames() const
{
    QHash<int, QByteArray> r = QAbstractItemModel::roleNames();
    r.insert({{KeyRole, "key"}, {ValueRole, "value"}, {TypeRole, "type"}, {PathRole, "path"},
              {ChangedRole, "changed"}, {NumericRole, "numeric"}, {MoreRole, "more"}});
    return r;
}
