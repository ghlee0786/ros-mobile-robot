#include "pointinstancing.h"

void PointInstancing::setPoints(const QList<QVector3D> &p)
{
    if (p == m_points)
        return;
    m_points = p;
    markDirty();
    emit pointsChanged();
}

void PointInstancing::setSize(float s)
{
    m_size = s;
    markDirty();
    emit changed();
}

void PointInstancing::setColor(const QColor &c)
{
    m_color = c;
    markDirty();
    emit changed();
}

QByteArray PointInstancing::getInstanceBuffer(int *instanceCount)
{
    QByteArray buf(m_points.size() * qsizetype(sizeof(InstanceTableEntry)), Qt::Uninitialized);
    auto *e = reinterpret_cast<InstanceTableEntry *>(buf.data());
    const float s = m_size / 100.0f;   // #Cube 는 100 단위
    for (const QVector3D &p : std::as_const(m_points))
        *e++ = calculateTableEntry(p, QVector3D(s, s, s), {}, m_color);
    *instanceCount = int(m_points.size());
    return buf;
}
