#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSize>

class QQuickWindow;

// 명령줄 시험 모드: 로그 파일 · actions 실행 · 화면 캡처 · 종료 직전 상태(JSON) · 속성 시계열.
struct TestOptions
{
    QString logPath, screenshot, statePath, actionsPath, tracePath;
    double traceHz = 2;
    double quitAfter = -1;     // 초, 0 이하면 스스로 끝내지 않는다
    bool failOnWarning = false;
    bool testMode = false;     // stderr 에도 로그를 낸다
    QSize size;                // 비어 있으면 그대로
};

class TestHooks : public QObject
{
    Q_OBJECT
public:
    // 만들자마자 메시지 핸들러를 건다(엔진을 만들기 전에 만든다).
    explicit TestHooks(const TestOptions &opt);
    ~TestHooks() override;

    // QML 로드 뒤 루트 창을 넘긴다: 창 크기 · actions · trace · quit-after 를 시작한다.
    void start(QQuickWindow *win);
    // app.exec() 결과를 받아 최종 종료 코드를 정한다(3 actions 실패 · 2 경고 · 그 밖은 rc).
    int exitCode(int rc) const;

private:
    struct Action { QJsonObject o; double t = 0; };

    void runNext();
    bool execute(const QJsonObject &a, QString *err);
    void finish(const QString &reason);
    QObject *findObject(const QString &path) const;
    QJsonObject snapshot(bool full) const;
    void fail(const QString &msg);

    TestOptions m_opt;
    QQuickWindow *m_win = nullptr;
    QElapsedTimer m_clock;
    QList<Action> m_actions;
    int m_next = 0;
    double m_waitStart = -1;
    bool m_actionsFailed = false;
    bool m_finished = false;
    QJsonArray m_done;
    // frameSwapped 로 센 fps
    qint64 m_frames = 0, m_firstFrameMs = -1, m_lastFrameMs = -1, m_bucketStartMs = -1;
    int m_bucketFrames = 0;
    double m_fpsMin = -1;
};
