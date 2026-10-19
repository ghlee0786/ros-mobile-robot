#include "scenesource.h"
#include "msgparse.h"
#include "scenemath.h"

#include <QDateTime>
#include <QJsonArray>

SceneSource::SceneSource(QObject *parent)
    : QObject(parent), m_reason(QStringLiteral("scene:%1").arg(quintptr(this), 0, 16))
{
}

qint64 SceneSource::now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

void SceneSource::setBridge(RosBridge *b)
{
    if (b == m_bridge)
        return;
    want(false);
    disconnect(m_conn);
    m_bridge = b;
    if (b)
        m_conn = connect(b, &RosBridge::message, this, [this](const QString &t, const QJsonObject &m) {
            if (m_active)
                feed(t, m);
        });
    want(m_active);
    emit bridgeChanged();
}

void SceneSource::setActive(bool on)
{
    if (on == m_active)
        return;
    want(false);
    m_active = on;
    want(on);
    if (!on)
        setReceived(false);
    emit activeChanged();
}

void SceneSource::want(bool on)
{
    if (!m_bridge || !m_active)
        return;
    for (const Sub &s : topics())
        m_bridge->want(s.topic, s.type, m_reason, on, s.throttleMs);
}

void SceneSource::setReceived(bool on)
{
    if (on == m_received)
        return;
    m_received = on;
    emit receivedChanged();
}

void SceneSource::feed(const QString &topic, const QJsonObject &msg)
{
    // map→odom 은 /tf 와 PC 내비 스택의 /map_odom(같은 TFMessage) 둘 다로 온다(Robot 과 같음).
    if (topic == "/tf" || topic == "/map_odom") {
        for (const QJsonValue &v : msg["transforms"].toArray()) {
            const QJsonObject t = v.toObject();
            if (t["header"].toObject()["frame_id"].toString() != "map" || t["child_frame_id"].toString() != "odom")
                continue;
            const QJsonObject tr = t["transform"].toObject();
            m_moX = tr["translation"].toObject()["x"].toDouble();
            m_moY = tr["translation"].toObject()["y"].toDouble();
            m_moYaw = scene::yawOf(tr["rotation"].toObject());
            m_haveMapOdom = true;
        }
    }
    handle(topic, msg);
}

// p_odom = (map→odom)^-1 * p_map
void SceneSource::toOdom(const QString &frame, double &x, double &y, double &yaw) const
{
    if (frame != "map" || !m_haveMapOdom)
        return;
    const msgparse::Pose2D p = msgparse::mapToOdom({m_moX, m_moY, m_moYaw}, {x, y, yaw});
    x = p.x, y = p.y, yaw = p.yaw;
}
