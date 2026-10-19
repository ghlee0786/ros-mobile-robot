#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QPointer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "servicequeue.h"

#include "rosbridge.h"

// 노드 파라미터 표. '/<node>/list_parameters' · describe_parameters · get_parameters · set_parameters
// (rcl_interfaces/srv)를 직접, 한 번에 한 호출씩 부른다(rosapi/set_param 은 쓰지 않는다).
// active 인 동안 /parameter_events 를 구독해 값을 갱신한다.
class ParamClient : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QString node READ node WRITE setNode NOTIFY nodeChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool writable MEMBER m_writable NOTIFY writableChanged)   // --no-publish 면 QML 이 false 로
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    Q_PROPERTY(bool lastSuccessful READ lastSuccessful NOTIFY resultChanged)
    Q_PROPERTY(QString lastReason READ lastReason NOTIFY resultChanged)

public:
    enum Type { NotSet = 0, Bool, Integer, Double, String, ByteArray, BoolArray, IntegerArray, DoubleArray, StringArray };
    enum Role { NameRole = Qt::UserRole + 1, TypeRole, TypeNameRole, ValueRole, ValueTextRole, HasRangeRole,
                MinRole, MaxRole, StepRole, ReadOnlyRole, DescriptionRole, EditorRole };
    struct Param {
        QString name, description;
        int type = NotSet;
        bool readOnly = false, hasRange = false;
        double min = 0, max = 0, step = 0;
        QVariant value;
    };

    explicit ParamClient(QObject *parent = nullptr);

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    QString node() const { return m_node; }
    void setNode(const QString &n);
    bool active() const { return m_active; }
    void setActive(bool on);
    bool busy() const { return m_queue.pending() > 0; }
    QString error() const { return m_error; }
    bool hasResult() const { return m_hasResult; }
    bool lastSuccessful() const { return m_lastOk; }
    QString lastReason() const { return m_lastReason; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &name, const QVariant &value);

    // rcl_interfaces 메시지 ↔ 값 변환(단위 시험용으로 공개)
    static QStringList parseListResult(const QJsonObject &resp);
    static QList<Param> parseDescriptors(const QJsonObject &resp);
    static QVariant parseValue(const QJsonObject &parameterValue);
    static QVariantList parseValues(const QJsonObject &resp);
    static QJsonObject makeValue(int type, const QVariant &v);   // 입력칸 문자열도 형식에 맞게 바꾼다
    static QJsonObject makeSetRequest(const QString &name, int type, const QVariant &v);
    static QPair<bool, QString> parseSetResult(const QJsonObject &resp);
    static QString typeName(int type);
    static QString valueText(const QVariant &v);

signals:
    void bridgeChanged();
    void nodeChanged();
    void activeChanged();
    void writableChanged();
    void countChanged();
    void busyChanged();
    void resultChanged();

private:
    QString service(const char *name) const { return m_node + '/' + name; }
    void updateSubscription();
    void onEvent(const QJsonObject &ev);
    void setError(const QString &e);
    int find(const QString &name) const;

    QPointer<RosBridge> m_bridge;
    ServiceQueue m_queue;
    QString m_node;
    bool m_active = false, m_subscribed = false, m_writable = true;
    QList<Param> m_params;
    QString m_error, m_lastReason;
    bool m_hasResult = false, m_lastOk = false;
    int m_gen = 0;   // 노드가 바뀌면 이전 응답을 버린다
};
