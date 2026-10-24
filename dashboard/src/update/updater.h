#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QFile;
class QNetworkReply;

// GitHub Releases 의 최신 릴리스를 조회해 새 버전을 알리고, 설치 파일을 받아 실행한다.
// 시작 10 s 뒤 · 그 뒤 6 시간마다 자동 조회(설정 updateCheck 가 false 면 하지 않음).
// 주소는 ROBOT_DASHBOARD_UPDATE_URL 로 바꿀 수 있다(시험용).
class Updater : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY changed)          // idle · checking · available · skipped · current · failed
    Q_PROPERTY(bool available READ available NOTIFY changed)     // 배너를 띄울 새 버전이 있음(건너뛰기 · 나중에 아님)
    Q_PROPERTY(QString latestTag READ latestTag NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)          // 릴리스 body(Markdown)
    Q_PROPERTY(QString message READ message NOTIFY changed)      // 수동 확인 결과 · 내려받기 오류 · 주행 중 안내
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)     // 0..1
    Q_PROPERTY(bool checkEnabled READ checkEnabled WRITE setCheckEnabled NOTIFY changed)
    Q_PROPERTY(bool driving MEMBER m_driving NOTIFY changed)     // 조종 중이면 설치를 막는다(QML 이 묶는다)

public:
    struct Release {
        QString tag, body, assetName, assetUrl;
        qint64 assetSize = -1;
    };
    // "v1.2.3" · "1.2.3-rc1" 처럼 앞의 v 와 '-' · '+' 뒤(pre-release · build)는 무시하고 숫자만 비교한다. <0 · 0 · >0
    static int compareVersions(const QString &a, const QString &b);
    // releases/latest JSON → tag · body · ROSMonitor-Setup-*.exe(옛 이름 RobotDashboard-Setup-*.exe) asset. tag 가 없으면 ok=false
    static Release parseRelease(const QByteArray &json, bool *ok);

    explicit Updater(QObject *parent = nullptr);

    QString currentVersion() const;
    QString state() const { return m_state; }
    bool available() const { return m_state == "available" && !m_dismissed; }
    QString latestTag() const { return m_rel.tag; }
    QString notes() const { return m_rel.body; }
    QString message() const { return m_message; }
    bool downloading() const { return m_download; }
    double progress() const { return m_progress; }
    bool checkEnabled() const;
    void setCheckEnabled(bool on);

    Q_INVOKABLE void check(bool manual = false);
    Q_INVOKABLE void skip();                 // 이 버전 건너뛰기(설정 skipVersion 에 기억)
    Q_INVOKABLE void later();                // 이번 실행 동안 배너를 닫는다
    Q_INVOKABLE void dismissMessage();
    Q_INVOKABLE void downloadAndInstall();

signals:
    void changed();

private:
    void finishCheck(QNetworkReply *r, bool manual);
    void finishDownload();
    void setMessage(const QString &m);

    QNetworkAccessManager m_net;
    QTimer m_timer;
    QPointer<QNetworkReply> m_reply;
    QFile *m_file = nullptr;
    Release m_rel;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    bool m_dismissed = false;
    bool m_download = false;
    bool m_driving = false;
    double m_progress = 0;
};
