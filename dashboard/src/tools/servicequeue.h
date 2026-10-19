#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <functional>

#include "rosbridge.h"

// 서비스 호출을 한 번에 하나씩 직렬로 보내고, timeoutMs 안에 응답이 없으면 실패로 돌린다.
// RosBridge::call() 의 실패 콜백(result=false · 연결 끊김)은 그 이유를 fail 로 바로 넘긴다.
class ServiceQueue : public QObject
{
    Q_OBJECT
public:
    using Ok = std::function<void(const QJsonObject &)>;
    using Fail = std::function<void(const QString &)>;

    explicit ServiceQueue(QObject *parent = nullptr);
    void setBridge(RosBridge *b) { m_bridge = b; }
    void setTimeoutMs(int ms) { m_timer.setInterval(ms); }
    void enqueue(const QString &service, const QJsonObject &args, Ok ok, Fail fail);
    int pending() const { return m_queue.size() + (m_inFlight ? 1 : 0); }

signals:
    void pendingChanged();

private:
    struct Job { QString service; QJsonObject args; Ok ok; Fail fail; };
    void pump();
    void finish();

    QPointer<RosBridge> m_bridge;
    QQueue<Job> m_queue;
    Job m_cur;
    bool m_inFlight = false;
    int m_gen = 0;
    QTimer m_timer;
};
