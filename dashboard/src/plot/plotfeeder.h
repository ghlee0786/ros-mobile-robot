#pragma once

#include "plotseries.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQmlListProperty>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// PlotSeries 들의 보이는 창만 솎아(M4) 짝인 QtGraphs XYSeries(targets, QML LineSeries)에
// 타이머마다 replace() 한 번으로 넣는다. X 창 · Y 범위 · 확대 · 이동 · 트래커 · 통계 · CSV 를 맡는다.
class PlotFeeder : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQmlListProperty<PlotSeries> series READ seriesList)
    Q_PROPERTY(QQmlListProperty<QObject> targets READ targetList)
    Q_PROPERTY(double windowSec READ windowSec WRITE setWindowSec NOTIFY windowSecChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(double xMin READ xMin WRITE setXMin NOTIFY xRangeChanged)
    Q_PROPERTY(double xMax READ xMax WRITE setXMax NOTIFY xRangeChanged)
    Q_PROPERTY(bool autoY READ autoY WRITE setAutoY NOTIFY autoYChanged)
    Q_PROPERTY(double yMin READ yMin WRITE setYMin NOTIFY yFixedChanged)
    Q_PROPERTY(double yMax READ yMax WRITE setYMax NOTIFY yFixedChanged)
    Q_PROPERTY(double yMinOut READ yMinOut NOTIFY yRangeChanged)
    Q_PROPERTY(double yMaxOut READ yMaxOut NOTIFY yRangeChanged)
    Q_PROPERTY(int pixelWidth READ pixelWidth WRITE setPixelWidth NOTIFY pixelWidthChanged)
    Q_PROPERTY(int intervalMs READ intervalMs WRITE setIntervalMs NOTIFY intervalMsChanged)
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY referenceChanged)
    Q_PROPERTY(double reference READ reference NOTIFY referenceChanged)

public:
    explicit PlotFeeder(QObject *parent = nullptr);

    QQmlListProperty<PlotSeries> seriesList();
    QQmlListProperty<QObject> targetList();
    QList<PlotSeries *> series() const { return m_series; }
    void addSeries(PlotSeries *s) { m_series << s; }
    void addTarget(QObject *t) { m_targets << t; }

    double windowSec() const { return m_windowSec; }
    void setWindowSec(double s);
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    bool active() const { return m_active; }
    void setActive(bool a);
    double xMin() const { return m_xMin; }
    void setXMin(double v);
    double xMax() const { return m_xMax; }
    void setXMax(double v);
    bool autoY() const { return m_autoY; }
    void setAutoY(bool a);
    double yMin() const { return m_yMin; }   // NaN 이면 고정하지 않음
    void setYMin(double v);
    double yMax() const { return m_yMax; }
    void setYMax(double v);
    double yMinOut() const { return m_yMinOut; }
    double yMaxOut() const { return m_yMaxOut; }
    int pixelWidth() const { return m_pixelWidth; }
    void setPixelWidth(int px);
    int intervalMs() const { return m_timer.interval(); }
    void setIntervalMs(int ms);
    bool hasReference() const { return !qIsNaN(m_ref); }
    double reference() const { return m_ref; }

    // 시험 · 계산용: i 번째 선에 지금 창에서 넣을 점들
    QList<QPointF> visiblePoints(int i) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantList valuesAt(double t) const;
    Q_INVOKABLE void setReference(double t);
    Q_INVOKABLE void clearReference();
    Q_INVOKABLE QVariantList statsInView() const;
    Q_INVOKABLE bool exportCsv(const QString &path, bool visibleOnly) const;
    Q_INVOKABLE void zoomTo(double t0, double t1, double y0, double y1);
    Q_INVOKABLE void zoomOut(bool horizontal, bool vertical);
    Q_INVOKABLE void pan(double dx, double dy);
    Q_INVOKABLE int fuzzyScore(const QString &query, const QString &text) const { return ::fuzzyScore(query, text); }
    Q_INVOKABLE QStringList flattenNumericPaths(const QString &topic, const QJsonObject &msg, int maxArray = 32) const
    {
        return ::flattenNumericPaths(topic, msg, maxArray);
    }

signals:
    void windowSecChanged();
    void pausedChanged();
    void activeChanged();
    void xRangeChanged();
    void autoYChanged();
    void yFixedChanged();
    void yRangeChanged();
    void pixelWidthChanged();
    void intervalMsChanged();
    void referenceChanged();
    void refreshed();

private:
    void updateX();
    void updateY(double dtSec);
    void setX(double a, double b);

    QList<PlotSeries *> m_series;
    QList<QObject *> m_targets;
    QTimer m_timer;
    QElapsedTimer m_since;
    double m_windowSec = 10;
    bool m_paused = false, m_active = true, m_autoY = true;
    double m_xMin = 0, m_xMax = 10;
    double m_yMin = qQNaN(), m_yMax = qQNaN();
    double m_yMinOut = -1, m_yMaxOut = 1;
    bool m_yInit = false;
    int m_pixelWidth = 800;
    double m_ref = qQNaN();
};
