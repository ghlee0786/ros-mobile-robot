#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// 명령줄(main.cpp 가 파싱)에서 정한 실행 설정을 QML 에 내놓는다. Main.qml 이 자동 연결 · 첫 화면에 쓴다.
class AppConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString host MEMBER host CONSTANT)          // --url 의 주소, 없으면 ""
    Q_PROPERTY(int port MEMBER port CONSTANT)              // --url 의 포트, 없으면 0
    Q_PROPERTY(bool autoConnect MEMBER autoConnect CONSTANT)
    Q_PROPERTY(int page MEMBER page CONSTANT)              // -1 이면 지정 없음
    Q_PROPERTY(bool testMode MEMBER testMode CONSTANT)

public:
    QString host;
    int port = 0;
    bool autoConnect = true;
    int page = -1;
    bool testMode = false;

    static AppConfig *instance();
    static AppConfig *create(QQmlEngine *, QJSEngine *);

private:
    AppConfig() = default;   // 기본 생성자가 보이면 QML 이 create() 대신 새로 만든다
};
