#pragma once

#include "scenesource.h"

#include <QImage>
#include <QJsonArray>
#include <QSizeF>
#include <QVector3D>

// Nav2 코스트맵(nav_msgs/OccupancyGrid)을 RViz costmap 팔레트의 ARGB 이미지로 만들고(0 은 투명)
// 지도 판처럼 odom 프레임에 놓는다. 이미지 0 행이 지도 맨 위 행이다(Robot::mapImage 와 같은 방향).
// 팔레트는 P4 msgparse::occupancyToImage(costmap) 와 겹치지 않게 여기 작게 둔다(나중에 그쪽으로 바꿀 수 있음).
class CostmapLayer : public SceneSource
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString topic READ topic WRITE setTopic NOTIFY topicChanged)
    Q_PROPERTY(int throttleMs READ throttleMs WRITE setThrottleMs NOTIFY topicChanged)
    Q_PROPERTY(QImage image READ image NOTIFY mapChanged)
    Q_PROPERTY(QVector3D center READ center NOTIFY mapChanged)   // 판 중심(odom 프레임, z 는 지도보다 0.002 위)
    Q_PROPERTY(double yaw READ yaw NOTIFY mapChanged)            // deg (Robot::mapYaw 와 같은 단위)
    Q_PROPERTY(QSizeF size READ size NOTIFY mapChanged)          // m

public:
    static constexpr float kZ = 0.005f;   // Robot 지도 판 z(0.003) + 0.002

    static QColor costColor(int v);   // RViz costmap 팔레트
    static QImage toImage(int width, int height, const QJsonArray &data);

    explicit CostmapLayer(QObject *parent = nullptr);

    QString topic() const { return m_topic; }
    void setTopic(const QString &t);
    int throttleMs() const { return m_throttle; }
    void setThrottleMs(int ms);
    QImage image() const { return m_image; }
    QVector3D center() const { return m_center; }
    double yaw() const { return m_yaw; }
    QSizeF size() const { return m_size; }

signals:
    void topicChanged();
    void mapChanged();

protected:
    QList<Sub> topics() const override;
    void handle(const QString &topic, const QJsonObject &msg) override;

private:
    QString m_topic = QStringLiteral("/global_costmap/costmap");
    int m_throttle = 1000;
    QImage m_image;
    QVector3D m_center;
    double m_yaw = 0;
    QSizeF m_size;
};

// /local_costmap/published_footprint(geometry_msgs/PolygonStamped)를 odom 프레임의 닫힌 선(촘촘한 점)으로.
class FootprintLayer : public SceneSource
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<QVector3D> points READ points NOTIFY pointsChanged)

public:
    explicit FootprintLayer(QObject *parent = nullptr) : SceneSource(parent) {}
    QList<QVector3D> points() const { return m_points; }

signals:
    void pointsChanged();

protected:
    QList<Sub> topics() const override;
    void handle(const QString &topic, const QJsonObject &msg) override;

private:
    QList<QVector3D> m_points;
};
