#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QWebSocket>
#include <QtQml/qqmlregistration.h>
#include <functional>

// rosbridge v2 JSON 프로토콜 클라이언트. 같은 토픽을 여러 용도(센서 표시 · 기록 등)가 원하면
// 하나라도 원하는 동안만 subscribe 하고, 끊기면 3 s 마다 다시 붙어 구독 · advertise 를 되살린다.
class RosBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Robot.bridge 로 접근")
    Q_PROPERTY(QString host READ host NOTIFY hostChanged)
    Q_PROPERTY(int port READ port NOTIFY hostChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int latencyMs READ latencyMs NOTIFY latencyChanged)
    Q_PROPERTY(int topicCount READ topicCount NOTIFY topicListChanged)
    Q_PROPERTY(bool topicListReceived READ topicListReceived NOTIFY topicListChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList found READ found NOTIFY foundChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY foundChanged)

public:
    struct Stat {
        double hz = 0;          // 최근 2 s 수신 주기
        int recent = 0;         // 최근 2 s 수신 개수
        double age = -1;        // 마지막 수신 뒤 경과 s (-1: 아직 없음)
        double latencyMs = qQNaN(); // 수신 시각 - header.stamp
        double sinceSub = 0;    // 구독 뒤 경과 s
    };

    explicit RosBridge(QObject *parent = nullptr);

    QString host() const { return m_host; }
    int port() const { return m_port; }
    bool connected() const { return m_connected; }
    int latencyMs() const { return m_latency; }
    int topicCount() const { return m_types.size(); }
    bool topicListReceived() const { return m_topicListReceived; }
    QString error() const { return m_error; }
    QStringList found() const { return m_found; }
    bool searching() const { return m_searching; }

    Q_INVOKABLE void open(const QString &host, int port);
    Q_INVOKABLE void discover();

    void want(const QString &topic, const QString &type, const QString &reason, bool on, int throttleMs = 0);
    bool subscribed(const QString &topic) const { return m_wanted.contains(topic); }
    void advertise(const QString &topic, const QString &type);
    void publish(const QString &topic, const QJsonObject &msg);
    void call(const QString &service, const QJsonObject &args, std::function<void(const QJsonObject &)> done);
    void resubscribe();
    QHash<QString, QString> topicTypes() const { return m_types; }
    QStringList wantedTopics() const { return m_wanted.keys(); }
    QString wantedType(const QString &topic) const { return m_subType.value(topic); }
    Stat stat(const QString &topic);

signals:
    void hostChanged();
    void connectedChanged();
    void latencyChanged();
    void topicListChanged();
    void errorChanged();
    void foundChanged();
    void message(const QString &topic, const QJsonObject &msg);

private:
    struct Times { QList<qint64> arrivals; qint64 last = 0; qint64 since = 0; double latency = qQNaN(); };

    void send(const QJsonObject &o);
    void sendSubscribe(const QString &topic);
    void refreshTopics();
    void onText(const QString &text);

    QWebSocket m_ws;
    QTimer m_retry, m_tick;
    QString m_host;
    int m_port = 9090;
    bool m_connected = false;
    int m_latency = -1;
    QString m_error;
    QHash<QString, QSet<QString>> m_wanted;   // topic -> 원하는 용도들
    QHash<QString, QString> m_subType;
    QHash<QString, int> m_throttle;
    QHash<QString, QString> m_adv;             // topic -> type
    QHash<QString, QString> m_types;           // rosapi 토픽 목록
    bool m_topicListReceived = false;
    QHash<QString, Times> m_times;
    QHash<QString, std::function<void(const QJsonObject &)>> m_calls;
    int m_callId = 0;
    int m_ticks = 0;
    QStringList m_found;
    bool m_searching = false;
};
