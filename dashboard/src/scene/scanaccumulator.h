#pragma once

#include "scenesource.h"
#include "msgparse.h"

#include <QColor>
#include <QTimer>
#include <QVector3D>

// LiDAR 잔상: /scan 을 받은 시점의 로봇 자세(QML 이 robot.x · y · theta 를 바인딩)로 odom 프레임에 고정해
// decaySec 동안 묶음 단위로 쌓는다. RViz 처럼 마지막 묶음은 시간이 지나도 남긴다. 점은 최대 kMaxPoints 개.
// /scan 구독은 Robot 의 LiDAR 센서가 맡으므로 여기서는 받기만 한다. frame_id 가 base_link 가 아니면
// 함께 지나가는 /tf_static 의 base_link → 레이저 장착 변환을 적용한다(Robot::onScan 과 같음).
class ScanAccumulator : public SceneSource
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double robotX MEMBER m_rx NOTIFY poseChanged)
    Q_PROPERTY(double robotY MEMBER m_ry NOTIFY poseChanged)
    Q_PROPERTY(double robotTheta MEMBER m_rt NOTIFY poseChanged)
    Q_PROPERTY(double decaySec READ decaySec WRITE setDecaySec NOTIFY settingsChanged)
    Q_PROPERTY(QString colorMode READ colorMode WRITE setColorMode NOTIFY settingsChanged)   // flat · range · intensity
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY settingsChanged)                // flat 색
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointsChanged)

public:
    static constexpr int kMaxPoints = 20000;
    static constexpr double kSelfHide = 0.3;   // 로봇 몸체에 맞은 점(m 이내)은 숨김(Robot 과 같음)
    static constexpr float kMinAlpha = 0.15f;  // 가장 오래된 묶음의 알파

    explicit ScanAccumulator(QObject *parent = nullptr);

    double decaySec() const { return m_decay; }
    void setDecaySec(double s);
    QString colorMode() const { return m_mode; }
    void setColorMode(const QString &m);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    int pointCount() const { return int(m_points.size()); }
    const QList<QVector3D> &points() const { return m_points; }
    const QList<QColor> &colors() const { return m_colors; }

    void addScan(const QJsonObject &scan, qint64 nowMs);
    void prune(qint64 nowMs);   // 만료된 묶음을 버리고 점 · 색을 다시 만든다

signals:
    void poseChanged();
    void settingsChanged();
    void pointsChanged();

protected:
    void handle(const QString &topic, const QJsonObject &msg) override;

private:
    struct Batch { qint64 t; QList<QVector3D> pts; QList<float> range, intensity; };
    void rebuild(qint64 nowMs);

    double m_rx = 0, m_ry = 0, m_rt = 0, m_decay = 0;
    QString m_mode = QStringLiteral("flat");
    QColor m_color = Qt::white;
    QList<Batch> m_batches;   // 오래된 것부터
    QHash<QString, msgparse::Pose2D> m_mounts;   // base_link → 레이저 프레임
    QList<QVector3D> m_points;
    QList<QColor> m_colors;
    QTimer m_tick;
};
