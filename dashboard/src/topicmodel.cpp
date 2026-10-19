#include "topicmodel.h"

RowModel::RowModel(const QList<QByteArray> &roles, QObject *parent) : QAbstractListModel(parent), m_roles(roles) {}

int RowModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

QVariant RowModel::data(const QModelIndex &index, int role) const
{
    const int r = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_rows.size() || r < 0 || r >= m_roles.size())
        return {};
    return m_rows[index.row()].value(QString::fromLatin1(m_roles[r]));
}

QHash<int, QByteArray> RowModel::roleNames() const
{
    QHash<int, QByteArray> h;
    for (int i = 0; i < m_roles.size(); ++i)
        h[Qt::UserRole + i] = m_roles[i];
    return h;
}

void RowModel::setRows(const QList<QVariantMap> &rows)
{
    const QString key = QString::fromLatin1(m_roles.first());
    bool sameKeys = rows.size() == m_rows.size();
    for (int i = 0; sameKeys && i < rows.size(); ++i)
        sameKeys = rows[i].value(key) == m_rows[i].value(key);
    if (!sameKeys) {   // 행이 늘거나 줄거나 순서가 바뀌면 다시 만든다
        const bool countDiff = rows.size() != m_rows.size();
        beginResetModel();
        m_rows = rows;
        endResetModel();
        if (countDiff)
            emit countChanged();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        QList<int> changed;
        for (int r = 0; r < m_roles.size(); ++r) {
            const QString name = QString::fromLatin1(m_roles[r]);
            if (rows[i].value(name) != m_rows[i].value(name))
                changed.append(Qt::UserRole + r);
        }
        if (!changed.isEmpty()) {
            m_rows[i] = rows[i];
            emit dataChanged(index(i), index(i), changed);
        }
    }
}
