#include "rosbridge.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QTcpSocket>

static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

RosBridge::RosBridge(QObject *parent) : QObject(parent)
{
    connect(&m_ws, &QWebSocket::connected, this, [this] {
        m_connected = true;
        m_error.clear();
        emit errorChanged();
        emit connectedChanged();
        for (auto it = m_adv.cbegin(); it != m_adv.cend(); ++it)
            send({{"op", "advertise"}, {"topic", it.key()}, {"type", it.value()}});
        for (const QString &t : m_wanted.keys())
            sendSubscribe(t);
        refreshTopics();
        m_tick.start();
    });
    connect(&m_ws, &QWebSocket::disconnected, this, [this] {
        m_tick.stop();
        m_calls.clear();
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
        }
    });
    connect(&m_ws, &QWebSocket::errorOccurred, this, [this] {
        m_error = m_ws.errorString();
        emit errorChanged();
    });
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &RosBridge::onText);
    connect(&m_ws, &QWebSocket::pong, this, [this](quint64 ms) {
        m_latency = int(ms);
        emit latencyChanged();
    });

    m_retry.setInterval(3000);   // 끊기면 3 s 간격으로 다시 연결
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_ws.state() == QAbstractSocket::UnconnectedState)
            m_ws.open(QUrl(QString("ws://%1:%2").arg(m_host).arg(m_port)));
    });
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        m_ws.ping();
        if (++m_ticks % 5 == 0)  // 새 발행자(/map 등)를 알아채도록 토픽 목록을 5 s 마다 갱신
            refreshTopics();
    });
}

void RosBridge::open(const QString &host, int port)
{
    m_host = host;
    m_port = port;
    emit hostChanged();
    m_ws.abort();
    m_types.clear();
    m_topicListReceived = false;
    emit topicListChanged();
    m_ws.open(QUrl(QString("ws://%1:%2").arg(host).arg(port)));
    m_retry.start();
}

void RosBridge::send(const QJsonObject &o)
{
    if (m_connected)
        m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void RosBridge::sendSubscribe(const QString &topic)
{
    QJsonObject o{{"op", "subscribe"}, {"topic", topic}, {"type", m_subType.value(topic)}};
    if (int t = m_throttle.value(topic))
        o["throttle_rate"] = t, o["queue_length"] = 1;
    send(o);
    m_times[topic] = Times{{}, 0, nowMs(), qQNaN()};
}

void RosBridge::want(const QString &topic, const QString &type, const QString &reason, bool on, int throttleMs)
{
    const bool was = m_wanted.contains(topic);
    if (on) {
        m_wanted[topic].insert(reason);
        m_subType[topic] = type;
        m_throttle[topic] = throttleMs;
        if (!was)
            sendSubscribe(topic);
    } else if (was) {
        m_wanted[topic].remove(reason);
        if (m_wanted[topic].isEmpty()) {
            m_wanted.remove(topic);
            m_times.remove(topic);
            send({{"op", "unsubscribe"}, {"topic", topic}});
        }
    }
}

void RosBridge::resubscribe()
{
    for (const QString &t : m_wanted.keys()) {
        send({{"op", "unsubscribe"}, {"topic", t}});
        sendSubscribe(t);
    }
    refreshTopics();
}

void RosBridge::advertise(const QString &topic, const QString &type)
{
    m_adv[topic] = type;
    send({{"op", "advertise"}, {"topic", topic}, {"type", type}});
}

void RosBridge::publish(const QString &topic, const QJsonObject &msg)
{
    send({{"op", "publish"}, {"topic", topic}, {"msg", msg}});
}

void RosBridge::call(const QString &service, const QJsonObject &args, std::function<void(const QJsonObject &)> done)
{
    if (!m_connected)
        return;
    const QString id = QString("call_%1").arg(++m_callId);
    m_calls[id] = std::move(done);
    send({{"op", "call_service"}, {"service", service}, {"args", args}, {"id", id}});
}

void RosBridge::refreshTopics()
{
    call("/rosapi/topics", {}, [this](const QJsonObject &v) {
        const QJsonArray names = v["topics"].toArray(), types = v["types"].toArray();
        QHash<QString, QString> t;
        for (int i = 0; i < names.size(); ++i)
            t[names[i].toString()] = types[i].toString();
        if (t != m_types || !m_topicListReceived) {
            m_types = t;
            m_topicListReceived = true;
            emit topicListChanged();
        }
    });
}

void RosBridge::onText(const QString &text)
{
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString op = o["op"].toString();
    if (op == "publish") {
        const QString topic = o["topic"].toString();
        const QJsonObject msg = o["msg"].toObject();
        Times &t = m_times[topic];
        const qint64 now = nowMs();
        t.last = now;
        t.arrivals.append(now);
        while (!t.arrivals.isEmpty() && t.arrivals.first() < now - 2000)
            t.arrivals.removeFirst();
        const QJsonObject stamp = msg["header"].toObject()["stamp"].toObject();
        if (!stamp.isEmpty())
            t.latency = now - (stamp["sec"].toDouble() * 1000.0 + stamp["nanosec"].toDouble() / 1e6);
        emit message(topic, msg);
    } else if (op == "service_response") {
        auto done = m_calls.take(o["id"].toString());
        if (done && o["result"].toBool(true))
            done(o["values"].toObject());
    }
}

RosBridge::Stat RosBridge::stat(const QString &topic)
{
    Stat s;
    auto it = m_times.find(topic);
    if (it == m_times.end())
        return s;
    const qint64 now = nowMs();
    QList<qint64> &a = it->arrivals;
    while (!a.isEmpty() && a.first() < now - 2000)
        a.removeFirst();
    s.recent = int(a.size());
    if (a.size() >= 2)
        s.hz = (a.size() - 1) * 1000.0 / qMax<qint64>(1, a.last() - a.first());
    s.age = it->last ? (now - it->last) / 1000.0 : -1;
    s.latencyMs = it->latency;
    s.sinceSub = (now - it->since) / 1000.0;
    return s;
}

// 이 PC 가 붙어 있는 /24 망마다 9090 포트가 열린 주소를 찾는다.
void RosBridge::discover()
{
    if (m_searching)
        return;
    m_found.clear();
    m_searching = true;
    emit foundChanged();
    QSet<quint32> nets;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
            nets.insert(a.toIPv4Address() & 0xFFFFFF00u);
    for (quint32 net : nets) {
        for (quint32 h = 1; h < 255; ++h) {
            auto *s = new QTcpSocket(this);
            const QString ip = QHostAddress(net | h).toString();
            connect(s, &QTcpSocket::connected, this, [this, s, ip] {
                if (!m_found.contains(ip))
                    m_found.append(ip), emit foundChanged();
                s->abort();
            });
            QTimer::singleShot(1500, s, &QObject::deleteLater);
            s->connectToHost(ip, quint16(m_port));
        }
    }
    QTimer::singleShot(1600, this, [this] {
        m_searching = false;
        emit foundChanged();
    });
}
