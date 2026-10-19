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

QByteArray ColorPointInstancing::getInstanceBuffer(int *instanceCount)
{
    const qsizetype n = m_source ? qMin(m_source->points().size(), m_source->colors().size()) : 0;
    QByteArray buf(n * qsizetype(sizeof(InstanceTableEntry)), Qt::Uninitialized);
    auto *e = reinterpret_cast<InstanceTableEntry *>(buf.data());
    const float s = m_size / 100.0f;   // #Cube 는 100 단위
    for (qsizetype i = 0; i < n; ++i)
        *e++ = calculateTableEntry(m_source->points()[i], QVector3D(s, s, s), {}, m_source->colors()[i]);
    *instanceCount = int(n);
    return buf;
}
