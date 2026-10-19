#include "maptexture.h"

void MapTexture::setImage(const QImage &img)
{
    if (!img.isNull() && img.cacheKey() == m_sourceKey)   // 같은 이미지면 텍스처를 다시 올리지 않는다
        return;
    m_sourceKey = img.cacheKey();
    m_image = img.convertToFormat(QImage::Format_RGBA8888);
    if (m_image.isNull())
        return;
    setSize(m_image.size());
    setFormat(QQuick3DTextureData::RGBA8);
    setHasTransparency(true);
    setTextureData(QByteArray(reinterpret_cast<const char *>(m_image.constBits()), m_image.sizeInBytes()));
    emit imageChanged();
}
