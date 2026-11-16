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
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 9, QStringLiteral("Schema version was not recorded"));
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

        // Reconstruct the released v3 project schema with existing user content.
        sql(path, {QStringLiteral("ALTER TABLE projects ADD COLUMN outline TEXT NOT NULL DEFAULT ''"), QStringLiteral("INSERT INTO projects(id,name,directory,outline,created_at,updated_at) VALUES(1,'Existing project','D:/project','Keep outline','2026-10-01','2026-10-02')"),
                   QStringLiteral("INSERT INTO project_work_records(id,project_id,title,content,created_at,updated_at) VALUES(1,1,'Existing prompt','Keep prompt','2026-10-03','2026-10-04')"),
                   QStringLiteral("DROP TRIGGER record_folder_insert"),
                   QStringLiteral("DROP TRIGGER record_folder_update"),
                   QStringLiteral("DROP INDEX idx_work_records_folder"),
                   QStringLiteral("ALTER TABLE project_work_records DROP COLUMN folder_id"),
                   QStringLiteral("ALTER TABLE project_work_records DROP COLUMN summary"),
                   QStringLiteral("DROP TABLE project_record_folders"),
                   QStringLiteral("ALTER TABLE projects DROP COLUMN completed"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position"),
                   QStringLiteral("PRAGMA user_version = 3")});
        {
            AppDatabase database;
            require(database.open(&error), error);
            require(database.projects().size() == 1 && !database.projects().first().completed
                && database.projects().first().targetType == QStringLiteral("local"), QStringLiteral("v3 project was changed"));
            const auto records = database.workRecords(1);
            require(records.size() == 1 && records.first().folderId == 0
                && records.first().title == QStringLiteral("Existing prompt")
                && records.first().content == QStringLiteral("Keep prompt"), QStringLiteral("v3 record was changed"));
            require(database.recordFolders(1).isEmpty(), QStringLiteral("Migration invented folders"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 9, QStringLiteral("v3 was not upgraded"));
        const auto v3Backups = backups.entryList({QStringLiteral("schema-3-*.sqlite3")}, QDir::Files);
        require(v3Backups.size() == 1 && scalar(backups.filePath(v3Backups.first()), QStringLiteral("PRAGMA user_version")) == 3,
                QStringLiteral("v3 backup missing"));
        require(scalar(path, QStringLiteral("SELECT count(*) FROM project_work_records WHERE created_at = '2026-10-03' AND updated_at = '2026-10-04'")) == 1,
                QStringLiteral("Record dates changed during migration"));
        output << "PASS v3 collaboration migration preserves project, record and timestamps" << Qt::endl;

        sql(path, {QStringLiteral("INSERT INTO project_record_folders(id,project_id,name) VALUES(10,1,'Prompts')"),
                   QStringLiteral("UPDATE projects SET completed=1 WHERE id=1"),
                   QStringLiteral("UPDATE project_work_records SET folder_id=10 WHERE id=1"),
                   QStringLiteral("ALTER TABLE project_work_records DROP COLUMN summary"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position"),
                   QStringLiteral("PRAGMA user_version = 4")});
        {
            AppDatabase database;
            require(database.open(&error), error);
            auto record = database.workRecords(1).first();
            require(record.summary.isEmpty() && record.content == QStringLiteral("Keep prompt")
                && record.title == QStringLiteral("Existing prompt") && record.folderId == 10,
                QStringLiteral("v4 original record was modified"));
            require(database.projects().first().completed && database.recordFolders(1).size() == 1,
                QStringLiteral("Project status or folders lost"));
            require(scalar(path, QStringLiteral("SELECT count(*) FROM project_work_records WHERE created_at='2026-10-03' AND updated_at='2026-10-04'")) == 1,
                QStringLiteral("v4 migration changed timestamps"));
            record.summary = QStringLiteral("# Key points\nKeep manual summary");
            require(database.saveWorkRecord(&record, &error), error);
            require(database.updateWorkRecord(record.id, record.content, &error), error);
            require(database.updateWorkRecord(record.id, record.title, record.content, &error), error);
            require(database.workRecords(1).first().summary == record.summary, QStringLiteral("Legacy APIs erased summary"));
            auto created = record; created.id = 0;
            require(database.saveWorkRecord(&created, &error) && created.id > 1, error);
            require(database.deleteWorkRecord(created.id, &error), error);
            require(!database.saveWorkRecord(&created, &error), QStringLiteral("Deleted record silently recreated"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 9, QStringLiteral("v4 was not upgraded"));
        const auto v4Backups = backups.entryList({QStringLiteral("schema-4-*.sqlite3")}, QDir::Files);
        require(v4Backups.size() == 1 && scalar(backups.filePath(v4Backups.first()), QStringLiteral("PRAGMA user_version")) == 4,
            QStringLiteral("v4 migration has no original backup"));
        require(scalar(backups.filePath(v4Backups.first()), QStringLiteral("SELECT count(*) FROM pragma_table_info('project_work_records') WHERE name='summary'")) == 0,
            QStringLiteral("Backup was taken after migration"));
        output << "PASS v4 summary migration, original backup, field preservation and legacy APIs" << Qt::endl;

        sql(path, {QStringLiteral("ALTER TABLE project_record_folders DROP COLUMN starred"),
                   QStringLiteral("UPDATE project_work_records SET summary='Keep summary', updated_at='2026-10-04' WHERE id=1"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position"),
                   QStringLiteral("PRAGMA user_version = 5")});
        {
            AppDatabase database;
            error.clear();
            require(database.open(&error), error);
            const auto folders = database.recordFolders(1);
            const auto record = database.workRecords(1).first();
            require(folders.size() == 1 && folders.first().id == 10 && !folders.first().starred
                && folders.first().name == QStringLiteral("Prompts"), QStringLiteral("v5 folder migration changed classification"));
            require(record.folderId == 10 && record.summary == QStringLiteral("Keep summary")
                && record.title == QStringLiteral("Existing prompt") && record.content == QStringLiteral("Keep prompt")
                && database.projects().first().completed, QStringLiteral("v5 migration modified user content"));
            require(database.setRecordFolderStarred(10, true, &error), error);
            require(database.recordFolders(1).first().starred, QStringLiteral("Star was not saved"));
            require(!database.setRecordFolderStarred(-1, true, &error) && !error.isEmpty(),
                QStringLiteral("Missing folder accepted a star"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 9, QStringLiteral("v5 was not upgraded"));
        const auto v5Backups = backups.entryList({QStringLiteral("schema-5-*.sqlite3")}, QDir::Files);
        require(v5Backups.size() == 1, QStringLiteral("v5 migration requires an original backup"));
        const QString v5Backup = backups.filePath(v5Backups.first());
        require(scalar(v5Backup, QStringLiteral("PRAGMA user_version")) == 5
            && scalar(v5Backup, QStringLiteral("SELECT count(*) FROM pragma_table_info('project_record_folders') WHERE name='starred'")) == 0
            && scalar(v5Backup, QStringLiteral("SELECT count(*) FROM project_work_records WHERE folder_id=10 AND summary='Keep summary'")) == 1,
            QStringLiteral("v5 backup was taken after changing the schema or user content"));
        require(scalar(path, QStringLiteral("SELECT count(*) FROM project_work_records WHERE created_at='2026-10-03' AND updated_at='2026-10-04'")) == 1,
            QStringLiteral("Folder starring changed record timestamps"));
        {
            AppDatabase reopened;
            error.clear();
            require(reopened.open(&error) && reopened.recordFolders(1).first().starred, QStringLiteral("Star did not survive reopening"));
        }
        require(backups.entryList({QStringLiteral("schema-5-*.sqlite3")}, QDir::Files).size() == 1,
            QStringLiteral("Reopening starred folders repeated migration"));
        output << "PASS v5 star migration, original backup, data preservation and persistence" << Qt::endl;

        // Recreate a released v1 schema, with a real registered local tool.
        sql(path, {QStringLiteral("INSERT INTO automation_tools(external_id,name,target_type,registration_path,state_path,refresh_enabled) VALUES('legacy-tool','Legacy','windows-local','D:/tool.json','D:/state.json',1)"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN ssh_user"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN wsl_distribution"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN wsl_user"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position"),
                   QStringLiteral("PRAGMA user_version = 1")});
        {
            AppDatabase database;
            require(database.open(&error), error);
            const auto tools = database.automationTools();
            require(tools.size() == 1 && tools.first().externalId == QStringLiteral("legacy-tool")
                && tools.first().refreshEnabled && tools.first().wslDistribution.isEmpty()
                && tools.first().wslUser.isEmpty(), QStringLiteral("v1 tool/settings were not preserved"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 9, QStringLiteral("v1 was not upgraded"));
        const auto v1Backups = backups.entryList({QStringLiteral("schema-1-*.sqlite3")}, QDir::Files);
        require(v1Backups.size() == 1 && scalar(backups.filePath(v1Backups.first()), QStringLiteral("PRAGMA user_version")) == 1,
                QStringLiteral("v1 upgrade requires a recoverable v1 backup"));
        output << "PASS v1 WSL migration preserves registrations and creates backup" << Qt::endl;

        sql(path, {QStringLiteral("ALTER TABLE automation_tools DROP COLUMN ssh_user"), QStringLiteral("ALTER TABLE automation_tools DROP COLUMN customized"),
                   QStringLiteral("ALTER TABLE automation_tools DROP COLUMN environment"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN enabled"),
                   QStringLiteral("ALTER TABLE tool_commands DROP COLUMN position"),
                   QStringLiteral("PRAGMA user_version = 2")});
        {
            AppDatabase database;
            require(database.open(&error), error);
            require(database.automationTools().first().sshUser.isEmpty(), QStringLiteral("Legacy SSH user must remain unset"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 9, QStringLiteral("v2 was not upgraded"));
        require(backups.entryList({QStringLiteral("schema-2-*.sqlite3")}, QDir::Files).size() == 1, QStringLiteral("v2 backup missing"));
        output << "PASS v2 SSH username migration" << Qt::endl;
        sql(path, {QStringLiteral("PRAGMA user_version = 10")});
        {
            AppDatabase database;
            error.clear();
            require(!database.open(&error) && !error.isEmpty(), QStringLiteral("Future schema must be rejected"));
        }
        require(scalar(path, QStringLiteral("PRAGMA user_version")) == 10, QStringLiteral("Future schema was modified"));
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
