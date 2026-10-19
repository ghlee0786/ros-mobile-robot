// 시험 기반이 도는지 보는 최소 시험: RosBridge 를 만들면 연결 안 됨 상태로 시작한다.
#include "rosbridge.h"

#include <QtTest>

class TstBridge : public QObject
{
    Q_OBJECT
private slots:
    void startsDisconnected()
    {
        RosBridge b;
        QVERIFY(!b.connected());
    }
};

QTEST_MAIN(TstBridge)
#include "tst_bridge.moc"
