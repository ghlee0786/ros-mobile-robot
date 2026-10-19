#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "servicequeue.h"

#include "rosbridge.h"

// rosapi 조회(노드 · 노드 상세 · 서비스 · 서비스 형식 · 토픽)와 임의 서비스 호출.
// 파이의 rosbridge 는 파이 도메인만 보므로 PC Nav2(WSL 도메인 8) 노드는 nodes 에 없을 수 있다.
// 응답이 5 s 안에 없으면 error 에 실패 문구를 두고 rosapiSlow 를 켠다(경보 출처).
class RosApiClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QStringList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QStringList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(QStringList topics READ topics NOTIFY topicsChanged)
    Q_PROPERTY(QString node READ node NOTIFY nodeDetailsChanged)
    Q_PROPERTY(QStringList publishing READ publishing NOTIFY nodeDetailsChanged)
    Q_PROPERTY(QStringList subscribing READ subscribing NOTIFY nodeDetailsChanged)
    Q_PROPERTY(QStringList nodeServices READ nodeServices NOTIFY nodeDetailsChanged)
    Q_PROPERTY(QString serviceType READ serviceType NOTIFY serviceTypeChanged)
    Q_PROPERTY(QJsonObject response READ response NOTIFY responseChanged)
    Q_PROPERTY(int responseMs READ responseMs NOTIFY responseChanged)
    Q_PROPERTY(QString callError READ callError NOTIFY responseChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool rosapiSlow READ rosapiSlow NOTIFY errorChanged)
    Q_PROPERTY(int callsPending READ callsPending NOTIFY callsPendingChanged)

public:
    explicit RosApiClient(QObject *parent = nullptr);

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    QStringList nodes() const { return m_nodes; }
    QStringList services() const { return m_services; }
    QStringList topics() const { return m_topics; }
    QString node() const { return m_node; }
    QStringList publishing() const { return m_pub; }
    QStringList subscribing() const { return m_sub; }
    QStringList nodeServices() const { return m_nodeSrv; }
    QString serviceType() const { return m_serviceType; }
    QJsonObject response() const { return m_response; }
    int responseMs() const { return m_responseMs; }
    QString callError() const { return m_callError; }
    QString error() const { return m_error; }
    bool rosapiSlow() const { return m_slow; }
    int callsPending() const { return m_queue.pending(); }

    Q_INVOKABLE void refreshNodes();
    Q_INVOKABLE void refreshServices();
    Q_INVOKABLE void refreshTopics();
    Q_INVOKABLE void selectNode(const QString &node);
    Q_INVOKABLE void fetchServiceType(const QString &service);
    Q_INVOKABLE void callService(const QString &service, const QString &requestJson);
    // 요청 편집칸 검사: {ok, error, offset, line, column}
    Q_INVOKABLE static QVariantMap checkJson(const QString &text);

signals:
    void bridgeChanged();
    void nodesChanged();
    void servicesChanged();
    void topicsChanged();
    void nodeDetailsChanged();
    void serviceTypeChanged();
    void responseChanged();
    void errorChanged();
    void callsPendingChanged();

private:
    void rosapi(const QString &service, const QJsonObject &args, ServiceQueue::Ok ok);
    void setError(const QString &e, bool slow);

    QPointer<RosBridge> m_bridge;
    ServiceQueue m_queue;
    QStringList m_nodes, m_services, m_topics, m_pub, m_sub, m_nodeSrv;
    QString m_node, m_serviceType, m_callError, m_error;
    QJsonObject m_response;
    int m_responseMs = -1;
    bool m_slow = false;
};
