#include "amclpose.h"
#include "scenemath.h"

#include <QJsonArray>
#include <cmath>

AmclPose::AmclPose(QObject *parent) : SceneSource(parent) {}

void AmclPose::setTopic(const QString &t)
{
    if (t == m_topic)
        return;
    const bool on = active();
    setActive(false);
    m_topic = t;
    m_valid = false;
    setActive(on);
    emit topicChanged();
    emit poseChanged();
}

QList<SceneSource::Sub> AmclPose::topics() const
{
    const QString type = m_topic == "/odom" ? "nav_msgs/Odometry" : "geometry_msgs/PoseWithCovarianceStamped";
    return {{m_topic, type, 0}, {"/tf", "tf2_msgs/TFMessage", 200}};
}

void AmclPose::handle(const QString &topic, const QJsonObject &msg)
{
    if (topic != m_topic)
        return;
    const QJsonObject pc = msg["pose"].toObject(), pose = pc["pose"].toObject();
    const QJsonArray cov = pc["covariance"].toArray();
    if (cov.size() < 36)
        return;
    const QJsonObject p = pose["position"].toObject();
    double x = p["x"].toDouble(), y = p["y"].toDouble(), yaw = scene::yawOf(pose["orientation"].toObject());
    const double yaw0 = yaw;
    toOdom(msg["header"].toObject()["frame_id"].toString(), x, y, yaw);
    const scene::Ellipse e = scene::covEllipse(cov[0].toDouble(), cov[1].toDouble(), cov[7].toDouble());
    m_x = x, m_y = y, m_yaw = yaw;
    m_major = e.major, m_minor = e.minor, m_angle = e.angle + (yaw - yaw0);
    m_yawSpread = 2 * std::sqrt(qMax(0.0, cov[35].toDouble()));
    m_valid = false;
    for (const QJsonValue &v : cov)
        m_valid = m_valid || v.toDouble() != 0;
    setReceived(true);
    emit poseChanged();
}
