// 메시지 경로 문법 · 평가 · 퍼지 매칭 · 숫자 경로 펼치기
#include "plot/messagepath.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <cmath>

static QJsonObject json(const char *s) { return QJsonDocument::fromJson(s).object(); }

class TstPlotMessagePath : public QObject
{
    Q_OBJECT
private slots:
    void parseOk()
    {
        MessagePath mp;
        QVERIFY(mp.parse("/topic.field.sub[3].x"));
        QCOMPARE(mp.topic(), QString("/topic"));
        QCOMPARE(mp.parts().size(), 4);
        QCOMPARE(mp.parts()[2].kind, MessagePath::Part::Index);
        QCOMPARE(mp.parts()[2].index, 3);
        QVERIFY(mp.parse("/ns/odom.pose.pose.orientation.@yaw"));
        QCOMPARE(mp.topic(), QString("/ns/odom"));
        QCOMPARE(mp.modifier(), QString("yaw"));
        QVERIFY(mp.parse("/a.b[-2:]"));
        QCOMPARE(mp.parts()[1].kind, MessagePath::Part::Slice);
        QVERIFY(mp.parse("/a.b{name==\"left\"}.v"));
        QCOMPARE(mp.parts()[1].filter.str, QString("left"));
        QVERIFY(mp.parse("/a.b{x.y >= -1.5}"));
        QCOMPARE(mp.parts()[1].filter.op, MessagePath::Op::Ge);
        QCOMPARE(mp.parts()[1].filter.num, -1.5);
    }

    void parseErrors_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("pos");
        QTest::newRow("no slash") << "topic.x" << 0;
        QTest::newRow("empty topic") << "/" << 1;
        QTest::newRow("empty field") << "/t..x" << 3;
        QTest::newRow("unclosed bracket") << "/t.a[3" << 6;
        QTest::newRow("bad index") << "/t.a[]" << 5;
        QTest::newRow("bad op") << "/t.a{x~1}" << 6;
        QTest::newRow("bad value") << "/t.a{x==}" << 8;
        QTest::newRow("unclosed string") << "/t.a{x==\"ab}" << 8;
        QTest::newRow("unknown modifier") << "/t.a.@foo" << 6;
        QTest::newRow("after modifier") << "/t.a.@abs.x" << 9;
        QTest::newRow("bad char") << "/t.a#" << 4;
    }
    void parseErrors()
    {
        QFETCH(QString, text);
        QFETCH(int, pos);
        MessagePath mp;
        QVERIFY(!mp.parse(text));
        QVERIFY(!mp.ok());
        QVERIFY(!mp.error().isEmpty());
        QCOMPARE(mp.errorPos(), pos);
        QVERIFY(mp.evaluate(QJsonObject()).isEmpty());
    }

    void fieldsAndSlices()
    {
        const QJsonObject m = json(R"({"a":{"b":[{"x":1},{"x":2},{"x":3},{"x":4}]},"arr":[10,20,30,40,50],"flag":true})");
        MessagePath mp;
        QVERIFY(mp.parse("/t.a.b[2].x"));
        QCOMPARE(mp.evaluate(m), QList<double>{3});
        QVERIFY(mp.parse("/t.a.b[-1].x"));
        QCOMPARE(mp.evaluate(m), QList<double>{4});
        QVERIFY(mp.parse("/t.arr[1:3]"));
        QCOMPARE(mp.evaluate(m), (QList<double>{20, 30, 40}));
        QVERIFY(mp.parse("/t.arr[-2:]"));
        QCOMPARE(mp.evaluate(m), (QList<double>{40, 50}));
        QVERIFY(mp.parse("/t.arr[:-4]"));
        QCOMPARE(mp.evaluate(m), (QList<double>{10, 20}));
        QVERIFY(mp.parse("/t.a.b[:].x"));
        QCOMPARE(mp.evaluate(m), (QList<double>{1, 2, 3, 4}));
        QVERIFY(mp.parse("/t.arr"));
        QCOMPARE(mp.evaluate(m).size(), 5);
        QVERIFY(mp.parse("/t.arr[9]"));
        QVERIFY(mp.evaluate(m).isEmpty());
        QVERIFY(mp.parse("/t.flag"));
        QCOMPARE(mp.evaluate(m), QList<double>{1});
        QVERIFY(mp.parse("/t.missing.x"));
        QVERIFY(mp.evaluate(m).isEmpty());
    }

    void filters()
    {
        const QJsonObject m = json(R"({"s":[{"name":"left","v":1,"k":5},{"name":"right","v":2,"k":7},{"name":"left","v":3,"k":9}],"id":4})");
        MessagePath mp;
        QVERIFY(mp.parse("/t.s{name==\"left\"}.v"));
        QCOMPARE(mp.evaluate(m), (QList<double>{1, 3}));
        QVERIFY(mp.parse("/t.s{name!='left'}.v"));
        QCOMPARE(mp.evaluate(m), QList<double>{2});
        QVERIFY(mp.parse("/t.s{k>5}.v"));
        QCOMPARE(mp.evaluate(m), (QList<double>{2, 3}));
        QVERIFY(mp.parse("/t.s{k<=7}.v"));
        QCOMPARE(mp.evaluate(m), (QList<double>{1, 2}));
        QVERIFY(mp.parse("/t.s{k<6}.v"));
        QCOMPARE(mp.evaluate(m), QList<double>{1});
        // 객체에 붙인 필터: 맞을 때만 통과
        QVERIFY(mp.parse("/t{id==4}.s[0].v"));
        QCOMPARE(mp.evaluate(m), QList<double>{1});
        QVERIFY(mp.parse("/t{id==5}.s[0].v"));
        QVERIFY(mp.evaluate(m).isEmpty());
    }

    void parallelArrayFilter()
    {
        // sensor_msgs/JointState 형태의 /wheel_states
        const QJsonObject m = json(R"({"header":{"stamp":{"sec":1,"nanosec":0}},
            "name":["left_wheel_joint","right_wheel_joint"],
            "position":[1.5,2.5],"velocity":[0.3,-0.7],"effort":[]})");
        MessagePath mp;
        QVERIFY(mp.parse("/wheel_states.velocity[name==\"right_wheel_joint\"]"));
        QCOMPARE(mp.parts()[1].kind, MessagePath::Part::ParallelFilter);
        QCOMPARE(mp.evaluate(m), QList<double>{-0.7});
        QVERIFY(mp.parse("/wheel_states.position[name==\"left_wheel_joint\"]"));
        QCOMPARE(mp.evaluate(m), QList<double>{1.5});
        QVERIFY(mp.parse("/wheel_states.effort[name==\"left_wheel_joint\"]"));
        QVERIFY(mp.evaluate(m).isEmpty());
        QVERIFY(mp.parse("/wheel_states.velocity[name==\"nope\"]"));
        QVERIFY(mp.evaluate(m).isEmpty());
    }

    void modifiers()
    {
        const QJsonObject m = json(R"({"a":-2,"r":3.141592653589793,"d":180})");
        MessagePath mp;
        QVERIFY(mp.parse("/t.a.@abs"));
        QCOMPARE(mp.evaluate(m), QList<double>{2});
        QVERIFY(mp.parse("/t.a.@negative"));
        QCOMPARE(mp.evaluate(m), QList<double>{2});
        QVERIFY(mp.parse("/t.r.@rad2deg"));
        QVERIFY(qAbs(mp.evaluate(m).value(0) - 180) < 1e-9);
        QVERIFY(mp.parse("/t.d.@deg2rad"));
        QVERIFY(qAbs(mp.evaluate(m).value(0) - std::acos(-1.0)) < 1e-9);
        // @derivative 는 PlotSeries 가 미분 변환으로 처리한다(평가는 원값)
        QVERIFY(mp.parse("/t.a.@derivative"));
        QCOMPARE(mp.modifier(), QString("derivative"));
        QCOMPARE(mp.evaluate(m), QList<double>{-2});
    }

    void quaternionRpy()
    {
        // z 축 0.5 rad 회전
        const double yaw = 0.5;
        QJsonObject q{{"x", 0}, {"y", 0}, {"z", std::sin(yaw / 2)}, {"w", std::cos(yaw / 2)}};
        QJsonObject m{{"pose", QJsonObject{{"orientation", q}}}};
        MessagePath mp;
        QVERIFY(mp.parse("/odom.pose.orientation.@yaw"));
        QVERIFY(qAbs(mp.evaluate(m).value(0) - yaw) < 1e-9);
        QVERIFY(mp.parse("/odom.pose.orientation.@roll"));
        QVERIFY(qAbs(mp.evaluate(m).value(0)) < 1e-9);
        // x 축 0.3 rad
        QJsonObject qr{{"x", std::sin(0.15)}, {"y", 0}, {"z", 0}, {"w", std::cos(0.15)}};
        m = {{"pose", QJsonObject{{"orientation", qr}}}};
        QVERIFY(qAbs(mp.evaluate(m).value(0) - 0.3) < 1e-9);
        QVERIFY(mp.parse("/odom.pose.orientation.@pitch"));
        QVERIFY(qAbs(mp.evaluate(m).value(0)) < 1e-9);
    }

    void fuzzy()
    {
        QVERIFY(fuzzyScore("vel", "/wheel_states.velocity") > 0);
        QCOMPARE(fuzzyScore("xyz", "/odom.pose"), -1);
        QVERIFY(fuzzyScore("odom", "/odom.twist") > fuzzyScore("odom", "/o_d_o_m_long_topic"));
        QVERIFY(fuzzyScore("ox", "/odom.x") > fuzzyScore("ox", "/odometry_long_name.max"));
    }

    void flatten()
    {
        const QJsonObject m = json(R"({"a":1,"s":"str","b":{"c":2.5},"arr":[1,2,3,4,5],
            "q":{"x":0,"y":0,"z":0,"w":1}})");
        const QStringList p = flattenNumericPaths("/t", m, 3);
        QVERIFY(p.contains("/t.a"));
        QVERIFY(p.contains("/t.b.c"));
        QVERIFY(p.contains("/t.arr[2]"));
        QVERIFY(!p.contains("/t.arr[3]"));
        QVERIFY(!p.contains("/t.s"));
        QVERIFY(p.contains("/t.q.w"));
        QVERIFY(p.contains("/t.q.@yaw"));
        // 펼친 경로는 다시 파싱 · 평가된다
        for (const QString &s : p) {
            MessagePath mp;
            QVERIFY2(mp.parse(s), qPrintable(s));
            QCOMPARE(mp.evaluate(m).size(), 1);
        }
    }
};

QTEST_GUILESS_MAIN(TstPlotMessagePath)
#include "tst_plot_messagepath.moc"
