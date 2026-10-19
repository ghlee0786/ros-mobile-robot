#pragma once

#include <QImage>
#include <QQuick3DTextureData>
#include <QtQml/qqmlregistration.h>

// OccupancyGrid 를 그린 QImage 를 3D 텍스처로 올린다.
class MapTexture : public QQuick3DTextureData
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)

public:
    explicit MapTexture(QQuick3DObject *parent = nullptr) : QQuick3DTextureData(parent) {}
    QImage image() const { return m_image; }
    void setImage(const QImage &img);

signals:
    void imageChanged();

private:
    QImage m_image;
    qint64 m_sourceKey = 0;
};
