#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <limits>

namespace RepositoryHelpers {
inline QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

inline void setError(QString *errorMessage, const QSqlQuery &query)
{
    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
}

// Qt binds a null QString as SQL NULL; text columns here are NOT NULL, so bind '' instead.
inline QString text(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

// Keep the legacy order until a user sorts a scope. Unknown/new ids append in
// legacy order; deleted or moved-out ids are harmless and never resurrect rows.
template<class T, class Matches>
inline void applySiblingOrder(QList<T> &items, const QString &serialized, Matches matches)
{
    const auto saved = QJsonDocument::fromJson(serialized.toUtf8()).array();
    if (saved.isEmpty()) return;
    QHash<int, int> ranks;
    for (const auto &value : saved) {
        const int id = value.toInt(-1);
        if (id >= 0 && !ranks.contains(id)) ranks.insert(id, ranks.size());
    }
    QList<qsizetype> positions;
    QList<T> siblings;
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (matches(items[i])) { positions.append(i); siblings.append(items[i]); }
    }
    std::stable_sort(siblings.begin(), siblings.end(), [&ranks](const T &a, const T &b) {
        return ranks.value(a.id, std::numeric_limits<int>::max()) < ranks.value(b.id, std::numeric_limits<int>::max());
    });
    for (qsizetype i = 0; i < positions.size(); ++i) items[positions[i]] = siblings[i];
}
} // namespace
