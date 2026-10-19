#include "msgparse.h"

#include <QtMath>
#include <cmath>

namespace msgparse {

double yawOf(const QJsonObject &q)
{
    const double x = q["x"].toDouble(), y = q["y"].toDouble(), z = q["z"].toDouble(), w = q["w"].toDouble(1);
    return std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z));
}

QJsonObject quatFromYaw(double yaw)
{
    return {{"x", 0}, {"y", 0}, {"z", std::sin(yaw / 2)}, {"w", std::cos(yaw / 2)}};
}

Pose2D transform2D(const Pose2D &t, const Pose2D &p)
{
    const double c = std::cos(t.yaw), s = std::sin(t.yaw);
    return {t.x + c * p.x - s * p.y, t.y + s * p.x + c * p.y, t.yaw + p.yaw};
}

Pose2D mapToOdom(const Pose2D &mo, const Pose2D &p)
{
    const double dx = p.x - mo.x, dy = p.y - mo.y, c = std::cos(mo.yaw), s = std::sin(mo.yaw);
    return {c * dx + s * dy, -s * dx + c * dy, p.yaw - mo.yaw};
}

Pose2D odomToMap(const Pose2D &mo, const Pose2D &p) { return transform2D(mo, p); }

Scan parseScan(const QJsonObject &m, const Pose2D &mount, double selfHide)
{
    Scan out;
    const QJsonArray ranges = m["ranges"].toArray();
    const double a0 = m["angle_min"].toDouble(), da = m["angle_increment"].toDouble();
    const double rmin = qMax(m["range_min"].toDouble(), selfHide), rmax = m["range_max"].toDouble(1e9);
    out.points.reserve(ranges.size());
    for (int i = 0; i < ranges.size(); ++i) {
        const double r = ranges[i].toDouble(qQNaN());   // inf/NaN(rosbridge null) · /scan_compact 0 은 NaN → 버림
        if (!(r >= rmin && r <= rmax))
            continue;
        const double a = a0 + i * da;
        const Pose2D p = transform2D(mount, {r * std::cos(a), r * std::sin(a), 0});
        out.points.append(QVector3D(float(p.x), float(p.y), 0));
        if (out.nearestRange == 0 || r < out.nearestRange)
            out.nearestRange = r, out.nearestAngle = qRadiansToDegrees(std::atan2(p.y, p.x));
    }
    out.scanTime = m["scan_time"].toDouble();
    return out;
}

QJsonObject scanFromCompact(const QJsonObject &m)
{
    const QJsonArray dims = m["layout"].toObject()["dim"].toArray();
    const QStringList f = dims.isEmpty() ? QStringList() : dims[0].toObject()["label"].toString().split(' ');
    if (f.size() < 7)
        return {};
    const qsizetype n = f.size() - 6;   // frame_id 가 비었거나 공백을 품어도 뒤 6 칸은 숫자
    const QJsonArray data = m["data"].toArray();
    QJsonArray ranges;
    for (const QJsonValue &v : data)
        ranges.append(v.toInt() > 0 ? QJsonValue(v.toInt() / 1000.0) : QJsonValue(QJsonValue::Null));
    const double a0 = f[n].toDouble(), da = f[n + 1].toDouble();
    return {{"header", QJsonObject{{"frame_id", f.mid(0, n).join(' ')},
                                   {"stamp", QJsonObject{{"sec", f[n + 4].toLongLong()}, {"nanosec", f[n + 5].toLongLong()}}}}},
            {"angle_min", a0}, {"angle_increment", da}, {"angle_max", a0 + (data.size() - 1) * da},
            {"range_min", f[n + 2].toDouble()}, {"range_max", f[n + 3].toDouble()}, {"ranges", ranges}};
}

namespace {

struct Rgba { uchar r, g, b, a; };

// rviz 와 같은 색: 101..127 초록, 128..254 빨강→노랑, 255(-1) 미확인
bool commonColor(int u, Rgba &c)
{
    if (u >= 101 && u <= 127)
        return c = {0, 255, 0, 255}, true;
    if (u >= 128 && u <= 254)
        return c = {255, uchar((u - 128) * 255 / 126), 0, 255}, true;
    if (u == 255)
        return c = {0x70, 0x89, 0x86, 255}, true;
    return false;
}

Rgba cellColor(int v, int scheme, int binaryThreshold)
{
    const int u = v & 0xFF;   // int8 를 rviz 처럼 0..255 로 본다(-1 → 255)
    Rgba c{0, 0, 0, 255};
    if (scheme == 2)   // raw
        return {uchar(u), uchar(u), uchar(u), 255};
    if (binaryThreshold >= 0 && u <= 100) {
        const uchar g = u >= binaryThreshold ? 0 : 255;
        return {g, g, g, 255};
    }
    if (commonColor(u, c))
        return c;
    if (scheme == 1) {   // costmap
        if (u == 0)
            return {0, 0, 0, 0};
        if (u == 99)
            return {0, 255, 255, 255};
        if (u == 100)
            return {255, 0, 255, 255};
        const uchar s = uchar(255 * u / 100);
        return {s, 0, uchar(255 - s), 255};
    }
    const uchar g = uchar(255 - 255 * u / 100);   // map: 0 흰색 → 100 검정
    return {g, g, g, 255};
}

} // namespace

QImage occupancyToImage(const QJsonArray &data, int w, int h, const QString &scheme, int binaryThreshold)
{
    if (w <= 0 || h <= 0 || data.size() < qsizetype(w) * h)
        return {};
    const int sch = scheme == "costmap" ? 1 : scheme == "raw" ? 2 : 0;
    // 값 256 가지의 색을 미리 만들어 둔다.
    Rgba lut[256];
    for (int u = 0; u < 256; ++u)
        lut[u] = cellColor(u, sch, binaryThreshold);
    QImage img(w, h, QImage::Format_RGBA8888);
    auto it = data.begin();
    for (int r = 0; r < h; ++r) {   // 지도 0 행은 아래쪽
        auto *line = reinterpret_cast<Rgba *>(img.scanLine(h - 1 - r));
        for (int c = 0; c < w; ++c, ++it)
            line[c] = lut[(*it).toInt() & 0xFF];
    }
    return img;
}

namespace {
Pose2D gridOrigin(const QJsonObject &info)
{
    const QJsonObject o = info["origin"].toObject(), pos = o["position"].toObject();
    return {pos["x"].toDouble(), pos["y"].toDouble(), yawOf(o["orientation"].toObject())};
}
}

GridPlane gridPlane(const QJsonObject &info)
{
    const double res = info["resolution"].toDouble();
    const double w = info["width"].toInt() * res, h = info["height"].toInt() * res;
    // 판 중심 = origin 에서 (폭/2, 높이/2) 만큼(origin 회전 적용)
    return {transform2D(gridOrigin(info), {w / 2, h / 2, 0}), w, h};
}

Pose2D gridCellCenter(const QJsonObject &info, int i, int j)
{
    const double res = info["resolution"].toDouble();
    return transform2D(gridOrigin(info), {(i + 0.5) * res, (j + 0.5) * res, 0});
}

QPointF planeUv(const GridPlane &plane, double x, double y)
{
    const double dx = x - plane.center.x, dy = y - plane.center.y;
    const double c = std::cos(plane.center.yaw), s = std::sin(plane.center.yaw);
    const double lx = c * dx + s * dy, ly = -s * dx + c * dy;   // 판 좌표(판 중심 기준, origin 축)
    return {lx / plane.width + 0.5, 0.5 - ly / plane.height};   // 이미지 위 = 판의 +y
}

QJsonArray initialPoseCovariance()
{
    QJsonArray a;
    for (int i = 0; i < 36; ++i)
        a.append(i == 0 || i == 7 ? 0.25 : i == 35 ? (M_PI / 12) * (M_PI / 12) : 0.0);
    return a;
}

} // namespace msgparse

namespace msgparse {

QString shortType(QString t) { return t.replace("/msg/", "/"); }

void appendDense(QList<QVector3D> &out, QVector3D a, QVector3D b)
{
    const int n = qMax(1, int((b - a).length() / 0.02f));
    for (int i = 1; i <= n; ++i)
        out.append(a + (b - a) * (float(i) / n));
}

void flatten(const QJsonValue &v, const QString &key, QHash<QString, QString> &out)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            flatten(it.value(), key.isEmpty() ? it.key() : key + "." + it.key(), out);
    } else if (v.isArray()) {
        const QJsonArray a = v.toArray();
        if (!a.isEmpty() && a.first().isObject()) {
            for (int i = 0; i < a.size(); ++i)
                flatten(a[i], key + QString("[%1]").arg(i), out);
        } else {
            QStringList parts;
            for (const QJsonValue &e : a)
                parts << (e.isString() ? e.toString() : e.isNull() ? "nan" : QString::number(e.toDouble(), 'g', 9));
            out[key] = parts.join(' ');
        }
    } else if (v.isString()) {
        out[key] = v.toString();
    } else if (v.isBool()) {
        out[key] = v.toBool() ? "1" : "0";
    } else if (v.isNull()) {
        out[key] = "nan";
    } else {
        out[key] = QString::number(v.toDouble(), 'g', 12);
    }
}

QString csvCell(QString s)
{
    if (s.contains(',') || s.contains('"') || s.contains('\n'))
        s = '"' + s.replace("\"", "\"\"") + '"';
    return s;
}

} // namespace msgparse
