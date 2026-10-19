// ParamClient: rcl_interfaces 응답 JSON 해석 · set 요청 생성, 그리고 가짜 rosbridge(QWebSocketServer, port 0)로
// list → describe → get 이 한 번에 하나씩 가고 set 이 set_parameters 로 가는지. RosApiClient 의 시간 초과도 본다.
#include "rosbridge.h"
#include "tools/paramclient.h"
#include "tools/rosapiclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>

static QJsonObject pv(int type, const char *field, const QJsonValue &v)
{
    return {{"type", type}, {field, v}};
}

// call_service 만 흉내 내는 rosbridge. silent 에 든 서비스는 답하지 않는다.
class FakeBridge : public QObject
{
public:
    QWebSocketServer server{"fake", QWebSocketServer::NonSecureMode};
    QStringList calls;
    QList<QJsonObject> args;
    QStringList silent;
    int inFlightMax = 0, inFlight = 0;

    FakeBridge()
    {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QWebSocketServer::newConnection, this, [this] {
            QWebSocket *ws = server.nextPendingConnection();
            connect(ws, &QWebSocket::textMessageReceived, this, [this, ws](const QString &text) {
                const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
                if (o["op"] != "call_service")
                    return;
                const QString svc = o["service"].toString();
                calls.append(svc);
                args.append(o["args"].toObject());
                if (silent.contains(svc))
                    return;
                const bool param = svc.contains("/straight_drive/");
                if (param)
                    inFlightMax = qMax(inFlightMax, ++inFlight);
                QTimer::singleShot(20, ws, [this, ws, o, svc, param] {   // 늦게 답해 직렬 여부를 드러낸다
                    inFlight -= param;
                    QJsonObject values;
                    if (svc.endsWith("list_parameters"))
                        values = {{"result", QJsonObject{{"names", QJsonArray{"max_speed", "enabled"}}, {"prefixes", QJsonArray()}}}};
                    else if (svc.endsWith("describe_parameters"))
                        values = {{"descriptors", QJsonArray{
                            QJsonObject{{"name", "max_speed"}, {"type", 3}, {"description", "m/s"}, {"read_only", false},
                                        {"floating_point_range", QJsonArray{QJsonObject{{"from_value", 0.0}, {"to_value", 0.5}, {"step", 0.01}}}},
                                        {"integer_range", QJsonArray()}},
                            QJsonObject{{"name", "enabled"}, {"type", 1}}}}};
                    else if (svc.endsWith("get_parameters"))
                        values = {{"values", QJsonArray{pv(3, "double_value", 0.3), pv(1, "bool_value", true)}}};
                    else if (svc.endsWith("set_parameters"))
                        values = {{"results", QJsonArray{QJsonObject{{"successful", true}, {"reason", ""}}}}};
                    else if (svc == "/rosapi/nodes")
                        values = {{"nodes", QJsonArray{"/straight_drive", "/bringup"}}};
                    ws->sendTextMessage(QJsonDocument(QJsonObject{{"op", "service_response"}, {"id", o["id"]},
                                                                  {"service", svc}, {"values", values},
                                                                  {"result", true}}).toJson(QJsonDocument::Compact));
                });
            });
        });
    }
};

class TstToolsParams : public QObject
{
    Q_OBJECT
private slots:
    void parse()
    {
        QCOMPARE(ParamClient::parseListResult({{"result", QJsonObject{{"names", QJsonArray{"a", "b"}}}}}),
                 (QStringList{"a", "b"}));

        const auto d = ParamClient::parseDescriptors({{"descriptors", QJsonArray{
            QJsonObject{{"name", "kp"}, {"type", 3}, {"read_only", true},
                        {"floating_point_range", QJsonArray{QJsonObject{{"from_value", -1.0}, {"to_value", 2.0}, {"step", 0.0}}}}},
            QJsonObject{{"name", "n"}, {"type", 2}, {"integer_range", QJsonArray{QJsonObject{{"from_value", 1}, {"to_value", 9}, {"step", 1}}}}},
            QJsonObject{{"name", "s"}, {"type", 4}}}}});
        QCOMPARE(d.size(), 3);
        QVERIFY(d[0].hasRange && d[0].readOnly);
        QCOMPARE(d[0].min, -1.0);
        QCOMPARE(d[0].max, 2.0);
        QVERIFY(d[1].hasRange);
        QCOMPARE(d[1].max, 9.0);
        QVERIFY(!d[2].hasRange);

        const QVariantList v = ParamClient::parseValues({{"values", QJsonArray{
            pv(1, "bool_value", true), pv(2, "integer_value", 42), pv(3, "double_value", 0.25),
            pv(4, "string_value", "x"), pv(8, "double_array_value", QJsonArray{1.5, 2.5})}}});
        QCOMPARE(v.size(), 5);
        QCOMPARE(v[0].toBool(), true);
        QCOMPARE(v[1].toLongLong(), 42);
        QCOMPARE(v[2].toDouble(), 0.25);
        QCOMPARE(v[3].toString(), QString("x"));
        QCOMPARE(ParamClient::valueText(v[4]), QString("[1.5,2.5]"));

        QCOMPARE(ParamClient::parseSetResult({{"results", QJsonArray{QJsonObject{{"successful", false}, {"reason", "read only"}}}}}),
                 (QPair<bool, QString>{false, "read only"}));
        QCOMPARE(ParamClient::parseSetResult({}).first, false);
    }

    void makeRequest()
    {
        const QJsonObject r = ParamClient::makeSetRequest("max_speed", ParamClient::Double, "0.35");
        const QJsonObject p = r["parameters"].toArray()[0].toObject();
        QCOMPARE(p["name"].toString(), QString("max_speed"));
        QCOMPARE(p["value"]["type"].toInt(), 3);
        QCOMPARE(p["value"]["double_value"].toDouble(), 0.35);

        QCOMPARE(ParamClient::makeValue(ParamClient::Integer, "12")["integer_value"].toInteger(), 12);
        QCOMPARE(ParamClient::makeValue(ParamClient::Integer, 3.6)["integer_value"].toInteger(), 4);
        QCOMPARE(ParamClient::makeValue(ParamClient::Bool, "true")["bool_value"].toBool(), true);
        QCOMPARE(ParamClient::makeValue(ParamClient::Bool, false)["bool_value"].toBool(), false);
        QCOMPARE(ParamClient::makeValue(ParamClient::IntegerArray, "[1, 2, 3]")["integer_array_value"].toArray().size(), 3);
        QCOMPARE(ParamClient::makeValue(ParamClient::StringArray, QVariantList{"a"})["string_array_value"].toArray().size(), 1);
    }

    void serialCalls()
    {
        FakeBridge fake;
        RosBridge b;
        b.open("127.0.0.1", fake.server.serverPort());
        QTRY_VERIFY(b.connected());

        ParamClient c;
        c.setBridge(&b);
        c.setNode("straight_drive");
        QCOMPARE(c.node(), QString("/straight_drive"));
        c.refresh();
        QTRY_COMPARE(c.rowCount(), 2);
        QVERIFY(!c.busy());
        QCOMPARE(fake.inFlightMax, 1);   // 한 번에 하나씩
        QStringList paramCalls = fake.calls.filter("/straight_drive/");
        QCOMPARE(paramCalls, (QStringList{"/straight_drive/list_parameters", "/straight_drive/describe_parameters",
                                          "/straight_drive/get_parameters"}));
        const int i = c.index(0).data(ParamClient::NameRole).toString() == "max_speed" ? 0 : 1;
        QCOMPARE(c.index(i).data(ParamClient::EditorRole).toString(), QString("slider"));
        QCOMPARE(c.index(1 - i).data(ParamClient::EditorRole).toString(), QString("switch"));
        QCOMPARE(c.index(i).data(ParamClient::ValueRole).toDouble(), 0.3);

        c.set("max_speed", 0.2);
        QTRY_VERIFY(c.hasResult());
        QVERIFY(c.lastSuccessful());
        QCOMPARE(fake.calls.last(), QString("/straight_drive/set_parameters"));
        QCOMPARE(fake.args.last()["parameters"][0]["value"]["double_value"].toDouble(), 0.2);
        QCOMPARE(c.index(i).data(ParamClient::ValueRole).toDouble(), 0.2);

        // /parameter_events 로 값 갱신
        c.setActive(true);
        QVERIFY(b.subscribed("/parameter_events"));
        emit b.message("/parameter_events", {{"node", "/straight_drive"},
            {"changed_parameters", QJsonArray{QJsonObject{{"name", "max_speed"}, {"value", pv(3, "double_value", 0.45)}}}}});
        QCOMPARE(c.index(i).data(ParamClient::ValueRole).toDouble(), 0.45);

        // 쓰기 막힘
        c.setProperty("writable", false);
        const int before = fake.calls.size();
        c.set("max_speed", 0.1);
        QVERIFY(!c.error().isEmpty());
        QTest::qWait(50);
        QCOMPARE(fake.calls.size(), before);
    }

    void rosapiTimeout()
    {
        FakeBridge fake;
        fake.silent = {"/rosapi/services"};
        RosBridge b;
        b.open("127.0.0.1", fake.server.serverPort());
        QTRY_VERIFY(b.connected());

        RosApiClient api;
        api.setBridge(&b);
        api.refreshNodes();
        QTRY_COMPARE(api.nodes(), (QStringList{"/bringup", "/straight_drive"}));
        QVERIFY(!api.rosapiSlow());

        api.refreshServices();
        QTRY_VERIFY_WITH_TIMEOUT(api.rosapiSlow(), 7000);   // 5 s 안에 응답 없음
        QVERIFY(api.error().contains("/rosapi/services"));
        QCOMPARE(api.callsPending(), 0);

        const QVariantMap bad = RosApiClient::checkJson("{\n  \"a\": 1,\n  \"b\": }");
        QVERIFY(!bad["ok"].toBool());
        QCOMPARE(bad["line"].toInt(), 3);
        QVERIFY(RosApiClient::checkJson("").value("ok").toBool());
        QVERIFY(!RosApiClient::checkJson("[1]").value("ok").toBool());
    }
};

QTEST_MAIN(TstToolsParams)
#include "tst_tools_params.moc"
