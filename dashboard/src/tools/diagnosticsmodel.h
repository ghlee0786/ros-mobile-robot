#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "rosbridge.h"

// /diagnostics(diagnostic_msgs/DiagnosticArray)를 'hardware_id: name' 키로 묶은 목록.
// 5 s 동안 새 값이 없으면 STALE. 행 순서는 그룹(고정 → Stale → Error → Warn → Ok) 순.
// 진단이 한 번도 안 왔으면 setSynthetic() 으로 받은 센서 상태로 합성한 항목을 보인다(synthetic=true).
class DiagnosticsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countsChanged)
    Q_PROPERTY(int staleCount READ staleCount NOTIFY countsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)
    Q_PROPERTY(int warnCount READ warnCount NOTIFY countsChanged)
    Q_PROPERTY(QVariantMap groupCounts READ groupCounts NOTIFY countsChanged)
    Q_PROPERTY(bool synthetic READ synthetic NOTIFY countsChanged)
    Q_PROPERTY(bool streamLost READ streamLost NOTIFY countsChanged)   // 진단 스트림이 10 s 넘게 끊김

public:
    enum Level { Ok = 0, Warn = 1, Error = 2, Stale = 3 };
    enum Role { KeyRole = Qt::UserRole + 1, NameRole, HardwareIdRole, LevelRole, LevelNameRole, MessageRole,
                ValuesRole, GroupRole, PinnedRole, SyntheticRole };
    struct Item { QString key, name, hardwareId, message; int level = 0; QVariantList values; qint64 lastMs = 0;
                  bool synthetic = false; };

    explicit DiagnosticsModel(QObject *parent = nullptr);

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    bool active() const { return m_active; }
    void setActive(bool on);
    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    int staleCount() const { return countLevel(Stale); }
    int errorCount() const { return countLevel(Error); }
    int warnCount() const { return countLevel(Warn); }
    QVariantMap groupCounts() const;
    bool synthetic() const { return m_real.isEmpty() && !m_synth.isEmpty(); }
    bool streamLost() const { return m_streamLost; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void ingest(const QJsonObject &array, qint64 nowMs);   // DiagnosticArray 한 개
    void checkStale(qint64 nowMs);                         // STALE · 끊김 판정(1 s 마다)
    // 센서 상태 행(robot.sensors 형식: label · state · detail, 하트비트 등도 같은 형식)으로 합성 항목을 만든다.
    Q_INVOKABLE void setSynthetic(const QVariantList &rows);
    Q_INVOKABLE void togglePin(const QString &key);
    Q_INVOKABLE QString groupOf(int level, bool pinned) const;
    static int levelFromState(const QString &state);       // 센서 상태 문구 → OK/WARN/ERROR/STALE
    static QString levelName(int level);

signals:
    void bridgeChanged();
    void activeChanged();
    void filterChanged();
    void countsChanged();

private:
    void rebuild();
    void updateSubscription();
    int countLevel(int level) const;

    QPointer<RosBridge> m_bridge;
    bool m_active = false, m_subscribed = false, m_streamLost = false;
    QString m_filter;
    QHash<QString, Item> m_real;
    QList<Item> m_synth;
    QStringList m_pins;
    QList<Item> m_rows;   // 보이는 행(정렬 · 필터 후)
    qint64 m_lastStream = 0;
    QTimer m_timer;
};
