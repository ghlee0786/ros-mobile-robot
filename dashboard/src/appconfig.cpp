#include "appconfig.h"

#include <QJSEngine>

AppConfig *AppConfig::instance()
{
    static AppConfig cfg;
    return &cfg;
}

AppConfig *AppConfig::create(QQmlEngine *, QJSEngine *)
{
    // 엔진이 지우지 않게 C++ 소유로 둔다(main.cpp 가 값을 채운 같은 객체).
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}
