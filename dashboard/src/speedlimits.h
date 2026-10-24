#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class RosBridge;

// 파이 cmd_watchdog 의 속도 한계(max_forward · max_reverse · max_angular, tooling/rpi/robot_link/speed_limits.py).
// 워치독이 모든 속도 명령(키보드 · 조이스틱 · MCP · Nav2)을 이 값으로 자르고, 대시보드는 같은 값으로 자기 배율도 묶는다.
// 읽기 · 쓰기는 가감속(ramp_enabled)과 같은 /cmd_watchdog/get_parameters · set_parameters(rosbridge 서비스).
// 바라는 값(want)은 슬라이더 · QSettings, 실제 값(actual)은 워치독이 돌려준 것 — 다르면 화면이 경고색.
class SpeedLimits : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // 줄마다 {name, label, unit, value, actual, actualKnown, locked, firmware, warn}. 슬라이더를 끄는 중에 대리자를
    // 다시 만들지 않게 QML 은 Repeater { model: 3 } 안에서 rows[index] 를 읽는다.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    // 1 워치독이 한계를 안다 · -1 모른다(옛 워치독, 파이 배포 필요) · 0 아직 모름(연결 전 · 응답 전)
    Q_PROPERTY(int support READ support NOTIFY changed)
    Q_PROPERTY(double maxForward READ maxForward NOTIFY changed)
    Q_PROPERTY(double maxReverse READ maxReverse NOTIFY changed)
    Q_PROPERTY(double maxAngular READ maxAngular NOTIFY changed)

public:
    enum { Forward, Reverse, Angular, Count };

    // ---- 순수 함수(단위 시험) ----
    static QString name(int i);
    static int indexOf(const QString &name);   // 모르는 이름이면 -1
    static double firmware(int i);             // Pico 펌웨어 한계 = 설정할 수 있는 상한
    static double defaultValue(int i);         // 워치독 기본값과 같다
    static double bound(int i, double v);      // 0 ~ 펌웨어 한계. 숫자가 아니면 NaN
    // get_parameters 응답에서 세 값(모두 PARAMETER_DOUBLE 이어야). 아니면 false(옛 워치독은 type 0 을 돌려준다)
    static bool parseValues(const QJsonObject &resp, double out[Count]);
    // 키보드 · 조이스틱 입력(-1..1)을 속도로: 앞은 min(최대 속도, 최대 전진), 뒤는 min(최대 속도, 최대 후진)
    static double linearCommand(double lin, double maxSpeed, double maxForward, double maxReverse);
    static double angularCommand(double ang, double scale, double maxAngular);

    explicit SpeedLimits(RosBridge &bridge, QObject *parent = nullptr);

    QVariantList rows() const;
    int support() const { return m_support; }
    double value(int i) const { return m_want[i]; }
    double maxForward() const { return m_want[Forward]; }
    double maxReverse() const { return m_want[Reverse]; }
    double maxAngular() const { return m_want[Angular]; }
    bool locked(int i) const { return m_locked[i]; }

    // 잠긴 줄이면 무시한다. 바꾼 값은 잠깐(끄는 동안 서비스 호출이 쏟아지지 않게) 모아 워치독에 보낸다.
    Q_INVOKABLE void setLimit(const QString &name, double v);
    Q_INVOKABLE void setLocked(const QString &name, bool on);
    Q_INVOKABLE void refresh();   // 워치독 값을 다시 읽는다(Robot 이 5 s 마다)
    void onConnectedChanged(bool connected);

signals:
    void changed();
    void event(const QString &text);

private:
    void push();
    void save() const;

    RosBridge &m_bridge;
    double m_want[Count];
    double m_actual[Count] = {};
    bool m_locked[Count] = {};
    bool m_actualKnown = false;
    bool m_synced = false;   // 이번 연결에서 워치독 값을 슬라이더로 가져왔는지(처음 연결 때는 워치독이 기준)
    int m_support = 0;
    unsigned m_dirty = 0;    // 보낼 줄(비트)
    QTimer m_push;
};
