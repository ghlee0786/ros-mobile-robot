// MessageTreeModel: JSON → 트리 행 수 · 경로 문자열 · 직전 메시지와 다른 필드 집합 · 큰 배열 생략 · inspect 구독.
#include "rosbridge.h"
#include "tools/messagetreemodel.h"

#include <QJsonArray>
#include <QtTest>

static int countRows(const QAbstractItemModel &m, const QModelIndex &parent = {})
{
    int n = m.rowCount(parent);
    for (int i = 0, rows = m.rowCount(parent); i < rows; ++i)
        n += countRows(m, m.index(i, 0, parent));
    return n;
}

static QModelIndex find(const QAbstractItemModel &m, const QString &path, const QModelIndex &parent = {})
{
    for (int i = 0; i < m.rowCount(parent); ++i) {
        const QModelIndex idx = m.index(i, 0, parent);
        if (idx.data(MessageTreeModel::PathRole).toString() == path)
            return idx;
        const QModelIndex sub = find(m, path, idx);
        if (sub.isValid())
            return sub;
    }
    return {};
}

static QJsonObject odom(double x, double y)
{
    return {{"header", QJsonObject{{"frame_id", "odom"}}},
            {"pose", QJsonObject{{"pose", QJsonObject{{"position", QJsonObject{{"x", x}, {"y", y}, {"z", 0}}}}}}}};
}

class TstToolsTree : public QObject
{
    Q_OBJECT
private slots:
    void rowsAndPaths()
    {
        MessageTreeModel m;
        m.setTopic("/odom");
        m.setMessage(odom(1.5, 2));
        // header, frame_id, pose, pose, position, x, y, z
        QCOMPARE(countRows(m), 8);
        QCOMPARE(m.columnCount(), 3);
        const QModelIndex x = find(m, "/odom.pose.pose.position.x");
        QVERIFY(x.isValid());
        QVERIFY(x.data(MessageTreeModel::NumericRole).toBool());
        QCOMPARE(m.index(x.row(), 1, x.parent()).data().toString(), QString("1.5"));
        QCOMPARE(m.index(x.row(), 2, x.parent()).data().toString(), QString("number"));
        QCOMPARE(m.pathOf(x), QString("/odom.pose.pose.position.x"));
        QVERIFY(!find(m, "/odom.header.frame_id").data(MessageTreeModel::NumericRole).toBool());
    }

    void diff()
    {
        MessageTreeModel m;
        m.setTopic("/odom");
        m.setCompare(true);
        m.setMessage(odom(1, 2));
        QVERIFY(!m.hasPrevious());
        m.setMessage(odom(1, 2));
        QVERIFY(m.hasPrevious());
        QCOMPARE(m.diffCount(), 0);   // '차이 없음'
        m.setMessage(odom(1.25, 2));
        QCOMPARE(m.diffCount(), 1);
        QCOMPARE(m.changedPaths(), (QSet<QString>{"/odom.pose.pose.position.x", "/odom.pose.pose.position",
                                                  "/odom.pose.pose", "/odom.pose"}));
        QVERIFY(find(m, "/odom.pose.pose.position.x").data(MessageTreeModel::ChangedRole).toBool());
        QVERIFY(!find(m, "/odom.pose.pose.position.y").data(MessageTreeModel::ChangedRole).toBool());
        m.setCompare(false);
        QVERIFY(!find(m, "/odom.pose.pose.position.x").data(MessageTreeModel::ChangedRole).toBool());
    }

    void bigArray()
    {
        QJsonArray ranges;
        for (int i = 0; i < 360; ++i)
            ranges.append(i * 0.01);
        MessageTreeModel m;
        m.setTopic("/scan");
        m.setMessage({{"ranges", ranges}});
        const QModelIndex r = m.index(0, 0);
        QCOMPARE(m.rowCount(r), MessageTreeModel::kArrayLimit + 1);
        const QModelIndex more = m.index(MessageTreeModel::kArrayLimit, 0, r);
        QVERIFY(more.data(MessageTreeModel::MoreRole).toBool());
        QCOMPARE(more.data().toString(), QString("… 328개 더"));
        QCOMPARE(m.index(3, 0, r).data(MessageTreeModel::PathRole).toString(), QString("/scan.ranges[3]"));
        QCOMPARE(countRows(m), 1 + 33);
    }

    void inspectSubscription()
    {
        RosBridge b;
        MessageTreeModel m;
        m.setBridge(&b);
        m.setTopic("/odom");
        QVERIFY(b.subscribed("/odom"));
        emit b.message("/odom", odom(3, 4));
        QCOMPARE(m.messageCount(), 1);
        QVERIFY(find(m, "/odom.pose.pose.position.y").isValid());
        m.setTopic({});
        QVERIFY(!b.subscribed("/odom"));
        QCOMPARE(m.rowCount(), 0);
    }
};

QTEST_MAIN(TstToolsTree)
#include "tst_tools_tree.moc"
