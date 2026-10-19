#include "rosapiclient.h"

#include "rosbridge.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>

static QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &s : v.toArray())
        out.append(s.toString());
    return out;
}

RosApiClient::RosApiClient(QObject *parent) : QObject(parent)
{
    connect(&m_queue, &ServiceQueue::pendingChanged, this, &RosApiClient::callsPendingChanged);
}

void RosApiClient::setBridge(RosBridge *b)
{
    if (b == m_bridge)
        return;
    m_bridge = b;
    m_queue.setBridge(b);
    emit bridgeChanged();
}

void RosApiClient::setError(const QString &e, bool slow)
{
    if (e == m_error && slow == m_slow)
        return;
    m_error = e;
    m_slow = slow;
    emit errorChanged();
}

void RosApiClient::rosapi(const QString &service, const QJsonObject &args, ServiceQueue::Ok ok)
{
    m_queue.enqueue(service, args, [this, ok](const QJsonObject &r) {
        setError({}, false);
        ok(r);
    }, [this](const QString &e) { setError(e, e.contains("응답 없음")); });   // 시간 초과만 지연 경보
}

void RosApiClient::refreshNodes()
{
    rosapi("/rosapi/nodes", {}, [this](const QJsonObject &r) {
        QStringList n = strings(r["nodes"]);
        n.sort();
        m_nodes = n;
        emit nodesChanged();
    });
}

void RosApiClient::refreshServices()
{
    rosapi("/rosapi/services", {}, [this](const QJsonObject &r) {
        QStringList s = strings(r["services"]);
        s.sort();
        m_services = s;
        emit servicesChanged();
    });
}

void RosApiClient::refreshTopics()
{
    rosapi("/rosapi/topics", {}, [this](const QJsonObject &r) {
        QStringList t = strings(r["topics"]);
        t.sort();
        m_topics = t;
        emit topicsChanged();
    });
}

void RosApiClient::selectNode(const QString &node)
{
    m_node = node;
    m_pub.clear(), m_sub.clear(), m_nodeSrv.clear();
    emit nodeDetailsChanged();
    if (node.isEmpty())
        return;
    rosapi("/rosapi/node_details", {{"node", node}}, [this, node](const QJsonObject &r) {
        if (node != m_node)
            return;
        m_pub = strings(r["publishing"]);
        m_sub = strings(r["subscribing"]);
        m_nodeSrv = strings(r["services"]);
        emit nodeDetailsChanged();
    });
}

void RosApiClient::fetchServiceType(const QString &service)
{
    m_serviceType.clear();
    emit serviceTypeChanged();
    rosapi("/rosapi/service_type", {{"service", service}}, [this](const QJsonObject &r) {
        m_serviceType = r["type"].toString();
        emit serviceTypeChanged();
    });
}

QVariantMap RosApiClient::checkJson(const QString &text)
{
    QJsonParseError pe;
    const QByteArray utf8 = (text.trimmed().isEmpty() ? QString("{}") : text).toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(utf8, &pe);
    if (pe.error == QJsonParseError::NoError && !doc.isObject())
        return {{"ok", false}, {"error", "객체({ … })여야 함"}, {"offset", 0}, {"line", 1}, {"column", 1}};
    if (pe.error == QJsonParseError::NoError)
        return {{"ok", true}};
    // pe.offset 은 UTF-8 바이트 위치 → 글자 위치 · 줄 · 칸으로 바꾼다
    const QString before = QString::fromUtf8(utf8.left(pe.offset));
    const int line = before.count('\n') + 1;
    const int column = before.size() - before.lastIndexOf('\n');
    return {{"ok", false}, {"error", pe.errorString()}, {"offset", before.size()}, {"line", line}, {"column", column}};
}

void RosApiClient::callService(const QString &service, const QString &requestJson)
{
    const QVariantMap check = checkJson(requestJson);
    m_response = {};
    m_responseMs = -1;
    if (!check["ok"].toBool()) {
        m_callError = QString("요청 JSON 오류(%1줄 %2칸): %3")
                          .arg(check["line"].toInt()).arg(check["column"].toInt()).arg(check["error"].toString());
        emit responseChanged();
        return;
    }
    m_callError.clear();
    emit responseChanged();
    const QJsonObject args = QJsonDocument::fromJson(requestJson.trimmed().isEmpty() ? "{}" : requestJson.toUtf8()).object();
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();   // 직렬 대기열에 앞선 호출이 있으면 그 시간도 들어간다
    m_queue.enqueue(service, args, [this, timer](const QJsonObject &r) {
        m_response = r;
        m_responseMs = int(timer->elapsed());
        emit responseChanged();
    }, [this, timer](const QString &e) {
        m_callError = "실패: " + e;
        m_responseMs = int(timer->elapsed());
        emit responseChanged();
    });
}
