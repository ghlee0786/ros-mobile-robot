#pragma once

#include <QElapsedTimer>
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
// 3 s 동안 아무것도 받지 못하면(반쯤 열린 연결) 소켓을 끊고 같은 재연결 경로를 탄다.
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
    Q_PROPERTY(double rxAge READ rxAge NOTIFY rxChanged)                  // 마지막 메시지 수신 뒤 s, pong 은 빼고 (-1: 연결 안 됨)
    Q_PROPERTY(double rxBytesPerSec READ rxBytesPerSec NOTIFY rxChanged)  // 직전 1 s 전체 수신 바이트
    Q_PROPERTY(QList<double> rxHistory READ rxHistory NOTIFY rxChanged)   // 최근 60 s, 초당 바이트
    Q_PROPERTY(QList<double> latencyHistory READ latencyHistory NOTIFY rxChanged)   // 최근 60 개 ping ms
    Q_PROPERTY(double clockSkewMs READ clockSkewMs NOTIFY rxChanged)   // 최근 메시지 header.stamp − 수신 시각의 중앙값 ms (표본 없으면 0)
    Q_PROPERTY(int callsPending READ callsPending NOTIFY callsChanged)
    Q_PROPERTY(bool noPublish READ noPublish CONSTANT)   // ROBOT_DASHBOARD_NO_PUBLISH=1: advertise · publish 안 함

public:
    struct Stat {
        double hz = 0;          // 최근 2 s 수신 주기
        int recent = 0;         // 최근 2 s 수신 개수
        double age = -1;        // 마지막 수신 뒤 경과 s (-1: 아직 없음)
        double latencyMs = qQNaN(); // 수신 시각 - header.stamp
        double sinceSub = 0;    // 구독 뒤 경과 s
        double bytesPerSec = 0; // 최근 2 s 평균 수신 바이트/s
        qint64 totalBytes = 0;  // 구독 뒤 누적 바이트
        bool clockSkew = false; // |지연| > 5 s: PC 와 파이 시계가 어긋남
    };
    using Done = std::function<void(const QJsonObject &)>;
    using Fail = std::function<void(const QString &)>;

    explicit RosBridge(QObject *parent = nullptr);
    ~RosBridge() override;

    QString host() const { return m_host; }
    int port() const { return m_port; }
    bool connected() const { return m_connected; }
    int latencyMs() const { return m_latency; }
    int topicCount() const { return m_types.size(); }
    bool topicListReceived() const { return m_topicListReceived; }
    QString error() const { return m_error; }
    QStringList found() const { return m_found; }
    bool searching() const { return m_searching; }
    double rxAge() const { return m_connected && m_lastData.isValid() ? m_lastData.elapsed() / 1000.0 : -1; }
    double rxBytesPerSec() const { return m_rxBytesPerSec; }
    QList<double> rxHistory() const { return m_rxHistory; }
    QList<double> latencyHistory() const { return m_latencyHistory; }
    double clockSkewMs() const;
    int callsPending() const { return int(m_calls.size()); }
    bool noPublish() const { return m_noPublish; }

    Q_INVOKABLE void open(const QString &host, int port);
    Q_INVOKABLE void discover(int port = -1);

    // reason 마다 throttleMs(-1: 의견 없음)와 queueLength(-1: 기본)를 따로 둔다. 실제 throttle 은
    // max(토픽 하한, 'record' 가 아닌 이유들의 최소값)이고, 그 값 · queue · type 이 바뀔 때만 다시 subscribe 한다.
    void want(const QString &topic, const QString &type, const QString &reason, bool on, int throttleMs = 0,
              int queueLength = -1);
    bool subscribed(const QString &topic) const { return m_wanted.contains(topic); }
    int throttleOf(const QString &topic) const;
    void advertise(const QString &topic, const QString &type);
    bool publish(const QString &topic, const QJsonObject &msg);
    // 응답이 result=false 이거나 5 s 안에 오지 않거나 연결이 없으면 fail 을 부른다.
    void call(const QString &service, const QJsonObject &args, Done done, Fail fail = nullptr);
    void resubscribe();
    void shutdown();   // 보낼 것을 내보내고 닫는다(앱 종료 때)
    QHash<QString, QString> topicTypes() const { return m_types; }
    QStringList wantedTopics() const { return m_wanted.keys(); }
    QString wantedType(const QString &topic) const { return m_subType.value(topic); }
    Stat stat(const QString &topic);
    double lastWorkerMs() const { return m_lastWorkerMs; }   // message 신호 안에서: 다른 스레드 파싱 시간

signals:
    void hostChanged();
    void connectedChanged();
    void latencyChanged();
    void topicListChanged();
    void errorChanged();
    void foundChanged();
    void rxChanged();
    void callsChanged();
    void message(const QString &topic, const QJsonObject &msg);
    void bridgeError(const QString &text);   // rosbridge status op, 실패한 서비스 응답

private:
    struct Arrival { qint64 t, bytes; double stampMs; };   // 수신 시각 · 바이트 · header.stamp(없으면 NaN)
    struct Times { QList<Arrival> arrivals; qint64 last = 0; qint64 since = 0; double latency = qQNaN(); qint64 total = 0; };
    struct Call { Done done; Fail fail; QString service; };
    struct Pending { int id = 0; bool ready = false; QJsonObject o; qint64 bytes = 0; double workerMs = 0; };
    struct Sub { QString type; int throttle = 0; int queue = -1; };

    bool send(const QJsonObject &o);
    void sendSubscribe(const QString &topic);
    Sub subOf(const QString &topic) const;
    void refreshTopics();
    void onText(const QString &text);
    void process(const QJsonObject &o, qint64 bytes, double workerMs);
    void dispatch(const QJsonObject &o, qint64 bytes, double workerMs);
    void finishBig(const QString &topic, int id, const QJsonObject &o, double workerMs);
    void failCall(const QString &id, const QString &why);
    void scanNext();

    QWebSocket m_ws;
    QTimer m_retry, m_tick;
    // 단조 시계. m_lastRx 는 pong 까지(반쯤 열린 연결 판단), m_lastData 는 메시지만(rxAge — 데이터가 멈춰도
    // 서버 WebSocket 층은 ping 에 계속 답하므로 pong 으로는 링크 끊김을 알 수 없다).
    QElapsedTimer m_mono, m_lastRx, m_lastData;
    QString m_host;
    int m_port = 9090;
    bool m_connected = false;
    bool m_noPublish = false;
    int m_latency = -1;
    QString m_error;
    QHash<QString, QSet<QString>> m_wanted;   // topic -> 원하는 용도들
    QHash<QString, QString> m_subType;
    QHash<QString, QHash<QString, int>> m_throttle, m_queue;   // topic -> reason -> 값
    QHash<QString, Sub> m_sent;                // 마지막으로 보낸 subscribe
    QHash<QString, QString> m_adv;             // topic -> type
    QHash<QString, QString> m_types;           // rosapi 토픽 목록
    bool m_topicListReceived = false;
    QHash<QString, Times> m_times;
    QHash<QString, Call> m_calls;
    QHash<QString, QList<Pending>> m_pending;  // 다른 스레드에서 파싱 중인 큰 메시지 뒤로 같은 토픽 순서를 지킨다
    int m_bigId = 0;
    double m_lastWorkerMs = 0;
    int m_callId = 0;
    int m_ticks = 0;
    qint64 m_rxSecBytes = 0;
    double m_rxBytesPerSec = 0;
    QList<double> m_rxHistory, m_latencyHistory;
    QList<double> m_skew;   // 최근 kHistory 개 메시지의 stamp − 수신 시각(ms)
    QStringList m_found;
    bool m_searching = false;
    QStringList m_scanQueue;
    int m_scanActive = 0;
    quint16 m_scanPort = 9090;
};
