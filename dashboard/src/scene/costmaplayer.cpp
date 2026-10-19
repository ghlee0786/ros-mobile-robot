#include "costmaplayer.h"
#include "msgparse.h"
#include "scenemath.h"

#include <QJsonArray>
#include <cmath>

namespace {
const char *kFootprint = "/local_costmap/published_footprint";
constexpr float kFootprintZ = 0.015f;
}

CostmapLayer::CostmapLayer(QObject *parent) : SceneSource(parent) {}

QColor CostmapLayer::costColor(int v)
{
    if (v == 0)
        return QColor(0, 0, 0, 0);
    if (v < 0)
        return QColor(0x70, 0x89, 0x86);   // 알 수 없음
    if (v >= 100)
        return QColor(255, 0, 255);        // 치명(lethal)
    if (v == 99)
        return QColor(0, 255, 255);        // 내접(inscribed)
    const int c = 255 * v / 100;
    return QColor(c, 0, 255 - c);
}

QImage CostmapLayer::toImage(int w, int h, const QJsonArray &data)
{
    // 팔레트는 msgparse 의 costmap 과 같다(costColor 는 시험 · 범례용으로 남김). 지도 0 행은 아래쪽.
    return msgparse::occupancyToImage(data, w, h, QStringLiteral("costmap")).convertToFormat(QImage::Format_ARGB32);
}

void CostmapLayer::setTopic(const QString &t)
{
    if (t == m_topic)
        return;
    const bool on = active();
    setActive(false);
    m_topic = t;
    setActive(on);
    emit topicChanged();
}

void CostmapLayer::setThrottleMs(int ms)
{
    if (ms == m_throttle)
        return;
    const bool on = active();
    setActive(false);
    m_throttle = ms;
    setActive(on);
    emit topicChanged();
}

QList<SceneSource::Sub> CostmapLayer::topics() const
{
    return {{m_topic, "nav_msgs/OccupancyGrid", m_throttle}, {"/tf", "tf2_msgs/TFMessage", 200}};
}

void CostmapLayer::handle(const QString &topic, const QJsonObject &m)
{
    if (topic != m_topic)
        return;
    const QJsonObject info = m["info"].toObject();
    const int w = info["width"].toInt(), h = info["height"].toInt();
    const double res = info["resolution"].toDouble();
    const QJsonArray data = m["data"].toArray();
    if (w <= 0 || h <= 0 || data.size() < qsizetype(w) * h)
        return;
    const QJsonObject o = info["origin"].toObject();
    double yaw = scene::yawOf(o["orientation"].toObject());
    const double hw = w * res / 2, hh = h * res / 2;
    double x = o["position"].toObject()["x"].toDouble() + std::cos(yaw) * hw - std::sin(yaw) * hh;
    double y = o["position"].toObject()["y"].toDouble() + std::sin(yaw) * hw + std::cos(yaw) * hh;
    toOdom(m["header"].toObject()["frame_id"].toString(), x, y, yaw);
    m_image = toImage(w, h, data);
    m_center = QVector3D(float(x), float(y), kZ);
    m_yaw = qRadiansToDegrees(yaw);
    m_size = QSizeF(w * res, h * res);
    setReceived(true);
    emit mapChanged();
}

QList<SceneSource::Sub> FootprintLayer::topics() const
{
    return {{kFootprint, "geometry_msgs/PolygonStamped", 0}, {"/tf", "tf2_msgs/TFMessage", 200}};
}

void FootprintLayer::handle(const QString &topic, const QJsonObject &m)
{
    if (topic != kFootprint)
        return;
    const QString frame = m["header"].toObject()["frame_id"].toString();
    QList<QVector3D> corners;
    for (const QJsonValue &v : m["polygon"].toObject()["points"].toArray()) {
        double x = v["x"].toDouble(), y = v["y"].toDouble(), yaw = 0;
        toOdom(frame, x, y, yaw);
        corners.append(QVector3D(float(x), float(y), kFootprintZ));
    }
    m_points.clear();
    if (!corners.isEmpty()) {
        m_points.append(corners.first());
        for (qsizetype i = 0; i < corners.size(); ++i)   // 마지막 꼭짓점에서 처음으로 닫는다
            scene::appendDense(m_points, corners[i], corners[(i + 1) % corners.size()]);
    }
    setReceived(true);
    emit pointsChanged();
}
