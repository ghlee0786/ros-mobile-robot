#include "tftree.h"

#include <QJsonArray>

TfTree::TfTree(QObject *parent) : SceneSource(parent)
{
    m_reason = QStringLiteral("tfview");
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(now()); });
    connect(this, &SceneSource::activeChanged, this, [this] {
        if (active())
            m_tick.start();
        else {
            m_tick.stop();
            m_frames.removeIf([](const QHash<QString, Frame>::iterator &it) { return !it->isStatic; });
            update(now());
        }
    });
    connect(this, &SceneSource::bridgeChanged, this, [this] {
        disconnect(m_staticConn);
        if (bridge())
            m_staticConn = connect(bridge(), &RosBridge::message, this, [this](const QString &t, const QJsonObject &m) {
                if (!active() && t == "/tf_static")   // 켜져 있으면 feed 가 받는다
                    ingest(m, true, now());
            });
    });
}

// /tf 는 파이 rosbridge 부담을 줄이려 200 ms, /tf_static 은 queue 1 에서 여러 변환이 사라지지 않게 전속(G7).
QList<SceneSource::Sub> TfTree::topics() const
{
    return {{"/tf", "tf2_msgs/TFMessage", 200}, {"/tf_static", "tf2_msgs/TFMessage", 0}};
}

void TfTree::setFixedFrame(const QString &f)
{
    m_fixed = f;
    update(now());
    emit settingsChanged();
}

TfTree::Fade TfTree::fade(double age, double timeout, bool isStatic)
{
    if (isStatic || timeout <= 0)
        return {};
    const double third = timeout / 3;
    if (age < third)
        return {};
    if (age < 2 * third)
        return {(age - third) / third, 1};
    return {1, qMax(0.0, 1 - (age - 2 * third) / third)};
}

void TfTree::handle(const QString &topic, const QJsonObject &msg)
{
    if (topic == "/tf" || topic == "/tf_static")
        ingest(msg, topic == "/tf_static", now());
}

void TfTree::ingest(const QJsonObject &msg, bool isStatic, qint64 nowMs)
{
    for (const QJsonValue &v : msg["transforms"].toArray()) {
        const QJsonObject t = v.toObject(), tr = t["transform"].toObject();
        const QJsonObject p = tr["translation"].toObject(), r = tr["rotation"].toObject();
        Frame &f = m_frames[t["child_frame_id"].toString()];
        f.parent = t["header"].toObject()["frame_id"].toString();
        f.tf = {QVector3D(float(p["x"].toDouble()), float(p["y"].toDouble()), float(p["z"].toDouble())),
                QQuaternion(float(r["w"].toDouble(1)), float(r["x"].toDouble()), float(r["y"].toDouble()),
                            float(r["z"].toDouble())).normalized()};
        f.recv = nowMs;
        f.isStatic = isStatic;
    }
    setReceived(active() && !m_frames.isEmpty());
}

TfTree::Tf TfTree::mul(const Tf &a, const Tf &b)
{
    return {a.t + a.q.rotatedVector(b.t), a.q * b.q};
}

TfTree::Tf TfTree::inv(const Tf &a)
{
    const QQuaternion qi = a.q.conjugated();
    return {-qi.rotatedVector(a.t), qi};
}

// 부모를 따라 올라가 뿌리 프레임 이름과 뿌리 → name 변환을 구한다(고리는 64 단계에서 끊음).
QString TfTree::rootOf(const QString &name, Tf &rootToName) const
{
    rootToName = {};
    QString cur = name;
    for (int i = 0; i < 64; ++i) {
        const auto it = m_frames.constFind(cur);
        if (it == m_frames.cend())
            return cur;
        rootToName = mul(it->tf, rootToName);
        cur = it->parent;
    }
    return {};
}

void TfTree::update(qint64 nowMs)
{
    QSet<QString> names;
    for (auto it = m_frames.cbegin(); it != m_frames.cend(); ++it)
        names << it.key() << it->parent;
    Tf rootToFixed;
    const QString fixedRoot = rootOf(m_fixed, rootToFixed);
    const Tf fixedFromRoot = inv(rootToFixed);

    QHash<QString, QVector3D> pos;
    QHash<QString, QQuaternion> rot;
    for (const QString &n : std::as_const(names)) {
        Tf rootToN;
        if (rootOf(n, rootToN) != fixedRoot || fixedRoot.isEmpty())
            continue;   // 고정 프레임과 이어지지 않은 프레임은 그리지 않는다
        const Tf w = mul(fixedFromRoot, rootToN);
        pos[n] = w.t;
        rot[n] = w.q;
    }

    QStringList sorted = pos.keys();
    sorted.sort();
    m_list.clear();
    for (const QString &n : std::as_const(sorted)) {
        const auto it = m_frames.constFind(n);
        const bool known = it != m_frames.cend();
        const bool isStatic = !known || it->isStatic;   // 뿌리 프레임은 페이드하지 않는다
        const double age = known ? (nowMs - it->recv) / 1000.0 : 0;
        const Fade f = fade(age, m_timeout, isStatic);
        const QString parent = known ? it->parent : QString();
        m_list.append(QVariantMap{{"name", n}, {"parent", parent}, {"linked", pos.contains(parent)},
                                  {"position", pos[n]}, {"rotation", rot[n]},
                                  {"parentPosition", pos.value(parent, pos[n])}, {"isStatic", isStatic},
                                  {"age", age}, {"gray", f.gray}, {"alpha", f.alpha}});
    }
    emit framesChanged();
}
