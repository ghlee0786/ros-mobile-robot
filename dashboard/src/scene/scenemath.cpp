#include "scenemath.h"

#include <cmath>

namespace scene {

double yawOf(const QJsonObject &q)
{
    const double x = q["x"].toDouble(), y = q["y"].toDouble(), z = q["z"].toDouble(), w = q["w"].toDouble(1);
    return std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z));
}

QColor rainbow(double value)
{
    value = qBound(0.0, value, 1.0);
    const double h = value * 5 + 1;
    const int i = int(std::floor(h));
    double f = h - i;
    if (!(i & 1))
        f = 1 - f;
    const double n = 1 - f;
    if (i <= 1)
        return QColor::fromRgbF(float(n), 0, 1);
    if (i == 2)
        return QColor::fromRgbF(0, float(n), 1);
    if (i == 3)
        return QColor::fromRgbF(0, 1, float(n));
    if (i == 4)
        return QColor::fromRgbF(float(n), 1, 0);
    return QColor::fromRgbF(1, float(n), 0);
}

Ellipse covEllipse(double a, double b, double c)
{
    const double m = (a + c) / 2, d = std::sqrt(((a - c) / 2) * ((a - c) / 2) + b * b);
    return {2 * std::sqrt(qMax(0.0, m + d)), 2 * std::sqrt(qMax(0.0, m - d)), 0.5 * std::atan2(2 * b, a - c)};
}

void appendDense(QList<QVector3D> &out, QVector3D a, QVector3D b, float step)
{
    const int n = qMax(1, int((b - a).length() / step));
    for (int i = 1; i <= n; ++i)
        out.append(a + (b - a) * (float(i) / n));
}

}
