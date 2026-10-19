#pragma once

#include "scenesource.h"

// 공분산이 붙은 자세(/amcl_pose: PoseWithCovarianceStamped, /odom: Odometry — 둘 다 pose.pose · pose.covariance)를
// odom 프레임으로 바꿔 고스트 화살표 · 2σ 타원 · yaw ±2σ 부채꼴 값으로 내놓는다.
class AmclPose : public SceneSource
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString topic READ topic WRITE setTopic NOTIFY topicChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY poseChanged)          // 공분산이 0 이 아닌 자세를 받았는지
    Q_PROPERTY(double x READ x NOTIFY poseChanged)                // odom 프레임 m
    Q_PROPERTY(double y READ y NOTIFY poseChanged)
    Q_PROPERTY(double yaw READ yaw NOTIFY poseChanged)            // rad
    Q_PROPERTY(double axisMajor READ axisMajor NOTIFY poseChanged)        // 2σ 타원 긴 반축 m
    Q_PROPERTY(double axisMinor READ axisMinor NOTIFY poseChanged)        // 2σ 타원 짧은 반축 m
    Q_PROPERTY(double angle READ angle NOTIFY poseChanged)        // 긴 축 방향 rad (odom 프레임)
    Q_PROPERTY(double yawSpread READ yawSpread NOTIFY poseChanged)   // yaw 2σ rad (부채꼴 반각)

public:
    explicit AmclPose(QObject *parent = nullptr);

    QString topic() const { return m_topic; }
    void setTopic(const QString &t);
    bool valid() const { return m_valid; }
    double x() const { return m_x; }
    double y() const { return m_y; }
    double yaw() const { return m_yaw; }
    double axisMajor() const { return m_major; }
    double axisMinor() const { return m_minor; }
    double angle() const { return m_angle; }
    double yawSpread() const { return m_yawSpread; }

signals:
    void topicChanged();
    void poseChanged();

protected:
    QList<Sub> topics() const override;
    void handle(const QString &topic, const QJsonObject &msg) override;

private:
    QString m_topic = QStringLiteral("/amcl_pose");
    bool m_valid = false;
    double m_x = 0, m_y = 0, m_yaw = 0, m_major = 0, m_minor = 0, m_angle = 0, m_yawSpread = 0;
};
