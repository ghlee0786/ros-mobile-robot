// LogModel · LogFilterModel: 심각도 AND · 검색어 OR · 노드 필터 · 제외/강조 규칙 · 링 버퍼 상한 · /rosout 구독.
#include "rosbridge.h"
#include "tools/logmodel.h"

#include <QtTest>

static QJsonObject logMsg(int level, const QString &name, const QString &msg, int sec = 0)
{
    return {{"stamp", QJsonObject{{"sec", sec}, {"nanosec", 0}}}, {"level", level}, {"name", name}, {"msg", msg},
            {"file", "a.cpp"}, {"function", "f"}, {"line", 7}};
}

class TstToolsLogModel : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("RobotDashboardUnitTest");
        QSettings().remove("inspector");
    }

    void filters()
    {
        LogModel m;
        m.append(logMsg(10, "straight_drive", "debug detail"));
        m.append(logMsg(20, "bringup", "Robot Ready"));
        m.append(logMsg(30, "straight_drive", "slip detected"));
        m.append(logMsg(40, "lidar", "timeout"));
        m.append(logMsg(50, "bringup", "fatal crash"));
        QCOMPARE(m.rowCount(), 5);
        QCOMPARE(m.nodes(), (QStringList{"bringup", "lidar", "straight_drive"}));
        QCOMPARE(m.index(1).data(LogModel::LevelNameRole).toString(), QString("INFO"));
        QCOMPARE(m.index(1).data(LogModel::LineRole).toInt(), 7);

        LogFilterModel f;
        f.setSourceModel(&m);
        QCOMPARE(f.count(), 5);

        f.setMinLevel(30);   // WARN 이상
        QCOMPARE(f.count(), 3);

        f.setTerms({"READY", "slip"});   // 소문자 포함 OR — 심각도와는 AND
        QCOMPARE(f.count(), 1);
        QCOMPARE(f.index(0, 0).data(LogModel::MsgRole).toString(), QString("slip detected"));

        f.setMinLevel(0);
        QCOMPARE(f.count(), 2);
        f.setTerms({"straight"});   // 노드 이름도 찾는다
        QCOMPARE(f.count(), 2);

        f.setTerms({});
        f.setHiddenNodes({"bringup"});
        QCOMPARE(f.count(), 3);
        f.setHiddenNodes({});

        f.setRules({QVariantMap{{"kind", "exclude"}, {"pattern", "^time"}, {"node", ""}, {"minLevel", 0}},
                    QVariantMap{{"kind", "highlight"}, {"pattern", "crash"}, {"node", "bringup"}, {"minLevel", 40}}});
        QCOMPARE(f.count(), 4);
        int highlighted = 0;
        for (int i = 0; i < f.rowCount(); ++i)
            highlighted += f.index(i, 0).data(LogFilterModel::HighlightRole).toBool();
        QCOMPARE(highlighted, 1);
        QCOMPARE(QSettings().value("inspector/logRules").toList().size(), 2);   // 규칙 저장

        const QStringList cols = f.tsv({0}).split('\t');
        QCOMPARE(cols.size(), 7);
        QCOMPARE(cols[1], QString("DEBUG"));
        QCOMPARE(cols[2], QString("straight_drive"));
        f.setRules({});
    }

    void ringBuffer()
    {
        LogModel m;
        QCOMPARE(m.capacity(), 5000);
        for (int i = 0; i < 5100; ++i)
            m.append(logMsg(20, "n", QString::number(i)));
        QCOMPARE(m.rowCount(), 5000);
        QCOMPARE(m.index(0).data(LogModel::MsgRole).toString(), QString("100"));
        QCOMPARE(m.index(4999).data(LogModel::MsgRole).toString(), QString("5099"));

        m.setPaused(true);
        m.append(logMsg(20, "n", "x"));
        QCOMPARE(m.index(4999).data(LogModel::MsgRole).toString(), QString("5099"));
    }

    void subscribesOnlyWhenActive()
    {
        RosBridge b;
        LogModel m;
        m.setBridge(&b);
        QVERIFY(!b.subscribed("/rosout"));
        emit b.message("/rosout", logMsg(20, "n", "ignored"));
        QCOMPARE(m.rowCount(), 0);
        m.setActive(true);
        QVERIFY(b.subscribed("/rosout"));
        emit b.message("/rosout", logMsg(20, "n", "hello"));
        QCOMPARE(m.rowCount(), 1);
        m.setActive(false);
        QVERIFY(!b.subscribed("/rosout"));
    }
};

QTEST_MAIN(TstToolsLogModel)
#include "tst_tools_logmodel.moc"
