#pragma once

#include "scanaccumulator.h"

#include <QPointer>
#include <QQuick3DInstancing>

// ScanAccumulator 의 점을 점마다 다른 색(알파 포함)의 작은 큐브 인스턴스로 그린다.
// source 가 없으면 points 를 color 로 그리되, 앞(오래된 쪽)일수록 알파를 낮춘다(이동 경로 그라디언트).
class ColorPointInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ScanAccumulator *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(float size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(QList<QVector3D> points READ points WRITE setPoints NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)

public:
    explicit ColorPointInstancing(QQuick3DObject *parent = nullptr) : QQuick3DInstancing(parent)
    {
        setHasTransparency(true);   // 오래된 묶음은 알파가 낮다
    }

    ScanAccumulator *source() const { return m_source; }
    void setSource(ScanAccumulator *s);
    float size() const { return m_size; }
    void setSize(float s);
    QList<QVector3D> points() const { return m_points; }
    void setPoints(const QList<QVector3D> &p);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);

signals:
    void changed();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    QPointer<ScanAccumulator> m_source;
    QMetaObject::Connection m_conn;
    float m_size = 0.02f;   // 한 변 m
    QList<QVector3D> m_points;
    QColor m_color = Qt::white;
};
