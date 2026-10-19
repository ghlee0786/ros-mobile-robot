#include "colorpointinstancing.h"

void ColorPointInstancing::setSource(ScanAccumulator *s)
{
    disconnect(m_conn);
    m_source = s;
    if (s)
        m_conn = connect(s, &ScanAccumulator::pointsChanged, this, [this] { markDirty(); });
    markDirty();
    emit changed();
}

void ColorPointInstancing::setSize(float s)
{
    m_size = s;
    markDirty();
    emit changed();
}

void ColorPointInstancing::setPoints(const QList<QVector3D> &p)
{
    m_points = p;
    markDirty();
    emit changed();
}

void ColorPointInstancing::setColor(const QColor &c)
{
    m_color = c;
    markDirty();
    emit changed();
}

QByteArray ColorPointInstancing::getInstanceBuffer(int *instanceCount)
{
    const float s = m_size / 100.0f;   // #Cube 는 100 단위
    if (!m_source) {
        const qsizetype n = m_points.size();
        QByteArray buf(n * qsizetype(sizeof(InstanceTableEntry)), Qt::Uninitialized);
        auto *e = reinterpret_cast<InstanceTableEntry *>(buf.data());
        QColor c = m_color;
        for (qsizetype i = 0; i < n; ++i) {
            c.setAlphaF(ScanAccumulator::kMinAlpha + (1 - ScanAccumulator::kMinAlpha) * float(i + 1) / float(n));
            *e++ = calculateTableEntry(m_points[i], QVector3D(s, s, s), {}, c);
        }
        *instanceCount = int(n);
        return buf;
    }
    const qsizetype n = qMin(m_source->points().size(), m_source->colors().size());
    QByteArray buf(n * qsizetype(sizeof(InstanceTableEntry)), Qt::Uninitialized);
    auto *e = reinterpret_cast<InstanceTableEntry *>(buf.data());
    for (qsizetype i = 0; i < n; ++i)
        *e++ = calculateTableEntry(m_source->points()[i], QVector3D(s, s, s), {}, m_source->colors()[i]);
    *instanceCount = int(n);
    return buf;
}
