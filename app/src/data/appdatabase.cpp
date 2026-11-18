#include "appdatabase.h"
#include "clipboardrepository.h"
#include "diaryrepository.h"
#include "projectrepository.h"
#include "toolrepository.h"
#include "repositoryhelpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QUuid>

using RepositoryHelpers::setError;

namespace {
constexpr int CurrentSchemaVersion = 9;

// A lookup index is compatible with existing v9 databases, including histories with duplicates.
bool ensureClipboardFingerprintIndex(const QSqlDatabase &database, QString *errorMessage)
{
    QSqlQuery query(database);
    if (query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS clipboard_auto_fingerprint ON clipboard_items(fingerprint,position,id DESC) "
        "WHERE tab_id=1 AND folder_id IS NULL AND automatic=1"))) return true;
    setError(errorMessage, query);
    return false;
}
} // namespace

AppDatabase::~AppDatabase()
{
    if (!connectionName_.isEmpty()) {
        const QString name = connectionName_;
        QSqlDatabase::removeDatabase(name);
    }
}

bool AppDatabase::open(QString *errorMessage)
{
    if (isOpen()) {
        return true;
    }

    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    if (applicationDirectory.isEmpty()) {
        return fail(errorMessage, QStringLiteral("无法确定 Orchestrate 程序目录。"));
    }

    const QString portableDataDirectory = QDir(applicationDirectory).filePath(QStringLiteral("data"));
    if (!QDir().mkpath(portableDataDirectory)) {
        return fail(errorMessage, QStringLiteral("无法在 Orchestrate 目录下创建数据目录：%1").arg(portableDataDirectory));
    }

    databasePath_ = QDir(portableDataDirectory).filePath(QStringLiteral("orchestrate.sqlite3"));
    const bool existingDatabase = QFileInfo(databasePath_).size() > 0;

    // 早期版本曾使用 Qt 的 AppDataLocation。第一次切换到便携目录时，
    // 尝试把已有数据库迁移过来，避免丢失已经录入的内容。
    if (!QFileInfo::exists(databasePath_)) {
        const QString legacyDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString legacyPath = QDir(legacyDirectory).filePath(QStringLiteral("orchestrate.sqlite3"));
        if (!QFileInfo::exists(legacyPath) && !QCoreApplication::organizationName().isEmpty()) {
            // Earlier application names used sibling folders under the same organization.
            // Discover the database by filename so branding does not determine data recovery.
            const QDir organizationDirectory = QFileInfo(legacyDirectory).dir();
            QStringList candidates;
            const auto folders = organizationDirectory.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            for (const auto &folder : folders) {
                const QString candidate = QDir(folder.absoluteFilePath()).filePath(QStringLiteral("orchestrate.sqlite3"));
                if (QFileInfo(candidate).isFile() && !QFileInfo(candidate).isSymLink()) {
                    candidates.append(candidate);
                }
            }
            if (candidates.size() > 1) {
                return fail(errorMessage, QStringLiteral("找到多个旧版数据库，请将需要使用的 orchestrate.sqlite3 复制到 %1 后重启。")
                                              .arg(portableDataDirectory));
            }
            if (candidates.size() == 1) legacyPath = candidates.first();
        }
        if (QFileInfo::exists(legacyPath) && !QFile::copy(legacyPath, databasePath_)) {
            return fail(errorMessage, QStringLiteral("无法把旧数据库迁移到便携目录：%1").arg(databasePath_));
        }
    }
    connectionName_ = QStringLiteral("orchestrate-%1").arg(QUuid::createUuid().toString(QUuid::Id128));

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database.setDatabaseName(databasePath_);
    if (!database.open()) {
        return fail(errorMessage, database.lastError().text());
    }

    QSqlQuery pragma(database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(errorMessage, pragma);
        database.close();
        return false;
    }

    QSqlQuery schemaVersion(database);
    if (!schemaVersion.exec(QStringLiteral("PRAGMA user_version")) || !schemaVersion.next()) {
        setError(errorMessage, schemaVersion);
        database.close();
        return false;
    }
    const int version = schemaVersion.value(0).toInt();
    schemaVersion.finish();
    if (version > CurrentSchemaVersion) {
        database.close();
        return fail(errorMessage, QStringLiteral("数据库来自更新版本的 Orchestrate。请使用新版程序，或同时恢复旧版程序与数据库备份。"));
    }
    if (version == CurrentSchemaVersion) {
        if (ensureClipboardFingerprintIndex(database, errorMessage)) return true;
        database.close();
        return false;
    }

    if (existingDatabase || QFileInfo(databasePath_).size() > 0) {
        const QString backupDirectory = QDir(portableDataDirectory).filePath(QStringLiteral("backups"));
        if (!QDir().mkpath(backupDirectory)) {
            database.close();
            return fail(errorMessage, QStringLiteral("无法创建数据库迁移备份目录。"));
        }
        const QString backupPath = QDir(backupDirectory).filePath(QStringLiteral("schema-%1-%2-%3.sqlite3")
            .arg(version).arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")), QUuid::createUuid().toString(QUuid::Id128)));
        QSqlQuery backup(database);
        backup.prepare(QStringLiteral("VACUUM INTO ?"));
        backup.addBindValue(backupPath);
        if (!backup.exec()) {
            setError(errorMessage, backup);
            database.close();
            return false;
        }
    }
    if (!database.transaction()) {
        database.close();
        return fail(errorMessage, QStringLiteral("无法开始数据库迁移事务。"));
    }
    QSqlQuery setVersion(database);
    // Version 1 adds WSL; version 2 adds explicit SSH usernames.
    bool migrated = true;
    if (version == 0) {
        migrated = initializeSchema(errorMessage);
    } else if (version < 3) {
        QStringList columns {QStringLiteral("ssh_user")};
        if (version == 1) columns << QStringLiteral("wsl_distribution") << QStringLiteral("wsl_user");
        for (const auto &column : columns) {
            QSqlQuery migration(database);
            if (!migration.exec(QStringLiteral("ALTER TABLE automation_tools ADD COLUMN %1 TEXT NOT NULL DEFAULT ''").arg(column))) {
                setError(errorMessage, migration);
                migrated = false;
                break;
            }
        }
    }
    if (migrated && version < 4) migrated = initializeCollaborationSchema(errorMessage);
    if (migrated) {
        QSqlQuery columns(database);
        bool hasSummary = false;
        migrated = columns.exec(QStringLiteral("PRAGMA table_info(project_work_records)"));
        if (!migrated) setError(errorMessage, columns);
        while (columns.next()) hasSummary |= columns.value(1).toString() == QStringLiteral("summary");
        columns.finish();
        if (migrated && !hasSummary) {
            QSqlQuery summary(database);
            migrated = summary.exec(QStringLiteral("ALTER TABLE project_work_records ADD COLUMN summary TEXT NOT NULL DEFAULT ''"));
            if (!migrated) setError(errorMessage, summary);
        }
    }
    if (migrated) {
        QSqlQuery columns(database);
        bool hasStarred = false;
        migrated = columns.exec(QStringLiteral("PRAGMA table_info(project_record_folders)"));
        if (!migrated) setError(errorMessage, columns);
        while (columns.next()) hasStarred |= columns.value(1).toString() == QStringLiteral("starred");
        columns.finish();
        if (migrated && !hasStarred) {
            QSqlQuery starred(database);
            migrated = starred.exec(QStringLiteral(
                "ALTER TABLE project_record_folders ADD COLUMN starred INTEGER NOT NULL DEFAULT 0 CHECK(starred IN (0, 1))"));
            if (!migrated) setError(errorMessage, starred);
        }
    }
    if (migrated && version < 7) migrated = initializeClipboardSchema(errorMessage);
    if (migrated && version < 8) {
        QSqlQuery columns(database);
        QSet<QString> projectColumns;
        migrated = columns.exec(QStringLiteral("PRAGMA table_info(projects)"));
        while (columns.next()) projectColumns.insert(columns.value(1).toString());
        columns.finish();
        QStringList statements;
        if (!projectColumns.contains(QStringLiteral("target_type"))) statements << QStringLiteral("ALTER TABLE projects ADD COLUMN target_type TEXT NOT NULL DEFAULT 'local' CHECK(target_type IN ('local','wsl','ssh'))");
        if (!projectColumns.contains(QStringLiteral("target_name"))) statements << QStringLiteral("ALTER TABLE projects ADD COLUMN target_name TEXT NOT NULL DEFAULT ''");
        if (!projectColumns.contains(QStringLiteral("target_user"))) statements << QStringLiteral("ALTER TABLE projects ADD COLUMN target_user TEXT NOT NULL DEFAULT ''");
        statements << QStringLiteral("DELETE FROM app_settings WHERE key = 'text_zoom.outline' OR key GLOB 'project.*.outline_expanded'");
        if (projectColumns.contains(QStringLiteral("outline"))) statements << QStringLiteral("ALTER TABLE projects DROP COLUMN outline");
        for (const auto &statement : statements) {
            QSqlQuery migration(database);
            if (!migrated || !migration.exec(statement)) {
                setError(errorMessage, migration);
                migrated = false;
                break;
            }
        }
    }
    if (migrated && version < 9) {
        const QStringList statements {
            QStringLiteral("ALTER TABLE automation_tools ADD COLUMN customized INTEGER NOT NULL DEFAULT 0 CHECK(customized IN (0,1))"),
            QStringLiteral("ALTER TABLE automation_tools ADD COLUMN environment TEXT NOT NULL DEFAULT '{}'"),
            QStringLiteral("ALTER TABLE tool_commands ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1))"),
            QStringLiteral("ALTER TABLE tool_commands ADD COLUMN position INTEGER NOT NULL DEFAULT 0")
        };
        for (const auto &statement : statements) {
            QSqlQuery migration(database);
            if (!migration.exec(statement)) { setError(errorMessage, migration); migrated = false; break; }
        }
    }
    if (migrated) migrated = ensureClipboardFingerprintIndex(database, errorMessage);
    if (!migrated || !setVersion.exec(QStringLiteral("PRAGMA user_version = %1").arg(CurrentSchemaVersion))) {
        if (errorMessage && errorMessage->isEmpty()) setError(errorMessage, setVersion);
        database.rollback();
        database.close();
        return false;
    }
    if (!database.commit()) {
        const QString message = database.lastError().text();
        database.rollback();
        database.close();
        return fail(errorMessage, message);
    }
    return true;
}

bool AppDatabase::initializeCollaborationSchema(QString *errorMessage)
{
    const QStringList statements {
        QStringLiteral("ALTER TABLE projects ADD COLUMN completed INTEGER NOT NULL DEFAULT 0 CHECK(completed IN (0, 1))"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS project_record_folders (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
                name TEXT NOT NULL CHECK(length(trim(name)) > 0),
                UNIQUE(project_id, name)
            )
        )SQL"),
        QStringLiteral("ALTER TABLE project_work_records ADD COLUMN folder_id INTEGER REFERENCES project_record_folders(id) ON DELETE SET NULL"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_records_folder ON project_work_records(folder_id)"),
        // Enforce project ownership even if another caller writes directly to SQLite.
        QStringLiteral(R"SQL(
            CREATE TRIGGER IF NOT EXISTS record_folder_insert BEFORE INSERT ON project_work_records
            WHEN NEW.folder_id IS NOT NULL AND NOT EXISTS (
                SELECT 1 FROM project_record_folders WHERE id = NEW.folder_id AND project_id = NEW.project_id)
            BEGIN SELECT RAISE(ABORT, '文件夹不存在或不属于当前项目。'); END
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TRIGGER IF NOT EXISTS record_folder_update BEFORE UPDATE OF folder_id, project_id ON project_work_records
            WHEN NEW.folder_id IS NOT NULL AND NOT EXISTS (
                SELECT 1 FROM project_record_folders WHERE id = NEW.folder_id AND project_id = NEW.project_id)
            BEGIN SELECT RAISE(ABORT, '文件夹不存在或不属于当前项目。'); END
        )SQL")
    };
    for (const auto &statement : statements) {
        QSqlQuery query(QSqlDatabase::database(connectionName_));
        // Unversioned development databases may already contain individual columns.
        if (!query.exec(statement)
            && !(statement.startsWith(QStringLiteral("ALTER TABLE"))
                 && query.lastError().text().contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive))) {
            setError(errorMessage, query);
            return false;
        }
    }
    return true;
}

bool AppDatabase::isOpen() const
{
    if (connectionName_.isEmpty()) {
        return false;
    }
    const QSqlDatabase database = QSqlDatabase::database(connectionName_, false);
    return database.isValid() && database.isOpen();
}

QString AppDatabase::databasePath() const
{
    return databasePath_;
}

bool AppDatabase::initializeSchema(QString *errorMessage)
{
    const QSqlDatabase database = QSqlDatabase::database(connectionName_);
    const QStringList statements {
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS diary_entries (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                entry_date TEXT NOT NULL,
                content TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS idx_diary_entries_date
            ON diary_entries(entry_date)
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS reminders (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                content TEXT NOT NULL DEFAULT '',
                remind_at TEXT NOT NULL,
                repeat_mode TEXT NOT NULL DEFAULT 'none',
                project_id INTEGER,
                completed INTEGER NOT NULL DEFAULT 0,
                last_notified_at TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE SET NULL
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS idx_reminders_remind_at
            ON reminders(remind_at, completed)
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS projects (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                directory TEXT NOT NULL DEFAULT '',
                outline TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS project_work_records (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                project_id INTEGER NOT NULL,
                content TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS idx_work_records_project
            ON project_work_records(project_id, created_at)
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS tool_groups (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                sort_order INTEGER NOT NULL DEFAULT 0
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS automation_tools (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                external_id TEXT NOT NULL DEFAULT '',
                group_id INTEGER,
                name TEXT NOT NULL,
                description TEXT NOT NULL DEFAULT '',
                target_type TEXT NOT NULL,
                registration_path TEXT NOT NULL,
                state_path TEXT NOT NULL,
                ssh_host TEXT NOT NULL DEFAULT '',
                working_directory TEXT NOT NULL DEFAULT '',
                refresh_seconds INTEGER NOT NULL DEFAULT 0,
                refresh_enabled INTEGER NOT NULL DEFAULT 0,
                refresh_mode TEXT NOT NULL DEFAULT 'interval',
                refresh_interval_seconds INTEGER NOT NULL DEFAULT 300,
                daily_refresh_time TEXT NOT NULL DEFAULT '08:00',
                builtin INTEGER NOT NULL DEFAULT 0,
                FOREIGN KEY(group_id) REFERENCES tool_groups(id) ON DELETE SET NULL
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS tool_commands (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                tool_id INTEGER NOT NULL,
                name TEXT NOT NULL,
                description TEXT NOT NULL DEFAULT '',
                executable TEXT NOT NULL,
                arguments TEXT NOT NULL DEFAULT '',
                parameters TEXT NOT NULL DEFAULT '',
                interactive INTEGER NOT NULL DEFAULT 0,
                high_risk INTEGER NOT NULL DEFAULT 0,
                FOREIGN KEY(tool_id) REFERENCES automation_tools(id) ON DELETE CASCADE
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS app_settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL DEFAULT ''
            )
        )SQL")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(database);
        if (!query.exec(statement)) {
            setError(errorMessage, query);
            return false;
        }
    }

    // Existing development databases may predate these fields. Keep them usable.
    const QStringList migrations {
        QStringLiteral("ALTER TABLE diary_entries ADD COLUMN title TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE project_work_records ADD COLUMN title TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN external_id TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN ssh_host TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN ssh_user TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN wsl_distribution TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN wsl_user TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN working_directory TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN refresh_enabled INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN refresh_mode TEXT NOT NULL DEFAULT 'interval'"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN refresh_interval_seconds INTEGER NOT NULL DEFAULT 300"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN daily_refresh_time TEXT NOT NULL DEFAULT '08:00'"),
        QStringLiteral("ALTER TABLE automation_tools ADD COLUMN builtin INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE tool_commands ADD COLUMN description TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE tool_commands ADD COLUMN parameters TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE tool_commands ADD COLUMN arguments_json TEXT NOT NULL DEFAULT ''")
    };
    for (const QString &statement : migrations) {
        QSqlQuery migration(database);
        if (!migration.exec(statement)
            && !migration.lastError().text().contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive)) {
            setError(errorMessage, migration);
            return false;
        }
    }

    return true;
}

bool AppDatabase::fail(QString *errorMessage, const QString &message) const
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

bool AppDatabase::initializeClipboardSchema(QString *errorMessage)
{
    const QStringList statements {
        QStringLiteral("CREATE TABLE IF NOT EXISTS clipboard_tabs (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE CHECK(length(trim(name))>0), is_history INTEGER NOT NULL DEFAULT 0 CHECK(is_history IN (0,1)), position INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS clipboard_one_history ON clipboard_tabs(is_history) WHERE is_history=1"),
        QStringLiteral("INSERT OR IGNORE INTO clipboard_tabs(id,name,is_history,position) VALUES(1,'历史',1,0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS clipboard_folders (id INTEGER PRIMARY KEY AUTOINCREMENT, tab_id INTEGER NOT NULL REFERENCES clipboard_tabs(id) ON DELETE CASCADE, name TEXT NOT NULL CHECK(length(trim(name))>0), position INTEGER NOT NULL DEFAULT 0, UNIQUE(tab_id,name))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS clipboard_items (id INTEGER PRIMARY KEY AUTOINCREMENT, tab_id INTEGER NOT NULL REFERENCES clipboard_tabs(id) ON DELETE CASCADE, folder_id INTEGER REFERENCES clipboard_folders(id) ON DELETE SET NULL, title TEXT NOT NULL DEFAULT '', text TEXT NOT NULL DEFAULT '', kind TEXT NOT NULL, payload BLOB NOT NULL, fingerprint BLOB NOT NULL, automatic INTEGER NOT NULL DEFAULT 0 CHECK(automatic IN (0,1)), position INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS clipboard_items_location ON clipboard_items(tab_id,folder_id,position,id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS clipboard_items_history ON clipboard_items(tab_id,automatic,created_at,id)"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS clipboard_protect_history BEFORE DELETE ON clipboard_tabs WHEN OLD.is_history=1 BEGIN SELECT RAISE(ABORT,'历史标签页不能删除。'); END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS clipboard_folder_insert BEFORE INSERT ON clipboard_items WHEN NEW.folder_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM clipboard_folders WHERE id=NEW.folder_id AND tab_id=NEW.tab_id) BEGIN SELECT RAISE(ABORT,'文件夹不属于目标标签页。'); END"),
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS clipboard_folder_update BEFORE UPDATE OF tab_id,folder_id ON clipboard_items WHEN NEW.folder_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM clipboard_folders WHERE id=NEW.folder_id AND tab_id=NEW.tab_id) BEGIN SELECT RAISE(ABORT,'文件夹不属于目标标签页。'); END")
    };
    for (const auto &sql : statements) {
        QSqlQuery query(QSqlDatabase::database(connectionName_));
        if (!query.exec(sql)) { setError(errorMessage, query); return false; }
    }
    return true;
}

QList<AppDatabase::DiaryEntry> AppDatabase::diaryEntries(const QDate &date, QString *errorMessage) const
{
    return DiaryRepository(connectionName_).diaryEntries(date, errorMessage);
}

bool AppDatabase::addDiaryEntry(const QDate &date, const QString &content, QString *errorMessage)
{
    return DiaryRepository(connectionName_).addDiaryEntry(date, content, errorMessage);
}

QList<AppDatabase::DiaryEntry> AppDatabase::allDiaryEntries(QString *errorMessage) const
{
    return DiaryRepository(connectionName_).allDiaryEntries(errorMessage);
}

AppDatabase::DiaryEntry AppDatabase::diaryEntry(int id, QString *errorMessage) const
{
    return DiaryRepository(connectionName_).diaryEntry(id, errorMessage);
}

QList<QDate> AppDatabase::diaryDates(const QDate &from, const QDate &to, QString *errorMessage) const
{
    return DiaryRepository(connectionName_).diaryDates(from, to, errorMessage);
}

bool AppDatabase::addDiaryEntry(const QDate &date, const QString &title, const QString &content, QString *errorMessage, int *newId)
{
    return DiaryRepository(connectionName_).addDiaryEntry(date, title, content, errorMessage, newId);
}

bool AppDatabase::updateDiaryEntry(int id, const QString &content, QString *errorMessage)
{
    return DiaryRepository(connectionName_).updateDiaryEntry(id, content, errorMessage);
}

bool AppDatabase::updateDiaryEntry(int id, const QString &title, const QString &content, QString *errorMessage)
{
    return DiaryRepository(connectionName_).updateDiaryEntry(id, title, content, errorMessage);
}

bool AppDatabase::deleteDiaryEntry(int id, QString *errorMessage)
{
    return DiaryRepository(connectionName_).deleteDiaryEntry(id, errorMessage);
}

QList<AppDatabase::Reminder> AppDatabase::reminders(const QDate &date, QString *errorMessage) const
{
    return DiaryRepository(connectionName_).reminders(date, errorMessage);
}

QList<AppDatabase::Reminder> AppDatabase::remindersInMonth(int year,
                                                           int month,
                                                           QString *errorMessage) const
{
    return DiaryRepository(connectionName_).remindersInMonth(year, month, errorMessage);
}

QList<AppDatabase::Reminder> AppDatabase::dueReminders(const QDateTime &now,
                                                        QString *errorMessage) const
{
    return DiaryRepository(connectionName_).dueReminders(now, errorMessage);
}

bool AppDatabase::addReminder(const Reminder &reminder,
                              int *reminderId,
                              QString *errorMessage)
{
    return DiaryRepository(connectionName_).addReminder(reminder, reminderId, errorMessage);
}

bool AppDatabase::updateReminder(const Reminder &reminder, QString *errorMessage)
{
    return DiaryRepository(connectionName_).updateReminder(reminder, errorMessage);
}

bool AppDatabase::deleteReminder(int id, QString *errorMessage)
{
    return DiaryRepository(connectionName_).deleteReminder(id, errorMessage);
}

bool AppDatabase::completeReminder(int id, const QDateTime &now, QString *errorMessage)
{
    return DiaryRepository(connectionName_).completeReminder(id, now, errorMessage);
}

bool AppDatabase::processDueReminder(int id, const QDateTime &now, QString *errorMessage)
{
    return DiaryRepository(connectionName_).processDueReminder(id, now, errorMessage);
}

QList<AppDatabase::Project> AppDatabase::projects(QString *errorMessage) const
{
    return ProjectRepository(connectionName_).projects(errorMessage);
}

bool AppDatabase::addProject(const QString &name,
                             const QString &directory,
                             int *projectId,
                             QString *errorMessage)
{
    return ProjectRepository(connectionName_).addProject(name, directory, projectId, errorMessage);
}

bool AppDatabase::addProject(const Project &project, int *projectId, QString *errorMessage)
{
    return ProjectRepository(connectionName_).addProject(project, projectId, errorMessage);
}

bool AppDatabase::updateProject(int id,
                                const QString &name,
                                const QString &directory,
                                QString *errorMessage)
{
    return ProjectRepository(connectionName_).updateProject(id, name, directory, errorMessage);
}

bool AppDatabase::updateProject(const Project &project, QString *errorMessage)
{
    return ProjectRepository(connectionName_).updateProject(project, errorMessage);
}

bool AppDatabase::deleteProject(int id, QString *errorMessage)
{
    return ProjectRepository(connectionName_).deleteProject(id, errorMessage);
}

bool AppDatabase::setProjectCompleted(int id, bool completed, QString *errorMessage)
{
    return ProjectRepository(connectionName_).setProjectCompleted(id, completed, errorMessage);
}

QList<AppDatabase::RecordFolder> AppDatabase::recordFolders(int projectId, QString *errorMessage) const
{
    return ProjectRepository(connectionName_).recordFolders(projectId, errorMessage);
}

bool AppDatabase::addRecordFolder(int projectId, const QString &name, int *folderId, QString *errorMessage)
{
    return ProjectRepository(connectionName_).addRecordFolder(projectId, name, folderId, errorMessage);
}

bool AppDatabase::renameRecordFolder(int id, const QString &name, QString *errorMessage)
{
    return ProjectRepository(connectionName_).renameRecordFolder(id, name, errorMessage);
}

bool AppDatabase::setRecordFolderStarred(int id, bool starred, QString *errorMessage)
{
    return ProjectRepository(connectionName_).setRecordFolderStarred(id, starred, errorMessage);
}

bool AppDatabase::deleteRecordFolder(int id, QString *errorMessage)
{
    return ProjectRepository(connectionName_).deleteRecordFolder(id, errorMessage);
}

bool AppDatabase::setWorkRecordFolder(int id, int folderId, QString *errorMessage)
{
    return ProjectRepository(connectionName_).setWorkRecordFolder(id, folderId, errorMessage);
}

QList<AppDatabase::WorkRecord> AppDatabase::workRecords(int projectId, QString *errorMessage) const
{
    return ProjectRepository(connectionName_).workRecords(projectId, errorMessage);
}

bool AppDatabase::addWorkRecord(int projectId, const QString &content, QString *errorMessage)
{
    return ProjectRepository(connectionName_).addWorkRecord(projectId, content, errorMessage);
}

bool AppDatabase::addWorkRecord(int projectId, const QString &title, const QString &content, QString *errorMessage)
{
    return ProjectRepository(connectionName_).addWorkRecord(projectId, title, content, errorMessage);
}

bool AppDatabase::addWorkRecord(int projectId, const QString &title, const QString &content, int folderId, QString *errorMessage)
{
    return ProjectRepository(connectionName_).addWorkRecord(projectId, title, content, folderId, errorMessage);
}

bool AppDatabase::saveWorkRecord(WorkRecord *record, QString *errorMessage)
{
    return ProjectRepository(connectionName_).saveWorkRecord(record, errorMessage);
}

bool AppDatabase::updateWorkRecord(int id, const QString &content, QString *errorMessage)
{
    return ProjectRepository(connectionName_).updateWorkRecord(id, content, errorMessage);
}

bool AppDatabase::updateWorkRecord(int id, const QString &title, const QString &content, QString *errorMessage)
{
    return ProjectRepository(connectionName_).updateWorkRecord(id, title, content, errorMessage);
}

bool AppDatabase::deleteWorkRecord(int id, QString *errorMessage)
{
    return ProjectRepository(connectionName_).deleteWorkRecord(id, errorMessage);
}

bool AppDatabase::reorderProjects(const QList<int> &ids, QString *errorMessage)
{
    return ProjectRepository(connectionName_).reorderProjects(ids, errorMessage);
}

bool AppDatabase::reorderRecordFolders(int projectId, bool starred, const QList<int> &ids, QString *errorMessage)
{
    return ProjectRepository(connectionName_).reorderRecordFolders(projectId, starred, ids, errorMessage);
}

bool AppDatabase::reorderWorkRecords(int projectId, int folderId, const QList<int> &ids, QString *errorMessage)
{
    return ProjectRepository(connectionName_).reorderWorkRecords(projectId, folderId, ids, errorMessage);
}

QList<AppDatabase::ToolGroup> AppDatabase::toolGroups(QString *errorMessage, bool includeUngrouped) const
{
    return ToolRepository(connectionName_).toolGroups(errorMessage, includeUngrouped);
}

bool AppDatabase::addToolGroup(const QString &name, int *groupId, QString *errorMessage)
{
    return ToolRepository(connectionName_).addToolGroup(name, groupId, errorMessage);
}

bool AppDatabase::renameToolGroup(int id, const QString &name, QString *errorMessage)
{
    return ToolRepository(connectionName_).renameToolGroup(id, name, errorMessage);
}

bool AppDatabase::deleteToolGroup(int id, QString *errorMessage)
{
    return ToolRepository(connectionName_).deleteToolGroup(id, errorMessage);
}

QList<AppDatabase::AutomationTool> AppDatabase::automationTools(int groupId, QString *errorMessage) const
{
    return ToolRepository(connectionName_).automationTools(groupId, errorMessage);
}

bool AppDatabase::addAutomationTool(const AutomationTool &tool,
                                    int *toolId,
                                    QString *errorMessage)
{
    return ToolRepository(connectionName_).addAutomationTool(tool, toolId, errorMessage);
}

bool AppDatabase::updateAutomationTool(const AutomationTool &tool, QString *errorMessage)
{
    return ToolRepository(connectionName_).updateAutomationTool(tool, errorMessage);
}

bool AppDatabase::setToolGroup(int toolId, int groupId, QString *errorMessage)
{
    return ToolRepository(connectionName_).setToolGroup(toolId, groupId, errorMessage);
}

bool AppDatabase::deleteAutomationTool(int id, QString *errorMessage)
{
    return ToolRepository(connectionName_).deleteAutomationTool(id, errorMessage);
}

bool AppDatabase::updateToolRefreshSettings(int id,
                                               bool enabled,
                                               const QString &mode,
                                               int intervalSeconds,
                                               const QString &dailyTime,
                                               QString *errorMessage)
{
    return ToolRepository(connectionName_).updateToolRefreshSettings(id, enabled, mode, intervalSeconds, dailyTime, errorMessage);
}

QList<AppDatabase::ToolCommand> AppDatabase::toolCommands(int toolId, QString *errorMessage) const
{
    return ToolRepository(connectionName_).toolCommands(toolId, errorMessage);
}

bool AppDatabase::addToolCommand(const ToolCommand &command, int *commandId, QString *errorMessage)
{
    return ToolRepository(connectionName_).addToolCommand(command, commandId, errorMessage);
}

bool AppDatabase::replaceToolCommands(int toolId, const QList<ToolCommand> &commands, QString *errorMessage)
{
    return ToolRepository(connectionName_).replaceToolCommands(toolId, commands, errorMessage);
}

bool AppDatabase::saveAutomationTool(const AutomationTool &tool, const QList<ToolCommand> &commands,
                                     int *toolId, QString *errorMessage)
{
    return ToolRepository(connectionName_).saveAutomationTool(tool, commands, toolId, errorMessage);
}

bool AppDatabase::reorderToolGroups(const QList<int> &ids, QString *errorMessage)
{
    return ToolRepository(connectionName_).reorderToolGroups(ids, errorMessage);
}

bool AppDatabase::reorderTools(int groupId, const QList<int> &ids, QString *errorMessage)
{
    return ToolRepository(connectionName_).reorderTools(groupId, ids, errorMessage);
}

QList<AppDatabase::ClipboardTab> AppDatabase::clipboardTabs(QString *errorMessage) const
{
    return ClipboardRepository(connectionName_).clipboardTabs(errorMessage);
}

QList<AppDatabase::ClipboardFolder> AppDatabase::clipboardFolders(int tabId, QString *errorMessage) const
{
    return ClipboardRepository(connectionName_).clipboardFolders(tabId, errorMessage);
}

QList<AppDatabase::ClipboardItem> AppDatabase::clipboardItems(int tabId, int folderId, const QString &search, QString *errorMessage) const
{
    return ClipboardRepository(connectionName_).clipboardItems(tabId, folderId, search, errorMessage);
}

AppDatabase::ClipboardItem AppDatabase::clipboardItem(int id, QString *errorMessage) const
{
    return ClipboardRepository(connectionName_).clipboardItem(id, errorMessage);
}

bool AppDatabase::addClipboardTab(const QString &name, int *id, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).addClipboardTab(name, id, errorMessage);
}

bool AppDatabase::renameClipboardTab(int id, const QString &name, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).renameClipboardTab(id, name, errorMessage);
}

bool AppDatabase::addClipboardFolder(int tabId, const QString &name, int *id, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).addClipboardFolder(tabId, name, id, errorMessage);
}

bool AppDatabase::renameClipboardFolder(int id, const QString &name, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).renameClipboardFolder(id, name, errorMessage);
}

bool AppDatabase::saveClipboardItem(ClipboardItem *item, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).saveClipboardItem(item, errorMessage);
}

bool AppDatabase::captureClipboardItem(ClipboardItem *item, int historyLimit, QString *errorMessage, const QList<int> &editingItemIds)
{
    return ClipboardRepository(connectionName_).captureClipboardItem(item, historyLimit, errorMessage, editingItemIds);
}

bool AppDatabase::moveClipboardItems(const QList<int> &ids, int tabId, int folderId, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).moveClipboardItems(ids, tabId, folderId, errorMessage);
}

bool AppDatabase::deleteClipboardItems(const QList<int> &ids, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).deleteClipboardItems(ids, errorMessage);
}

bool AppDatabase::deleteClipboardFolder(int id, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).deleteClipboardFolder(id, errorMessage);
}

bool AppDatabase::deleteClipboardTab(int id, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).deleteClipboardTab(id, errorMessage);
}

QString AppDatabase::setting(const QString &key, const QString &fallback) const
{
    return RepositoryBase(connectionName_).setting(key, fallback);
}

bool AppDatabase::setSetting(const QString &key, const QString &value, QString *errorMessage)
{
    return RepositoryBase(connectionName_).setSetting(key, value, errorMessage);
}

bool AppDatabase::reorderClipboardTabs(const QList<int> &ids, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).reorderClipboardTabs(ids, errorMessage);
}

bool AppDatabase::reorderClipboardFolders(int tabId, const QList<int> &ids, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).reorderClipboardFolders(tabId, ids, errorMessage);
}

bool AppDatabase::reorderClipboardItems(int tabId, int folderId, const QList<int> &ids, QString *errorMessage)
{
    return ClipboardRepository(connectionName_).reorderClipboardItems(tabId, folderId, ids, errorMessage);
}
