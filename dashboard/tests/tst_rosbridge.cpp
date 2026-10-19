// RosBridge 를 시험 안의 모의 rosbridge(빈 포트)에 붙여 본다: 이유별 throttle, 재연결, 서비스 실패 · 시간 초과,
// status op, 수신율 · 대역폭, 반쯤 열린 연결, 큰 메시지의 순서와 GUI 스레드 막힘.
#include "mockserver.h"
#include "rosbridge.h"

#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest>

namespace {
bool connectTo(RosBridge &b, const MockServer &s)
{
    b.open("127.0.0.1", s.port());
    return QTest::qWaitFor([&] { return b.connected() && !s.clients.isEmpty(); }, 5000);
}

// 2000×2000 OccupancyGrid publish 원문(약 8 MB)
QString bigMapText(const QString &topic, int w, int h, int marker)
{
    QString data;
    data.reserve(qsizetype(w) * h * 2 + 16);
    for (qsizetype i = 0; i < qsizetype(w) * h; ++i)
        data += i ? QStringLiteral(",0") : QStringLiteral("0");
    return QString(R"({"op":"publish","topic":"%1","msg":{"header":{"frame_id":"map","seq":%2},)"
                   R"("info":{"width":%3,"height":%4,"resolution":0.05,"origin":{"position":{"x":0,"y":0},)"
                   R"("orientation":{"w":1}}},"data":[%5]}})")
        .arg(topic).arg(marker).arg(w).arg(h).arg(data);
}

// 손으로 만든 WebSocket 서버 쪽: 핸드셰이크에 답하고 upgraded 를 표시한다(그 뒤 받는 ping 등은 무시).
void answerHandshake(QTcpSocket *c)
{
    QObject::connect(c, &QTcpSocket::readyRead, c, [c] {
        if (c->property("upgraded").toBool()) {
            c->readAll();
            return;
        }
        const QByteArray req = c->peek(c->bytesAvailable());
        if (!req.contains("\r\n\r\n"))
            return;
        c->readAll();
        QByteArray key;
        for (const QByteArray &line : req.split('\n'))
            if (line.toLower().startsWith("sec-websocket-key:"))
                key = line.mid(18).trimmed();
        const QByteArray accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                           QCryptographicHash::Sha1).toBase64();
        c->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
        c->setProperty("upgraded", true);
    });
}

// 서버 → 클라이언트 text 프레임(마스크 없음). 길이는 가장 짧은 꼴로 써야 한다(아니면 프로토콜 오류로 닫힌다).
QByteArray textFrame(const QByteArray &payload)
{
    const quint64 n = quint64(payload.size());
    QByteArray f(1, char(0x81));
    const int bytes = n < 126 ? 0 : n < 65536 ? 2 : 8;
    f.append(char(bytes == 0 ? n : bytes == 2 ? 126 : 127));
    for (int i = bytes - 1; i >= 0; --i)
        f.append(char((n >> (8 * i)) & 0xFF));
    return f + payload;
}
} // namespace

class TstRosBridge : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("ROBOT_DASHBOARD_NO_PUBLISH", "0"); }

    // 같은 토픽을 sensor + record 로 원한 뒤 하나를 꺼도 unsubscribe 하지 않고, record 가 throttle 50 을 0 으로 바꾸지 않는다.
    void throttleReasons()
    {
        MockServer s;
        RosBridge b;
        QVERIFY(connectTo(b, s));
        b.want("/imu/data_raw", "sensor_msgs/Imu", "sensor", true, 50);
        b.want("/imu/data_raw", "", "record", true, 0);
        b.want("/imu/data_raw", "", "sensor", false);
        b.want("/imu/data_raw", "", "plot", true, 0);   // 0(전속)을 원해도 하한 50
        QTest::qWait(300);
        const auto subs = s.ops("subscribe", "/imu/data_raw");
        QCOMPARE(subs.size(), 1);
        QCOMPARE(subs[0]["throttle_rate"].toInt(), 50);
        QCOMPARE(subs[0]["type"].toString(), QString("sensor_msgs/Imu"));
        QCOMPARE(s.ops("unsubscribe", "/imu/data_raw").size(), 0);
        QCOMPARE(b.throttleOf("/imu/data_raw"), 50);

        // 하한이 없는 토픽: record 는 100 을 바꾸지 않고, 다른 이유의 0 은 전속으로 바꾼다.
        b.want("/foo", "std_msgs/String", "sensor", true, 100);
        b.want("/foo", "", "record", true, 0);
        QCOMPARE(b.throttleOf("/foo"), 100);
        b.want("/foo", "", "plot", true, 0);
        QCOMPARE(b.throttleOf("/foo"), 0);
        // /tf_static 은 항상 throttle 0 · queue 10 이상, queueLength 는 그대로 보낸다.
        b.want("/tf_static", "tf2_msgs/TFMessage", "sensor", true, 500);
        b.want("/rosout", "rcl_interfaces/Log", "log", true, 0, 100);
        QTest::qWait(300);
        const auto foo = s.ops("subscribe", "/foo");
        QCOMPARE(foo.size(), 2);
        QCOMPARE(foo[0]["throttle_rate"].toInt(), 100);
        QVERIFY(!foo[1].contains("throttle_rate"));
        QVERIFY(!s.ops("subscribe", "/tf_static")[0].contains("throttle_rate"));
        QVERIFY(s.ops("subscribe", "/tf_static")[0]["queue_length"].toInt() >= 10);
        QCOMPARE(s.ops("subscribe", "/rosout")[0]["queue_length"].toInt(), 100);

        // 모든 이유가 꺼지면 그때 unsubscribe
        b.want("/imu/data_raw", "", "record", false);
        b.want("/imu/data_raw", "", "plot", false);
        QTest::qWait(200);
        QCOMPARE(s.ops("unsubscribe", "/imu/data_raw").size(), 1);
    }

    // 서버가 끊으면 3.5 s 안에 다시 붙어 advertise · subscribe 를 다시 보낸다.
    void reconnectResends()
    {
        MockServer s;
        RosBridge b;
        b.want("/scan", "sensor_msgs/LaserScan", "sensor", true);
        b.advertise("/teleop/cmd_vel", "geometry_msgs/Twist");
        QVERIFY(connectTo(b, s));
        QVERIFY(QTest::qWaitFor([&] { return s.ops("advertise").size() == 1 && s.ops("subscribe", "/scan").size() == 1; }, 2000));
        QElapsedTimer t;
        t.start();
        s.clients.first()->close();
        QVERIFY(QTest::qWaitFor([&] { return !b.connected(); }, 2000));
        QVERIFY(QTest::qWaitFor([&] { return s.ops("advertise").size() == 2 && s.ops("subscribe", "/scan").size() == 2; }, 3500));
        QVERIFY(t.elapsed() < 3500);
        QCOMPARE(s.connections, 2);
    }

    // result=false → 실패 콜백 · bridgeError
    void serviceFailure()
    {
        MockServer s;
        s.onMessage = [](QWebSocket *c, const QJsonObject &o) {
            if (o["service"].toString() == "/test/fail")
                MockServer::send(c, {{"op", "service_response"}, {"id", o["id"]}, {"result", false}, {"values", "boom"}});
        };
        RosBridge b;
        QVERIFY(connectTo(b, s));
        QSignalSpy errors(&b, &RosBridge::bridgeError);
        QString why;
        bool done = false;
        b.call("/test/fail", {}, [&](const QJsonObject &) { done = true; }, [&](const QString &w) { why = w; });
        QVERIFY(QTest::qWaitFor([&] { return !why.isEmpty(); }, 2000));
        QCOMPARE(why, QString("boom"));
        QVERIFY(!done);
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors[0][0].toString().contains("/test/fail"));
        QCOMPARE(b.callsPending(), 0);
    }

    // 5 s 동안 응답이 없으면 실패 콜백을 부르고 대기열에서 지운다.
    void serviceTimeout()
    {
        MockServer s;
        RosBridge b;
        QVERIFY(connectTo(b, s));
        QTest::qWait(200);
        QString why;
        QElapsedTimer t;
        t.start();
        b.call("/test/silent", {}, [](const QJsonObject &) {}, [&](const QString &w) { why = w; });
        QCOMPARE(b.callsPending(), 1);
        QTest::qWait(4000);
        QVERIFY(why.isEmpty());
        QVERIFY(QTest::qWaitFor([&] { return !why.isEmpty(); }, 2500));
        QVERIFY(t.elapsed() >= 4900);
        QCOMPARE(b.callsPending(), 0);
    }

    void statusOp()
    {
        MockServer s;
        RosBridge b;
        QVERIFY(connectTo(b, s));
        QSignalSpy errors(&b, &RosBridge::bridgeError);
        s.sendAll({{"op", "status"}, {"level", "error"}, {"msg", "Cannot infer topic type"}});
        QVERIFY(errors.wait(2000));
        QVERIFY(errors[0][0].toString().contains("Cannot infer topic type"));
        QVERIFY(errors[0][0].toString().contains("error"));
    }

    // 10 Hz 로 받으면 hz ≈ 10, 대역폭 > 0
    void rateAndBandwidth()
    {
        MockServer s;
        RosBridge b;
        QVERIFY(connectTo(b, s));
        b.want("/foo", "std_msgs/String", "sensor", true);
        QTimer pub;
        pub.setInterval(100);
        QObject::connect(&pub, &QTimer::timeout, [&] { s.publish("/foo", {{"data", "0123456789"}}); });
        pub.start();
        QTest::qWait(2600);
        const RosBridge::Stat st = b.stat("/foo");
        pub.stop();
        QVERIFY2(st.hz > 8 && st.hz < 12, qPrintable(QString::number(st.hz)));
        QVERIFY(st.bytesPerSec > 0);
        QVERIFY(st.totalBytes > 0);
        QVERIFY(!st.clockSkew);
        QVERIFY(b.rxBytesPerSec() > 0);
        QVERIFY(!b.rxHistory().isEmpty());
        QVERIFY(b.rxAge() >= 0 && b.rxAge() < 1);
    }

    // header.stamp 가 8 s 앞선 메시지들: clockSkewMs 는 중앙값이라 튀는 값 하나(−60 s)에 흔들리지 않는다.
    void clockSkewMedian()
    {
        MockServer s;
        RosBridge b;
        QVERIFY(connectTo(b, s));
        QCOMPARE(b.clockSkewMs(), 0.0);
        b.want("/foo", "std_msgs/Header", "sensor", true);
        QTest::qWait(200);
        auto pub = [&](double offsetMs) {
            const double ms = QDateTime::currentMSecsSinceEpoch() + offsetMs;
            s.publish("/foo", {{"header", QJsonObject{{"stamp", QJsonObject{{"sec", std::floor(ms / 1000)},
                                                                             {"nanosec", std::fmod(ms, 1000) * 1e6}}}}}});
        };
        for (int i = 0; i < 5; ++i)
            pub(8000);
        pub(-60000);
        QVERIFY(QTest::qWaitFor([&] { return b.stat("/foo").recent == 6; }, 2000));
        QVERIFY2(b.clockSkewMs() > 7500 && b.clockSkewMs() < 8100, qPrintable(QString::number(b.clockSkewMs())));
    }

    // 핸드셰이크만 하고 아무것도 보내지 않는 서버(반쯤 열린 연결): 3 s 넘게 받지 못하면 끊고 다시 붙는다.
    void halfOpenReconnects()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int connections = 0;
        QList<QTcpSocket *> socks;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *c = server.nextPendingConnection()) {
                ++connections;
                socks.append(c);
                QObject::connect(c, &QTcpSocket::readyRead, c, [c] {
                    if (c->property("upgraded").toBool())
                        return;   // 핸드셰이크 뒤에는 ping 도 무시한다
                    const QByteArray req = c->peek(c->bytesAvailable());
                    if (!req.contains("\r\n\r\n"))
                        return;
                    c->readAll();
                    QByteArray key;
                    for (const QByteArray &line : req.split('\n'))
                        if (line.toLower().startsWith("sec-websocket-key:"))
                            key = line.mid(18).trimmed();
                    const QByteArray accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                                       QCryptographicHash::Sha1).toBase64();
                    c->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
                    c->setProperty("upgraded", true);
                });
            }
        });
        RosBridge b;
        b.open("127.0.0.1", server.serverPort());
        QVERIFY(QTest::qWaitFor([&] { return b.connected(); }, 5000));
        QElapsedTimer t;
        t.start();
        double maxAge = 0;
        QTimer probe;
        probe.setInterval(50);
        QObject::connect(&probe, &QTimer::timeout, [&] { maxAge = qMax(maxAge, b.rxAge()); });
        probe.start();
        QVERIFY(QTest::qWaitFor([&] { return !b.connected(); }, 6000));
        // rxAge 가 3 s 를 넘긴 뒤(다음 1 s 점검에서) 끊는다
        QVERIFY2(t.elapsed() > 3000 && maxAge > 2.9, qPrintable(QString("%1 ms, rxAge %2").arg(t.elapsed()).arg(maxAge)));
        QVERIFY(QTest::qWaitFor([&] { return connections >= 2; }, 5000));
        qDeleteAll(socks);
    }

    // 큰 메시지는 다른 스레드에서 파싱하되 같은 토픽의 순서를 지키고, GUI 스레드를 100 ms 넘게 막지 않는다.
    // 8 MB 를 보내는 일은 실제로는 파이가 하므로 모의 서버는 다른 스레드에 둔다.
    void bigMessageOffThread()
    {
        QThread th;
        QObject ctx;
        ctx.moveToThread(&th);
        th.start();
        MockServer *srv = nullptr;
        QMetaObject::invokeMethod(&ctx, [&] { srv = new MockServer; }, Qt::BlockingQueuedConnection);
        auto onServer = [&](auto fn) { QMetaObject::invokeMethod(&ctx, fn, Qt::BlockingQueuedConnection); };
        RosBridge b;
        b.open("127.0.0.1", srv->port());
        QVERIFY(QTest::qWaitFor([&] {
            int n = 0;
            onServer([&] { n = int(srv->clients.size()); });
            return b.connected() && n > 0;
        }, 5000));
        QList<int> order;
        bool ok = false;
        QObject::connect(&b, &RosBridge::message, &b, [&](const QString &topic, const QJsonObject &msg) {
            if (topic == "/map") {
                order.append(msg["header"].toObject()["seq"].toInt());
                if (msg["data"].toArray().size() == 2000 * 2000)
                    ok = true;
            }
        });
        const QString big = bigMapText("/map", 2000, 2000, 1);
        QVERIFY(big.size() > 512 * 1024);
        onServer([&] {
            srv->sendAllText(big);
            srv->publish("/map", {{"header", QJsonObject{{"seq", 2}}}, {"info", QJsonObject{}}, {"data", QJsonArray()}});
        });

        // 5 ms 타이머의 최대 간격 = GUI 스레드가 막힌 최대 시간
        QElapsedTimer gap;
        gap.start();
        qint64 maxGap = 0;
        QTimer tick;
        tick.setInterval(5);
        QObject::connect(&tick, &QTimer::timeout, [&] { maxGap = qMax(maxGap, gap.restart()); });
        tick.start();
        QVERIFY(QTest::qWaitFor([&] { return order.size() == 2; }, 20000));
        tick.stop();
        QCOMPARE(order, QList<int>({1, 2}));
        QVERIFY(ok);
        qInfo().noquote() << QString("[perf] bridge big map 2000x2000 bytes=%1 max_gui_gap_ms=%2").arg(big.size()).arg(maxGap);
        onServer([&] { delete srv; });
        th.quit();
        th.wait();
        QVERIFY2(maxGap < 100, qPrintable(QString("GUI 스레드가 %1 ms 막힘").arg(maxGap)));
    }

    // B5: 느린 링크로 큰 지도 한 프레임이 6 s 에 걸쳐 들어온다. 서버는 그동안 pong 을 보내지 않는다(같은 줄 뒤).
    // 받는 중인 바이트를 살아 있음으로 보고 끊지 않아, 같은 연결로 지도를 끝까지 받는다. 한 번 다시 붙은 뒤의 소켓에서 본다.
    void slowBigFrameKeepsConnection()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int connections = 0;
        QList<QTcpSocket *> socks;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *c = server.nextPendingConnection()) {
                ++connections;
                socks.append(c);
                answerHandshake(c);
            }
        });
        RosBridge b;
        int disconnects = 0, maps = 0;
        QObject::connect(&b, &RosBridge::connectedChanged, &b, [&] { disconnects += !b.connected(); });
        QObject::connect(&b, &RosBridge::message, &b, [&](const QString &topic, const QJsonObject &msg) {
            maps += topic == "/map" && msg["data"].toArray().size() == 600 * 600;
        });
        b.open("127.0.0.1", server.serverPort());
        QVERIFY(QTest::qWaitFor([&] { return b.connected() && !socks.isEmpty() && socks[0]->property("upgraded").toBool(); }, 5000));
        QTest::qWait(1500);
        socks[0]->close();
        QVERIFY(QTest::qWaitFor([&] { return b.connected() && socks.size() == 2 && socks[1]->property("upgraded").toBool(); }, 5000));
        const QByteArray frame = textFrame(bigMapText("/map", 600, 600, 1).toUtf8());   // 약 720 KB
        constexpr int kChunks = 24;   // 250 ms 마다 1/24 → 6 s
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < kChunks; ++i) {
            const qsizetype n = frame.size() / kChunks + 1;
            socks[1]->write(frame.mid(i * n, n));
            QTest::qWait(250);
        }
        QVERIFY(QTest::qWaitFor([&] { return maps == 1; }, 5000));
        QVERIFY2(t.elapsed() > 5500, qPrintable(QString("%1 ms").arg(t.elapsed())));
        QCOMPARE(disconnects, 1);
        QCOMPARE(connections, 2);
        QVERIFY(b.connected());
        qDeleteAll(socks);
    }

    // B5: 서버가 붙을 때마다 큰 메시지를 직렬화하느라 4.5 s 동안 아무것도(pong 도) 보내지 못한다.
    // 처음 한 번은 3 s 기준으로 끊어도, 기준을 늘려 다음 연결은 살아남는다(같은 원인으로 끝없이 다시 붙지 않는다).
    void rxTimeoutBacksOff()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int connections = 0;
        QList<QTcpSocket *> socks;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *c = server.nextPendingConnection()) {
                ++connections;
                socks.append(c);
                answerHandshake(c);
                const QByteArray msg = textFrame(R"({"op":"publish","topic":"/odom","msg":{}})");
                auto *beat = new QTimer(c);
                QObject::connect(beat, &QTimer::timeout, c, [c, msg] { c->write(msg); });
                QTimer::singleShot(4500, c, [beat] { beat->start(200); });
            }
        });
        RosBridge b;
        int odom = 0;
        QObject::connect(&b, &RosBridge::message, &b, [&](const QString &topic, const QJsonObject &) { odom += topic == "/odom"; });
        b.open("127.0.0.1", server.serverPort());
        QVERIFY(QTest::qWaitFor([&] { return odom > 0; }, 20000));
        QTest::qWait(3000);
        QVERIFY(b.connected());
        QCOMPARE(connections, 2);
        qDeleteAll(socks);
    }

    // B29: 1 s 넘게 붙어 있던 연결을 서버가 닫으면 3 s 재시도 주기를 기다리지 않고 1 s 안에 다시 붙는다(여러 번 되풀이해도).
    void quickReconnectAfterServerClose()
    {
        MockServer s;
        RosBridge b;
        QVERIFY(connectTo(b, s));
        for (int i = 1; i <= 3; ++i) {
            QTest::qWait(1500);
            QVERIFY(!s.clients.isEmpty());
            QElapsedTimer t;
            t.start();
            s.clients.first()->close();
            QVERIFY(QTest::qWaitFor([&] { return s.connections == i + 1 && b.connected(); }, 5000));
            QVERIFY2(t.elapsed() < 1000, qPrintable(QString("%1 번째 재연결 %2 ms").arg(i).arg(t.elapsed())));
        }
    }
};

QTEST_GUILESS_MAIN(TstRosBridge)
#include "tst_rosbridge.moc"
