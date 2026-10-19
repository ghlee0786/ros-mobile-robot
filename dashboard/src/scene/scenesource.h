#pragma once

#include "rosbridge.h"

#include <QtQml/qqmlregistration.h>

// 3D 장면 레이어의 공통 바탕: bridge 를 받아 active 인 동안만 자기 토픽을 구독하고(RosBridge 공개 API 만 씀),
// /tf 가 함께 오면 map→odom 을 기억해 map 프레임 좌표를 odom 프레임으로 바꿔 준다.
class SceneSource : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool received READ received NOTIFY receivedChanged)   // 켠 뒤 메시지를 하나라도 받았는지

public:
    struct Sub { QString topic, type; int throttleMs; };

    explicit SceneSource(QObject *parent = nullptr);

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    bool active() const { return m_active; }
    void setActive(bool on);
    bool received() const { return m_received; }

    // 받은 메시지 하나를 처리한다(bridge 의 message 신호가 부른다. 시험은 직접 부른다).
    void feed(const QString &topic, const QJsonObject &msg);

signals:
    void bridgeChanged();
    void activeChanged();
    void receivedChanged();

protected:
    virtual QList<Sub> topics() const { return {}; }
    virtual void handle(const QString &topic, const QJsonObject &msg) { Q_UNUSED(topic) Q_UNUSED(msg) }
    void setReceived(bool on);
    // frame_id 가 map 이고 map→odom 을 알면 (x, y, yaw) 를 odom 프레임으로 바꾼다.
    void toOdom(const QString &frame, double &x, double &y, double &yaw) const;
    static qint64 now();

    QString m_reason;   // RosBridge::want 의 용도 이름(인스턴스마다 다름)

private:
    void want(bool on);

    RosBridge *m_bridge = nullptr;
    QMetaObject::Connection m_conn;
    bool m_active = false, m_received = false;
    bool m_haveMapOdom = false;
    double m_moX = 0, m_moY = 0, m_moYaw = 0;   // map → odom
};
