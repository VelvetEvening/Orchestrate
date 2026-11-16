#include "repositorybase.h"
#include "repositoryhelpers.h"
#include <QSqlDatabase>
#include <QSet>

using namespace RepositoryHelpers;

bool RepositoryBase::isOpen() const
{
    if (connectionName_.isEmpty()) {
        return false;
    }
    const QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
    return database.isValid() && database.isOpen();
}

bool RepositoryBase::fail(QString *errorMessage, const QString &message) const
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

QString RepositoryBase::setting(const QString &key, const QString &fallback) const
{
    if (!isOpen()) {
        return fallback;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next()) {
        return fallback;
    }
    return query.value(0).toString();
}

bool RepositoryBase::setSetting(const QString &key, const QString &value, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO app_settings(key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(text(value));
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool RepositoryBase::saveSiblingOrder(const QString &key, const QString &sql, const QVariantList &bindings,
                                   const QList<int> &ids, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    auto database = QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage, database.lastError().text());
    QSqlQuery query(database);
    query.prepare(sql);
    for (const auto &value : bindings) query.addBindValue(value);
    if (!query.exec()) { setError(errorMessage, query); database.rollback(); return false; }
    QSet<int> expected;
    while (query.next()) expected.insert(query.value(0).toInt());
    query.finish();
    const QSet<int> actual(ids.cbegin(), ids.cend());
    if (actual.size() != ids.size() || actual != expected) {
        database.rollback();
        return fail(errorMessage, QStringLiteral("列表内容已变化，排序未保存。请重新打开此列表后重试。"));
    }
    QJsonArray values;
    for (int id : ids) values.append(id);
    if (!setSetting(key, QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact)), errorMessage)) {
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        const QString message = database.lastError().text();
        database.rollback();
        return fail(errorMessage, message);
    }
    return true;
}
