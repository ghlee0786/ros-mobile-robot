#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// 표 하나를 행(QVariantMap) 목록으로 들고 있는 목록 모델. setRows 로 통째로 넘기면
// 행 구성(첫 역할 값 = 행 키)이 같을 때는 바뀐 칸만 dataChanged 로 알린다.
class RowModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    RowModel(const QList<QByteArray> &roles, QObject *parent);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRows(const QList<QVariantMap> &rows);

signals:
    void countChanged();

private:
    QList<QByteArray> m_roles;
    QList<QVariantMap> m_rows;
};

// 토픽 표: name, type, hz, latencyMs, kbps, state, rec, clockSkew
class TopicModel : public RowModel
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    explicit TopicModel(QObject *parent = nullptr)
        : RowModel({"name", "type", "hz", "latencyMs", "kbps", "state", "rec", "clockSkew"}, parent) {}
};

// 센서 표: key, label, topic, on, state, hz, age
class SensorModel : public RowModel
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    explicit SensorModel(QObject *parent = nullptr)
        : RowModel({"key", "label", "topic", "on", "state", "hz", "age"}, parent) {}
};
