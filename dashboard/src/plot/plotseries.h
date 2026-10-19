#pragma once

#include "messagepath.h"
#include "rosbridge.h"
#include "timebuffer.h"
#include "transforms.h"

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <vector>

// 그래프 선 하나의 데이터. path(메시지 경로)로 RosBridge 토픽을 구독해 값을 버퍼에 쌓거나,
// path 없이 push(t, v) 로 직접 넣는다. 시각은 앱 시작 기준 초(PlotSeries::now()).
class PlotSeries : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RosBridge *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString transform READ transform WRITE setTransform NOTIFY transformChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool steps MEMBER m_steps NOTIFY stepsChanged)   // 계단으로 그린다(목표 · 명령 값)
    Q_PROPERTY(QString timeSource READ timeSource WRITE setTimeSource NOTIFY timeSourceChanged)
    Q_PROPERTY(int throttleMs READ throttleMs WRITE setThrottleMs NOTIFY throttleMsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(double lastValue READ lastValue NOTIFY updated)
    Q_PROPERTY(int count READ count NOTIFY updated)

public:
    explicit PlotSeries(QObject *parent = nullptr);
    ~PlotSeries() override;

    RosBridge *bridge() const { return m_bridge; }
    void setBridge(RosBridge *b);
    QString path() const { return m_path; }
    void setPath(const QString &p);
    QString transform() const { return m_transform; }
    void setTransform(const QString &t);
    QString name() const { return m_name; }
    void setName(const QString &n);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool e);
    QString timeSource() const { return m_timeSource; }
    void setTimeSource(const QString &s);
    int throttleMs() const { return m_throttleMs; }
    void setThrottleMs(int ms);
    QString error() const { return m_error; }
    double lastValue() const { return data().isEmpty() ? qQNaN() : data().last().y(); }
    int count() const { return data().size(); }
    bool steps() const { return m_steps; }

    QString topic() const { return m_mp.ok() ? m_mp.topic() : QString(); }
    double headerOffset() const { return m_stampOffset; } // header 기준일 때 stamp + offset = 그래프 시각
    // 그리는 데이터(변환이 있으면 변환 결과, 없으면 원 데이터)
    const TimeBuffer &data() const { return m_chain.empty() ? m_raw : m_out; }

    Q_INVOKABLE void push(double t, double v);
    Q_INVOKABLE void clear();
    Q_INVOKABLE static double now();

signals:
    void bridgeChanged();
    void pathChanged();
    void transformChanged();
    void nameChanged();
    void colorChanged();
    void enabledChanged();
    void stepsChanged();
    void timeSourceChanged();
    void throttleMsChanged();
    void errorChanged();
    void updated();

private:
    void resubscribe();
    void unsubscribe();
    void rebuildChain();
    void onMessage(const QString &topic, const QJsonObject &msg);
    void feed(double t, double v);

    QPointer<RosBridge> m_bridge;
    QString m_path, m_transform, m_name, m_timeSource = QStringLiteral("receive");
    QColor m_color = Qt::white;
    bool m_enabled = true, m_steps = false;
    int m_throttleMs = -1;  // -1: 토픽별 센서 기본값(RosBridge 가 정한다)
    QString m_error, m_pathError, m_transformError;
    MessagePath m_mp;
    QString m_reason;       // "plot:<id>"
    QString m_subTopic;     // 지금 구독 중인 토픽
    std::vector<std::unique_ptr<Transform>> m_chain;
    TimeBuffer m_raw, m_out;
    double m_stampOffset = qQNaN();
    QMetaObject::Connection m_msgConn, m_listConn;
};
