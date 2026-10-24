#include "speedlimits.h"

#include "rosbridge.h"

#include <QJsonArray>
#include <QSettings>
#include <cmath>
#include <limits>

namespace {
const char *const kNames[] = {"max_forward", "max_reverse", "max_angular"};
const char *const kLabels[] = {"최대 전진", "최대 후진", "최대 회전"};
const char *const kUnits[] = {"m/s", "m/s", "rad/s"};
constexpr double kFirmware[] = {0.35, 0.35, 0.80};   // Pico 펌웨어 한계(speed_limits.FIRMWARE 와 같다)
constexpr double kDefault[] = {0.25, 0.15, 0.60};    // 워치독 기본값(speed_limits.DEFAULTS 와 같다)
constexpr double kSame = 5e-4;                        // 워치독은 double 그대로 돌려주지만 표시는 소수 둘째 자리
constexpr int kPushDelayMs = 250;
} // namespace

QString SpeedLimits::name(int i) { return QString::fromLatin1(kNames[i]); }

int SpeedLimits::indexOf(const QString &n)
{
    for (int i = 0; i < Count; ++i)
        if (n == QLatin1String(kNames[i]))
            return i;
    return -1;
}

double SpeedLimits::firmware(int i) { return kFirmware[i]; }
double SpeedLimits::defaultValue(int i) { return kDefault[i]; }

double SpeedLimits::bound(int i, double v)
{
    return std::isfinite(v) ? qBound(0.0, v, kFirmware[i]) : std::numeric_limits<double>::quiet_NaN();
}

bool SpeedLimits::parseValues(const QJsonObject &resp, double out[Count])
{
    const QJsonArray vals = resp["values"].toArray();
    if (vals.size() != Count)
        return false;
    for (int i = 0; i < Count; ++i) {
        const QJsonObject v = vals[i].toObject();
        // 3 = PARAMETER_DOUBLE. Foxy 는 형식을 고정하지 않아 `ros2 param set ... 0` 이면 2 = PARAMETER_INTEGER 가 된다
        const int t = v["type"].toInt();
        if (t != 3 && t != 2)
            return false;
        out[i] = t == 3 ? v["double_value"].toDouble() : v["integer_value"].toDouble();
    }
    return true;
}

double SpeedLimits::linearCommand(double lin, double maxSpeed, double maxForward, double maxReverse)
{
    return lin * qMin(maxSpeed, lin >= 0 ? maxForward : maxReverse);
}

double SpeedLimits::angularCommand(double ang, double scale, double maxAngular)
{
    return ang * qMin(scale, maxAngular);
}

SpeedLimits::SpeedLimits(RosBridge &bridge, QObject *parent) : QObject(parent), m_bridge(bridge)
{
    QSettings s;
    for (int i = 0; i < Count; ++i) {
        const double v = bound(i, s.value(QStringLiteral("speedLimits/%1").arg(kNames[i]), kDefault[i]).toDouble());
        m_want[i] = std::isnan(v) ? kDefault[i] : v;
        m_locked[i] = s.value(QStringLiteral("speedLimits/%1_locked").arg(kNames[i]), false).toBool();
    }
    m_push.setSingleShot(true);
    m_push.setInterval(kPushDelayMs);
    connect(&m_push, &QTimer::timeout, this, &SpeedLimits::push);
}

QVariantList SpeedLimits::rows() const
{
    QVariantList out;
    for (int i = 0; i < Count; ++i) {
        const bool same = m_actualKnown && std::abs(m_actual[i] - m_want[i]) < kSame;
        out.append(QVariantMap{{"name", name(i)}, {"label", QString::fromUtf8(kLabels[i])}, {"unit", kUnits[i]},
                               {"value", m_want[i]}, {"actual", m_actual[i]}, {"actualKnown", m_actualKnown},
                               {"locked", m_locked[i]}, {"firmware", kFirmware[i]},
                               // 옛 워치독이면 대시보드 배율만 묶으므로 줄마다 경고하지 않는다(패널 안내 한 줄)
                               {"warn", m_support != -1 && !same}});
    }
    return out;
}

void SpeedLimits::setLimit(const QString &n, double v)
{
    const int i = indexOf(n);
    if (i < 0 || m_locked[i])
        return;
    v = bound(i, v);
    if (std::isnan(v) || v == m_want[i])
        return;
    m_want[i] = v;
    m_dirty |= 1u << i;
    save();
    emit changed();
    m_push.start();
}

void SpeedLimits::setLocked(const QString &n, bool on)
{
    const int i = indexOf(n);
    if (i < 0 || m_locked[i] == on)
        return;
    m_locked[i] = on;
    save();
    emit changed();
}

void SpeedLimits::push()
{
    if (!m_dirty || !m_bridge.connected() || m_bridge.noPublish() || m_support == -1) {
        m_dirty = 0;   // 보낼 곳이 없으면 대시보드 배율 제한만(다음 연결 때는 워치독 값을 가져온다)
        return;
    }
    QJsonArray params;
    QList<int> idx;
    for (int i = 0; i < Count; ++i)
        if (m_dirty & (1u << i)) {
            params.append(QJsonObject{{"name", name(i)}, {"value", QJsonObject{{"type", 3}, {"double_value", m_want[i]}}}});
            idx.append(i);
        }
    m_dirty = 0;
    m_bridge.call("/cmd_watchdog/set_parameters", {{"parameters", params}}, [this, idx](const QJsonObject &v) {
        const QJsonArray res = v["results"].toArray();
        for (int k = 0; k < idx.size(); ++k) {
            const QJsonObject r = res.at(k).toObject();
            if (r["successful"].toBool())
                continue;
            emit event(QStringLiteral("속도 한계 %1 설정 거부: %2").arg(name(idx[k]),
                       res.size() > k ? r["reason"].toString() : QStringLiteral("응답 없음")));
        }
        refresh();   // 화면에는 워치독이 실제로 가진 값을 보인다
    }, [this](const QString &e) { emit event("속도 한계 설정 실패: " + e); });
}

void SpeedLimits::refresh()
{
    if (!m_bridge.connected() || m_bridge.noPublish())
        return;
    QJsonArray names;
    for (int i = 0; i < Count; ++i)
        names.append(name(i));
    m_bridge.call("/cmd_watchdog/get_parameters", {{"names", names}}, [this](const QJsonObject &v) {
        double vals[Count];
        if (!parseValues(v, vals)) {
            if (m_support != -1)
                emit event("파이 워치독이 속도 한계를 지원하지 않음 — 배포 필요(대시보드 배율만 제한)");
            m_support = -1;
            m_actualKnown = false;
            emit changed();
            return;
        }
        m_support = 1;
        m_actualKnown = true;
        for (int i = 0; i < Count; ++i)
            m_actual[i] = vals[i];
        if (!m_synced) {   // 처음 연결 때는 워치독이 기준(잠긴 줄도 — 워치독이 실제로 자르는 값을 보여야 한다)
            m_synced = true;
            for (int i = 0; i < Count; ++i)
                m_want[i] = vals[i];
            save();
        }
        emit changed();
    }, [this](const QString &) {
        if (m_actualKnown) {
            m_actualKnown = false;
            emit changed();
        }
    });
}

void SpeedLimits::onConnectedChanged(bool connected)
{
    m_synced = false;
    m_actualKnown = false;
    m_support = 0;
    if (connected)
        refresh();
    emit changed();
}

void SpeedLimits::save() const
{
    QSettings s;
    for (int i = 0; i < Count; ++i) {
        s.setValue(QStringLiteral("speedLimits/%1").arg(kNames[i]), m_want[i]);
        s.setValue(QStringLiteral("speedLimits/%1_locked").arg(kNames[i]), m_locked[i]);
    }
}
