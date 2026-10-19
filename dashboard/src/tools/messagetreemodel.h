#pragma once

#include <QAbstractItemModel>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QSet>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <vector>

#include "rosbridge.h"

// QJsonObject 를 키 · 값 · 형식 3열 트리로 보이는 모델(원문 메시지 · 서비스 응답).
// topic 을 주면 그 토픽을 'inspect' 이유로 throttle 200 ms 구독하고, 빈 값이면 끈다.
// compare 가 켜져 있으면 직전 메시지와 다른 필드(와 그 조상)를 changed 역할로 표시한다.
// 32개보다 긴 배열은 앞 32개만 두고 '… n개 더' 행으로 줄인다.
class MessageTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QString topic READ topic WRITE setTopic NOTIFY topicChanged)
    Q_PROPERTY(bool compare READ compare WRITE setCompare NOTIFY compareChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY messageChanged)
    Q_PROPERTY(int diffCount READ diffCount NOTIFY messageChanged)   // 바뀐 잎 필드 수
    Q_PROPERTY(int messageCount READ messageCount NOTIFY messageChanged)
    Q_PROPERTY(QJsonObject message READ message NOTIFY messageChanged)   // 지금 보이는 메시지(필드 경로 검색용)

public:
    static constexpr int kArrayLimit = 32;
    enum Role { KeyRole = Qt::UserRole + 1, ValueRole, TypeRole, PathRole, ChangedRole, NumericRole, MoreRole };

    explicit MessageTreeModel(QObject *parent = nullptr);
    ~MessageTreeModel() override;

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    QString topic() const { return m_topic; }
    void setTopic(const QString &t);
    bool compare() const { return m_compare; }
    void setCompare(bool on);
    bool hasPrevious() const { return !m_prev.isEmpty(); }
    int diffCount() const { return m_diffCount; }
    int messageCount() const { return m_messageCount; }
    QJsonObject message() const { return m_cur; }
    QSet<QString> changedPaths() const { return m_changed; }

    Q_INVOKABLE void setMessage(const QJsonObject &msg);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString pathOf(const QModelIndex &index) const;
    Q_INVOKABLE void copyPath(const QModelIndex &index) const;   // 경로 문자열을 클립보드로

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void bridgeChanged();
    void topicChanged();
    void compareChanged();
    void messageChanged();

private:
    struct Node;
    std::unique_ptr<Node> build(const QString &key, const QString &path, const QJsonValue &v, Node *parent, int row) const;
    bool sameShape(const Node *a, const Node *b) const;
    void copyValues(Node *dst, Node *src);
    void updateSubscription(bool on);
    void computeDiff(const QJsonObject &prev, const QJsonObject &cur);

    QPointer<RosBridge> m_bridge;
    QString m_topic, m_subTopic;
    bool m_compare = false;
    std::unique_ptr<Node> m_root;
    QJsonObject m_cur, m_prev;
    QSet<QString> m_changed;
    int m_diffCount = 0, m_messageCount = 0;
};
