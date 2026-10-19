#include "rosbridge.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QtConcurrent>

namespace {
constexpr qsizetype kBigMessage = 512 * 1024;   // 이보다 큰 원문은 다른 스레드에서 파싱
constexpr qint64 kRxTimeoutMs = 3000;           // 이만큼 아무것도 받지 못하면 반쯤 열린 연결로 보고 끊는다
constexpr int kCallTimeoutMs = 5000;
constexpr int kHistory = 60;

// 토픽별 throttle 하한(ms). 파이의 rosbridge 는 코어 하나로 돌아 전속 IMU · TF 를 받으면 모든 토픽이
// 22 Hz 로 떨어졌다(00908e1). 어느 용도가 0(전속)으로 구독해도 이 값보다 빨라지지 않는다.
const QHash<QString, int> kThrottleFloor = {
    {"/imu/data_raw", 50}, {"/tf", 200}, {"/map", 2000}, {"/camera/image_raw", 1000}};
} // namespace

RosBridge::RosBridge(QObject *parent) : QObject(parent)
{
    m_mono.start();
    m_noPublish = qEnvironmentVariableIntValue("ROBOT_DASHBOARD_NO_PUBLISH") == 1;
    connect(&m_ws, &QWebSocket::connected, this, [this] {
        m_connected = true;
        m_lastRx.restart();
        m_error.clear();
        emit errorChanged();
        emit connectedChanged();
        if (!m_noPublish)
            for (auto it = m_adv.cbegin(); it != m_adv.cend(); ++it)
                send({{"op", "advertise"}, {"topic", it.key()}, {"type", it.value()}});
        m_sent.clear();
        for (const QString &t : m_wanted.keys())
            sendSubscribe(t);
        refreshTopics();
        m_tick.start();
    });
    connect(&m_ws, &QWebSocket::disconnected, this, [this] {
        m_tick.stop();
        for (const QString &id : m_calls.keys())
            failCall(id, "연결 끊김");
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
            emit rxChanged();
        }
    });
    connect(&m_ws, &QWebSocket::errorOccurred, this, [this] {
        m_error = m_ws.errorString();
        emit errorChanged();
    });
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &RosBridge::onText);
    connect(&m_ws, &QWebSocket::binaryMessageReceived, this, [this] { m_lastRx.restart(); });
    connect(&m_ws, &QWebSocket::pong, this, [this](quint64 ms) {
        m_lastRx.restart();
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
        if (m_lastRx.elapsed() > kRxTimeoutMs) {   // 반쯤 열린 연결: disconnected → 재연결
            m_error = QString("%1 s 동안 수신 없음 — 다시 연결").arg(kRxTimeoutMs / 1000);
            emit errorChanged();
            m_ws.abort();
            return;
        }
        m_rxBytesPerSec = double(m_rxSecBytes);
        m_rxSecBytes = 0;
        m_rxHistory.append(m_rxBytesPerSec);
        m_latencyHistory.append(m_latency);
        while (m_rxHistory.size() > kHistory)
            m_rxHistory.removeFirst();
        while (m_latencyHistory.size() > kHistory)
            m_latencyHistory.removeFirst();
        emit rxChanged();
        m_ws.ping();
        if (++m_ticks % 5 == 0)  // 새 발행자(/map 등)를 알아채도록 토픽 목록을 5 s 마다 갱신
            refreshTopics();
    });
}

// 소켓이 소멸 중에 내는 disconnected 가 이미 해제된 멤버를 건드리지 않게 먼저 끊는다.
RosBridge::~RosBridge()
{
    m_ws.disconnect(this);
    m_ws.abort();
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

void RosBridge::shutdown()
{
    m_retry.stop();
    m_ws.flush();
    m_ws.close();
}

bool RosBridge::send(const QJsonObject &o)
{
    if (!m_connected)
        return false;
    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    return true;
}

RosBridge::Sub RosBridge::subOf(const QString &topic) const
{
    Sub s{m_subType.value(topic), -1, -1};
    const auto th = m_throttle.value(topic), qu = m_queue.value(topic);
    for (auto it = th.cbegin(); it != th.cend(); ++it)
        if (it.key() != "record" && it.value() >= 0)
            s.throttle = s.throttle < 0 ? it.value() : qMin(s.throttle, it.value());
    s.throttle = topic == "/tf_static" ? 0 : qMax(kThrottleFloor.value(topic), qMax(0, s.throttle));
    for (int q : qu)
        s.queue = qMax(s.queue, q);
    if (s.queue <= 0 && s.throttle > 0)
        s.queue = 1;
    return s;
}

int RosBridge::throttleOf(const QString &topic) const { return m_wanted.contains(topic) ? subOf(topic).throttle : 0; }

void RosBridge::sendSubscribe(const QString &topic)
{
    const Sub s = subOf(topic);
    QJsonObject o{{"op", "subscribe"}, {"topic", topic}, {"type", s.type}};
    if (s.throttle > 0)
        o["throttle_rate"] = s.throttle;
    if (s.queue > 0)
        o["queue_length"] = s.queue;
    m_sent[topic] = s;
    send(o);
    if (!m_times.contains(topic))
        m_times[topic] = Times{{}, 0, m_mono.elapsed(), qQNaN(), 0};
}

void RosBridge::want(const QString &topic, const QString &type, const QString &reason, bool on, int throttleMs,
                     int queueLength)
{
    const bool was = m_wanted.contains(topic);
    if (on) {
        m_wanted[topic].insert(reason);
        if (!type.isEmpty())
            m_subType[topic] = type;
        m_throttle[topic][reason] = throttleMs;
        m_queue[topic][reason] = queueLength;
    } else if (was) {
        m_wanted[topic].remove(reason);
        m_throttle[topic].remove(reason);
        m_queue[topic].remove(reason);
        if (m_wanted[topic].isEmpty()) {
            m_wanted.remove(topic);
            m_throttle.remove(topic);
            m_queue.remove(topic);
            m_sent.remove(topic);
            m_times.remove(topic);
            send({{"op", "unsubscribe"}, {"topic", topic}});
            return;
        }
    }
    if (!m_wanted.contains(topic))
        return;
    const Sub now = subOf(topic), prev = m_sent.value(topic, Sub{QString(), -2, -2});
    if (!was || now.throttle != prev.throttle || now.queue != prev.queue || now.type != prev.type)
        sendSubscribe(topic);
}

void RosBridge::resubscribe()
{
    for (const QString &t : m_wanted.keys()) {
        send({{"op", "unsubscribe"}, {"topic", t}});
        m_times.remove(t);
        sendSubscribe(t);
    }
    refreshTopics();
}

void RosBridge::advertise(const QString &topic, const QString &type)
{
    m_adv[topic] = type;
    if (!m_noPublish)
        send({{"op", "advertise"}, {"topic", topic}, {"type", type}});
}

bool RosBridge::publish(const QString &topic, const QJsonObject &msg)
{
    if (m_noPublish)
        return false;
    return send({{"op", "publish"}, {"topic", topic}, {"msg", msg}});
}

void RosBridge::call(const QString &service, const QJsonObject &args, Done done, Fail fail)
{
    if (!m_connected) {
        if (fail)
            fail("연결 안 됨");
        return;
    }
    const QString id = QString("call_%1").arg(++m_callId);
    m_calls[id] = Call{std::move(done), std::move(fail), service};
    emit callsChanged();
    send({{"op", "call_service"}, {"service", service}, {"args", args}, {"id", id}});
    QTimer::singleShot(kCallTimeoutMs, this, [this, id] { failCall(id, "응답 없음 (5 s)"); });
}

void RosBridge::failCall(const QString &id, const QString &why)
{
    if (!m_calls.contains(id))
        return;
    const Call c = m_calls.take(id);
    emit callsChanged();
    if (c.fail)
        c.fail(why);
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
    m_lastRx.restart();
    const qint64 bytes = text.size();   // UTF-8 바이트 근사(대부분 ASCII)
    m_rxSecBytes += bytes;
    if (bytes <= kBigMessage) {
        process(QJsonDocument::fromJson(text.toUtf8()).object(), bytes, 0);
        return;
    }
    // 큰 메시지(/map 등): GUI 스레드에서는 토픽만 정규식으로 보고, 파싱은 다른 스레드에서 한다.
    static const QRegularExpression re(R"re("topic"\s*:\s*"([^"]*)")re");
    const QString topic = re.match(text.left(4096)).captured(1);
    const int id = ++m_bigId;
    m_pending[topic].append(Pending{id, false, {}, bytes, 0});
    QtConcurrent::run([text] {
        QElapsedTimer t;
        t.start();
        const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
        return std::pair<QJsonObject, double>(o, double(t.elapsed()));
    }).then(this, [this, topic, id](const std::pair<QJsonObject, double> &r) {   // GUI 스레드로 queued
        finishBig(topic, id, r.first, r.second);
    });
}

void RosBridge::finishBig(const QString &topic, int id, const QJsonObject &o, double workerMs)
{
    auto it = m_pending.find(topic);
    if (it == m_pending.end())
        return;
    for (Pending &p : *it)
        if (p.id == id)
            p.ready = true, p.o = o, p.workerMs = workerMs;
    while (!it->isEmpty() && it->first().ready) {
        const Pending p = it->takeFirst();
        dispatch(p.o, p.bytes, p.workerMs);
        it = m_pending.find(topic);   // dispatch 중 m_pending 이 바뀌었을 수 있다
        if (it == m_pending.end())
            return;
    }
    if (it->isEmpty())
        m_pending.erase(it);
}

void RosBridge::process(const QJsonObject &o, qint64 bytes, double workerMs)
{
    if (o["op"].toString() == "publish") {
        auto it = m_pending.find(o["topic"].toString());
        if (it != m_pending.end() && !it->isEmpty()) {   // 앞선 큰 메시지가 아직 파싱 중이면 뒤에 줄 선다
            it->append(Pending{0, true, o, bytes, workerMs});
            return;
        }
    }
    dispatch(o, bytes, workerMs);
}

void RosBridge::dispatch(const QJsonObject &o, qint64 bytes, double workerMs)
{
    const QString op = o["op"].toString();
    if (op == "publish") {
        const QString topic = o["topic"].toString();
        const QJsonObject msg = o["msg"].toObject();
        Times &t = m_times[topic];
        const qint64 now = m_mono.elapsed();
        t.last = now;
        t.total += bytes;
        t.arrivals.append({now, bytes});
        while (!t.arrivals.isEmpty() && t.arrivals.first().first < now - 2000)
            t.arrivals.removeFirst();
        const QJsonObject stamp = msg["header"].toObject()["stamp"].toObject();
        if (!stamp.isEmpty())   // 지연은 벽시계끼리 비교한다(파이 시계와 어긋나면 clockSkew)
            t.latency = QDateTime::currentMSecsSinceEpoch()
                        - (stamp["sec"].toDouble() * 1000.0 + stamp["nanosec"].toDouble() / 1e6);
        m_lastWorkerMs = workerMs;
        emit message(topic, msg);
        m_lastWorkerMs = 0;
    } else if (op == "service_response") {
        const QString id = o["id"].toString();
        if (!m_calls.contains(id))
            return;
        const Call c = m_calls.take(id);
        emit callsChanged();
        if (o["result"].toBool(true)) {
            if (c.done)
                c.done(o["values"].toObject());
        } else {
            const QJsonValue v = o["values"];
            const QString why = v.isString() ? v.toString()
                                             : QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
            emit bridgeError(QString("서비스 %1 실패: %2").arg(c.service, why));
            if (c.fail)
                c.fail(why);
        }
    } else if (op == "status") {
        emit bridgeError(QString("rosbridge %1: %2").arg(o["level"].toString(), o["msg"].toString()));
    }
}

RosBridge::Stat RosBridge::stat(const QString &topic)
{
    Stat s;
    auto it = m_times.find(topic);
    if (it == m_times.end())
        return s;
    const qint64 now = m_mono.elapsed();
    auto &a = it->arrivals;
    while (!a.isEmpty() && a.first().first < now - 2000)
        a.removeFirst();
    s.recent = int(a.size());
    if (a.size() >= 2)
        s.hz = (a.size() - 1) * 1000.0 / qMax<qint64>(1, a.last().first - a.first().first);
    for (const auto &e : a)
        s.bytesPerSec += e.second / 2.0;
    s.totalBytes = it->total;
    s.age = it->last ? (now - it->last) / 1000.0 : -1;
    s.latencyMs = it->latency;
    s.clockSkew = !qIsNaN(s.latencyMs) && std::abs(s.latencyMs) > 5000;
    s.sinceSub = (now - it->since) / 1000.0;
    return s;
}

// 이 PC 가 붙어 있는 /24 망마다 port(기본: 지금 포트)가 열린 주소를 찾는다. 가상 어댑터(WSL · Hyper-V)는 건너뛰고,
// 동시에 여는 연결은 64 개까지.
void RosBridge::discover(int port)
{
    if (m_searching)
        return;
    m_found.clear();
    m_searching = true;
    emit foundChanged();
    m_scanPort = quint16(port > 0 ? port : m_port);
    QSet<quint32> nets;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        const auto f = i.flags();
        const QString name = i.humanReadableName() + " " + i.name();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack)
            || name.contains("vEthernet", Qt::CaseInsensitive) || name.contains("WSL", Qt::CaseInsensitive)
            || name.contains("Hyper-V", Qt::CaseInsensitive))
            continue;
        for (const QNetworkAddressEntry &e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
                nets.insert(a.toIPv4Address() & 0xFFFFFF00u);
        }
    }
    m_scanQueue.clear();
    for (quint32 net : nets)
        for (quint32 h = 1; h < 255; ++h)
            m_scanQueue.append(QHostAddress(net | h).toString());
    m_scanActive = 0;
    for (int i = 0; i < 64; ++i)
        scanNext();
}

void RosBridge::scanNext()
{
    if (m_scanQueue.isEmpty()) {
        if (m_scanActive == 0 && m_searching) {
            m_searching = false;
            emit foundChanged();
        }
        return;
    }
    const QString ip = m_scanQueue.takeFirst();
    ++m_scanActive;
    auto *s = new QTcpSocket(this);
    auto finish = [this, s] {
        if (s->property("done").toBool())
            return;
        s->setProperty("done", true);
        s->deleteLater();
        --m_scanActive;
        scanNext();
    };
    connect(s, &QTcpSocket::connected, this, [this, s, ip, finish] {
        if (!m_found.contains(ip))
            m_found.append(ip), emit foundChanged();
        s->abort();
        finish();
    });
    connect(s, &QTcpSocket::errorOccurred, this, finish);
    QTimer::singleShot(1500, s, finish);
    s->connectToHost(ip, m_scanPort);
}
