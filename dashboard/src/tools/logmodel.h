#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "rosbridge.h"

// /rosout(rcl_interfaces/msg/Log) 링 버퍼(최대 5000줄). active 인 동안만 구독한다(로그 탭이 보일 때).
class LogModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY countChanged)
    Q_PROPERTY(QStringList nodes READ nodes NOTIFY nodesChanged)

public:
    enum Role { StampRole = Qt::UserRole + 1, LevelRole, LevelNameRole, NameRole, MsgRole, FileRole,
                FunctionRole, LineRole, TimeTextRole };
    struct Entry { double stamp = 0; int level = 0; QString name, msg, file, function; int line = 0; };

    explicit LogModel(QObject *parent = nullptr);

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    bool active() const { return m_active; }
    void setActive(bool on);
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    int capacity() const { return m_capacity; }
    void setCapacity(int n);
    QStringList nodes() const { return m_nodes; }
    const Entry &entry(int row) const { return m_rows.at(row); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const QJsonObject &msg);   // rcl_interfaces/msg/Log 한 개
    Q_INVOKABLE void clear();
    static QString levelName(int level);

signals:
    void bridgeChanged();
    void activeChanged();
    void pausedChanged();
    void countChanged();
    void nodesChanged();

private:
    void updateSubscription();

    QPointer<RosBridge> m_bridge;
    bool m_active = false, m_paused = false, m_subscribed = false;
    int m_capacity = 5000;
    QList<Entry> m_rows;
    QStringList m_nodes;
};

// LogModel 필터: 최소 심각도(AND) · 검색어(노드 이름과 메시지 소문자 포함, 여러 개면 OR) · 노드 선택 ·
// 제외 규칙. 강조 규칙에 맞는 줄은 highlight 역할이 true. 규칙은 QSettings 에 저장한다.
// 규칙 하나 = {kind: "highlight"|"exclude", pattern: 정규식, node: 노드 이름(빈 값=모두), minLevel: 심각도}
class LogFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int minLevel READ minLevel WRITE setMinLevel NOTIFY filterChanged)
    Q_PROPERTY(QStringList terms READ terms WRITE setTerms NOTIFY filterChanged)
    Q_PROPERTY(QStringList hiddenNodes READ hiddenNodes WRITE setHiddenNodes NOTIFY filterChanged)
    Q_PROPERTY(QVariantList rules READ rules WRITE setRules NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum { HighlightRole = Qt::UserRole + 100 };
    explicit LogFilterModel(QObject *parent = nullptr);

    int minLevel() const { return m_minLevel; }
    void setMinLevel(int l);
    QStringList terms() const { return m_terms; }
    void setTerms(const QStringList &t);
    QStringList hiddenNodes() const { return m_hidden; }
    void setHiddenNodes(const QStringList &n);
    QVariantList rules() const { return m_rules; }
    void setRules(const QVariantList &r);
    int count() const { return rowCount(); }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QString tsv(const QList<int> &rows) const;   // 선택 행 TSV
    Q_INVOKABLE void copyTsv(const QList<int> &rows) const;   // 위 TSV 를 클립보드로

signals:
    void filterChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    struct Rule { bool exclude; QRegularExpression re; QString node; int minLevel; };
    bool ruleMatches(const Rule &r, const LogModel::Entry &e) const;
    void compileRules();

    int m_minLevel = 0;
    QStringList m_terms, m_hidden;
    QVariantList m_rules;
    QList<Rule> m_compiled;
};
