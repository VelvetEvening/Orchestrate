#pragma once

#include <QList>
#include <QString>
#include <QVariant>

// A repository is a view of AppDatabase's named connection. It never opens or
// owns a second SQLite connection, so transactions span every operation.
class RepositoryBase
{
public:
    explicit RepositoryBase(const QString &connectionName) : connectionName_(connectionName) {}
    bool isOpen() const;
    QString setting(const QString &key, const QString &fallback = QString()) const;
    bool setSetting(const QString &key, const QString &value, QString *errorMessage = nullptr);

protected:
    bool fail(QString *errorMessage, const QString &message) const;
    bool saveSiblingOrder(const QString &key, const QString &query, const QVariantList &bindings,
                          const QList<int> &ids, QString *errorMessage);
    QString connectionName_;
};
