#pragma once

#include "scanaccumulator.h"

#include <QPointer>
#include <QQuick3DInstancing>

// ScanAccumulator 의 점을 점마다 다른 색(알파 포함)의 작은 큐브 인스턴스로 그린다.
class ColorPointInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ScanAccumulator *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(float size READ size WRITE setSize NOTIFY changed)

public:
    explicit ColorPointInstancing(QQuick3DObject *parent = nullptr) : QQuick3DInstancing(parent)
    {
        setHasTransparency(true);   // 오래된 묶음은 알파가 낮다
    }

    ScanAccumulator *source() const { return m_source; }
    void setSource(ScanAccumulator *s);
    float size() const { return m_size; }
    void setSize(float s);

signals:
    void changed();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    QPointer<ScanAccumulator> m_source;
    QMetaObject::Connection m_conn;
    float m_size = 0.02f;   // 한 변 m
};
