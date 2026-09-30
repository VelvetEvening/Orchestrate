#include "data/appdatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <stdexcept>

namespace {
void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

void sql(const QString &path, const QStringList &statements)
{
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("schema-fixture"));
        database.setDatabaseName(path);
        require(database.open(), database.lastError().text());
        for (const auto &statement : statements) {
            QSqlQuery query(database);
            require(query.exec(statement), query.lastError().text());
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("schema-fixture"));
}

int scalar(const QString &path, const QString &statement)
{
    int result = -1;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("schema-fixture"));
        database.setDatabaseName(path);
        require(database.open(), database.lastError().text());
        QSqlQuery query(database);
        require(query.exec(statement) && query.next(), query.lastError().text());
        result = query.value(0).toInt();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("schema-fixture"));
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (QFileInfo(app.applicationDirPath()).fileName() != QStringLiteral("database-schema-test")) return 2;
    const QString data = app.applicationDirPath() + QStringLiteral("/data");
    const QString path = data + QStringLiteral("/orchestrate.sqlite3");
    QTextStream output(stdout);
    try {
        require(!QFileInfo::exists(data) || QDir(data).removeRecursively(), QStringLiteral("Cannot reset isolated fixture"));
        require(QDir().mkpath(data), QStringLiteral("Cannot create fixture directory"));
        sql(path, {QStringLiteral("CREATE TABLE diary_entries (id INTEGER PRIMARY KEY, entry_date TEXT NOT NULL, content TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
                   QStringLiteral("INSERT INTO diary_entries VALUES(1,'2026-09-30','original diary','2026-09-30T01:00:00Z','2026-09-30T01:00:00Z')")});
        QString error;
        {
            AppDatabase database;
            require(database.open(&error), error);
            require(database.diaryEntry(1).content == QStringLiteral("original diary"), QStringLiteral("Diary was lost"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 1, QStringLiteral("Schema version was not recorded"));
        const QDir backups(data + QStringLiteral("/backups"));
        const auto files = backups.entryList({QStringLiteral("*.sqlite3")}, QDir::Files);
        require(files.size() == 1, QStringLiteral("Migration must create one snapshot"));
        const QString backup = backups.filePath(files.first());
        require(scalar(backup, QStringLiteral("PRAGMA user_version")) == 0
            && scalar(backup, QStringLiteral("SELECT count(*) FROM diary_entries WHERE content = 'original diary'")) == 1,
            QStringLiteral("Migration backup is not the original database"));
        {
            AppDatabase database;
            require(database.open(&error), error);
        }
        require(backups.entryList({QStringLiteral("*.sqlite3")}, QDir::Files).size() == 1, QStringLiteral("Unchanged schema was migrated again"));
        output << "PASS old schema snapshot, migration and version tracking" << Qt::endl;

        sql(path, {QStringLiteral("PRAGMA user_version = 2")});
        {
            AppDatabase database;
            error.clear();
            require(!database.open(&error) && !error.isEmpty(), QStringLiteral("Future schema must be rejected"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 2, QStringLiteral("Future schema was modified"));
        output << "PASS future schema rejection" << Qt::endl;

        require(QFile::remove(path), QStringLiteral("Cannot reset database"));
        sql(path, {QStringLiteral("CREATE TABLE reminders (id INTEGER PRIMARY KEY)"), QStringLiteral("PRAGMA user_version = 0")});
        {
            AppDatabase database;
            error.clear();
            require(!database.open(&error) && !error.isEmpty(), QStringLiteral("Invalid schema must fail"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 0
            && scalar(path, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'diary_entries'")) == 0,
            QStringLiteral("Failed migration was not rolled back"));
        output << "PASS migration failure rolls back schema and version" << Qt::endl;
        return 0;
    } catch (const std::exception &error) {
        output << "FAIL " << error.what() << Qt::endl;
        return 1;
    }
}
