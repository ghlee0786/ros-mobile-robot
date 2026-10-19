#pragma once

#include "scenesource.h"

#include <QQuaternion>
#include <QTimer>
#include <QVariantList>
#include <QVector3D>

// /tf · /tf_static 으로 프레임 트리를 만들고 프레임마다 고정 프레임(fixedFrame) 기준 자세를 구한다.
// 정적이 아닌 프레임은 RViz 처럼 timeoutSec 동안 페이드한다(1/3 정상 → 1/3 회색 보간 → 1/3 알파 감소).
// /tf_static 은 연결 때 한 번만 오므로(같은 토픽을 다른 용도가 이미 구독 중이면 다시 오지 않음) 꺼져 있어도
// bridge 로 지나가는 것을 받아 두고, 끌 때도 정적 프레임은 남긴다.
class TfTree : public SceneSource
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString fixedFrame READ fixedFrame WRITE setFixedFrame NOTIFY settingsChanged)
    Q_PROPERTY(double timeoutSec MEMBER m_timeout NOTIFY settingsChanged)
    // [{name, parent, linked(부모 자세를 앎), position, rotation, parentPosition, isStatic, age, gray, alpha}]
    Q_PROPERTY(QVariantList frames READ frames NOTIFY framesChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY framesChanged)

public:
    struct Fade { double gray = 0, alpha = 1; };
    static Fade fade(double age, double timeout, bool isStatic);

    explicit TfTree(QObject *parent = nullptr);

    QString fixedFrame() const { return m_fixed; }
    void setFixedFrame(const QString &f);
    QVariantList frames() const { return m_list; }
    int frameCount() const { return int(m_list.size()); }

    void ingest(const QJsonObject &msg, bool isStatic, qint64 nowMs);
    void update(qint64 nowMs);   // frames 를 다시 만든다

signals:
    void settingsChanged();
    void framesChanged();

protected:
    QList<Sub> topics() const override;
    void handle(const QString &topic, const QJsonObject &msg) override;

private:
    struct Tf { QVector3D t; QQuaternion q; };
    struct Frame { QString parent; Tf tf; qint64 recv = 0; bool isStatic = false; };
    static Tf mul(const Tf &a, const Tf &b);
    static Tf inv(const Tf &a);
    QString rootOf(const QString &name, Tf &rootToName) const;

    QHash<QString, Frame> m_frames;   // child → 부모 기준 변환
    QString m_fixed = QStringLiteral("odom");
    double m_timeout = 15;
    QVariantList m_list;
    QTimer m_tick;
    QMetaObject::Connection m_staticConn;
};
