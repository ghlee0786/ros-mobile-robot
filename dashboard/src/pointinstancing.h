#pragma once

#include <QColor>
#include <QQuick3DInstancing>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

// 점 목록을 같은 크기 · 색의 인스턴스로 그린다(LiDAR 점, 이동 경로, 목표 경로).
// D3D11 백엔드는 점 크기 · 선 굵기를 지원하지 않아 작은 큐브 인스턴스로 대신한다.
class PointInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<QVector3D> points READ points WRITE setPoints NOTIFY pointsChanged)
    Q_PROPERTY(float size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)

public:
    explicit PointInstancing(QQuick3DObject *parent = nullptr) : QQuick3DInstancing(parent) {}

    QList<QVector3D> points() const { return m_points; }
    void setPoints(const QList<QVector3D> &p);
    float size() const { return m_size; }
    void setSize(float s);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);

signals:
    void pointsChanged();
    void changed();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    QList<QVector3D> m_points;
    float m_size = 0.02f;   // 한 변 m
    QColor m_color = Qt::white;
};
