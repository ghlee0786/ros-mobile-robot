#include "servicequeue.h"

#include "rosbridge.h"

ServiceQueue::ServiceQueue(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        const Fail fail = m_cur.fail;
        const QString svc = m_cur.service;
        finish();
        if (fail)
            fail(svc + " 응답 없음(" + QString::number(m_timer.interval() / 1000.0) + " s)");
        pump();
    });
}

void ServiceQueue::enqueue(const QString &service, const QJsonObject &args, Ok ok, Fail fail)
{
    m_queue.enqueue({service, args, std::move(ok), std::move(fail)});
    emit pendingChanged();
    pump();
}

void ServiceQueue::finish()
{
    m_timer.stop();
    m_inFlight = false;
    m_cur = {};
    ++m_gen;
    emit pendingChanged();
}

void ServiceQueue::pump()
{
    while (!m_inFlight && !m_queue.isEmpty()) {
        m_cur = m_queue.dequeue();
        if (!m_bridge || !m_bridge->connected()) {
            const Fail fail = m_cur.fail;
            finish();
            if (fail)
                fail("연결 안 됨");
            continue;
        }
        m_inFlight = true;
        const int gen = ++m_gen;
        m_timer.start();
        QPointer<ServiceQueue> self(this);
        m_bridge->call(m_cur.service, m_cur.args, [self, gen](const QJsonObject &v) {
            if (!self || gen != self->m_gen)
                return;   // 이미 시간 초과로 끝난 호출
            const Ok ok = self->m_cur.ok;
            self->finish();
            if (ok)
                ok(v);
            if (self)
                self->pump();
        }, [self, gen](const QString &why) {
            if (!self || gen != self->m_gen)
                return;
            const Fail fail = self->m_cur.fail;
            const QString svc = self->m_cur.service;
            self->finish();
            if (fail)
                fail(svc + ": " + why);
            if (self)
                self->pump();
        });
    }
}
