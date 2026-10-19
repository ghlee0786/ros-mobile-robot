#pragma once
// 시험용 모의 rosbridge: 127.0.0.1 의 빈 포트(0)에 WebSocket 서버를 띄워 받은 JSON 을 모으고,
// /rosapi/topics 호출에는 빈 목록으로 답한다. onMessage 로 다른 응답을 더할 수 있다.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>
#include <QWebSocketServer>
#include <functional>

struct MockServer {
    QWebSocketServer server{QStringLiteral("mock"), QWebSocketServer::NonSecureMode};
    QList<QWebSocket *> clients;
    QList<QJsonObject> received;
    int connections = 0;
    std::function<void(QWebSocket *, const QJsonObject &)> onMessage;

    MockServer()
    {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server, &QWebSocketServer::newConnection, &server, [this] {
            while (QWebSocket *c = server.nextPendingConnection()) {
                ++connections;
                clients.append(c);
                QObject::connect(c, &QWebSocket::textMessageReceived, c, [this, c](const QString &t) {
                    const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
                    received.append(o);
                    if (o["op"].toString() == "call_service" && o["service"].toString() == "/rosapi/topics")
                        send(c, {{"op", "service_response"}, {"id", o["id"]}, {"result", true},
                                 {"values", QJsonObject{{"topics", QJsonArray()}, {"types", QJsonArray()}}}});
                    if (onMessage)
                        onMessage(c, o);
                });
                QObject::connect(c, &QWebSocket::disconnected, c, [this, c] { clients.removeAll(c); });
            }
        });
    }
    ~MockServer()
    {
        for (QWebSocket *c : std::as_const(clients)) {
            QObject::disconnect(c, nullptr, nullptr, nullptr);
            c->abort();
        }
        qDeleteAll(clients);
    }

    quint16 port() const { return server.serverPort(); }
    static void send(QWebSocket *c, const QJsonObject &o)
    {
        c->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    }
    void sendAll(const QJsonObject &o)
    {
        for (QWebSocket *c : std::as_const(clients))
            send(c, o);
    }
    void sendAllText(const QString &t)
    {
        for (QWebSocket *c : std::as_const(clients))
            c->sendTextMessage(t);
    }
    void publish(const QString &topic, const QJsonObject &msg) { sendAll({{"op", "publish"}, {"topic", topic}, {"msg", msg}}); }
    // op(와 topic)이 맞는 받은 메시지들
    QList<QJsonObject> ops(const QString &op, const QString &topic = {}) const
    {
        QList<QJsonObject> out;
        for (const QJsonObject &o : received)
            if (o["op"].toString() == op && (topic.isEmpty() || o["topic"].toString() == topic))
                out.append(o);
        return out;
    }
};
