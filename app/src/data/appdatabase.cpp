#include "appdatabase.h"

#include <QCoreApplication>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void setError(QString *errorMessage, const QSqlQuery &query)
{
    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
}

// Qt binds a null QString as SQL NULL; text columns here are NOT NULL, so bind '' instead.
QString text(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
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

    if (!initializeSchema(errorMessage)) {
        database.close();
        return false;
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

QList<AppDatabase::DiaryEntry> AppDatabase::diaryEntries(const QDate &date, QString *errorMessage) const
{
    QList<DiaryEntry> entries;
    if (!isOpen()) {
        return entries;
    }

    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, entry_date, content, created_at, updated_at, title FROM diary_entries ")
        + (date.isValid() ? QStringLiteral("WHERE entry_date = ? ") : QString())
        + QStringLiteral("ORDER BY entry_date DESC, created_at DESC, id DESC"));
    if (date.isValid()) query.addBindValue(date.toString(Qt::ISODate));
    if (!query.exec()) {
        setError(errorMessage, query);
        return entries;
    }

    while (query.next()) {
        DiaryEntry entry;
        entry.id = query.value(0).toInt();
        entry.date = QDate::fromString(query.value(1).toString(), Qt::ISODate);
        entry.content = query.value(2).toString();
        entry.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        entry.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        entry.title = query.value(5).toString();
        entries.append(entry);
    }
    return entries;
}

bool AppDatabase::addDiaryEntry(const QDate &date, const QString &content, QString *errorMessage)
{
    return addDiaryEntry(date, QStringLiteral(""), content, errorMessage);
}

QList<AppDatabase::DiaryEntry> AppDatabase::allDiaryEntries(QString *errorMessage) const
{
    return diaryEntries(QDate(), errorMessage);
}

AppDatabase::DiaryEntry AppDatabase::diaryEntry(int id, QString *errorMessage) const
{
    DiaryEntry entry;
    if (!isOpen()) return entry;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT id, entry_date, content, created_at, updated_at, title FROM diary_entries WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return entry; }
    if (query.next()) {
        entry.id = query.value(0).toInt();
        entry.date = QDate::fromString(query.value(1).toString(), Qt::ISODate);
        entry.content = query.value(2).toString();
        entry.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        entry.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        entry.title = query.value(5).toString();
    }
    return entry;
}

QList<QDate> AppDatabase::diaryDates(const QDate &from, const QDate &to, QString *errorMessage) const
{
    QList<QDate> dates;
    if (!isOpen()) return dates;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT DISTINCT entry_date FROM diary_entries WHERE entry_date BETWEEN ? AND ?"));
    query.addBindValue(from.toString(Qt::ISODate));
    query.addBindValue(to.toString(Qt::ISODate));
    if (!query.exec()) { setError(errorMessage, query); return dates; }
    while (query.next()) dates.append(QDate::fromString(query.value(0).toString(), Qt::ISODate));
    return dates;
}

bool AppDatabase::addDiaryEntry(const QDate &date, const QString &title, const QString &content, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO diary_entries(entry_date, content, created_at, updated_at, title) VALUES (?, ?, ?, ?, ?)"));
    const QString timestamp = nowIso();
    query.addBindValue(date.toString(Qt::ISODate));
    query.addBindValue(content);
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    query.addBindValue(title.isNull() ? QStringLiteral("") : title.trimmed());
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::updateDiaryEntry(int id, const QString &content, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE diary_entries SET content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(content);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::updateDiaryEntry(int id, const QString &title, const QString &content, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE diary_entries SET title = ?, content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(title.isNull() ? QStringLiteral("") : title.trimmed());
    query.addBindValue(content);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return true;
}

bool AppDatabase::deleteDiaryEntry(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM diary_entries WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

namespace {

AppDatabase::Reminder reminderFromQuery(const QSqlQuery &query)
{
    AppDatabase::Reminder reminder;
    reminder.id = query.value(0).toInt();
    reminder.title = query.value(1).toString();
    reminder.content = query.value(2).toString();
    reminder.remindAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs).toLocalTime();
    reminder.repeatMode = query.value(4).toString();
    if (reminder.repeatMode.isEmpty()) {
        reminder.repeatMode = QStringLiteral("none");
    }
    reminder.projectId = query.value(5).toInt();
    reminder.completed = query.value(6).toInt() != 0;
    const QString lastNotified = query.value(7).toString();
    reminder.lastNotifiedAt = lastNotified.isEmpty()
        ? QDateTime()
        : QDateTime::fromString(lastNotified, Qt::ISODateWithMs).toLocalTime();
    reminder.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs);
    reminder.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    return reminder;
}

QDateTime nextReminderOccurrence(QDateTime occurrence,
                                 const QString &repeatMode,
                                 const QDateTime &after)
{
    if (repeatMode == QStringLiteral("daily")) {
        do {
            occurrence = occurrence.addDays(1);
        } while (occurrence <= after);
    } else if (repeatMode == QStringLiteral("weekly")) {
        do {
            occurrence = occurrence.addDays(7);
        } while (occurrence <= after);
    } else if (repeatMode == QStringLiteral("monthly")) {
        do {
            occurrence = occurrence.addMonths(1);
        } while (occurrence <= after);
    }
    return occurrence;
}

} // namespace

QList<AppDatabase::Reminder> AppDatabase::reminders(const QDate &date, QString *errorMessage) const
{
    QList<Reminder> result;
    const QList<Reminder> candidates = remindersInMonth(date.year(), date.month(), errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return result;
    }
    for (const Reminder &reminder : candidates) {
        if (reminder.remindAt.date() == date) {
            result.append(reminder);
        }
    }
    std::sort(result.begin(), result.end(), [](const Reminder &left, const Reminder &right) {
        return left.remindAt < right.remindAt;
    });
    return result;
}

QList<AppDatabase::Reminder> AppDatabase::remindersInMonth(int year,
                                                           int month,
                                                           QString *errorMessage) const
{
    QList<Reminder> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, title, content, remind_at, repeat_mode, COALESCE(project_id, 0), "
        "completed, last_notified_at, created_at, updated_at FROM reminders ORDER BY remind_at, id"));
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    const QDate firstDay(year, month, 1);
    const QDate lastDay = firstDay.isValid() ? firstDay.addMonths(1).addDays(-1) : QDate();
    while (query.next()) {
        const Reminder reminder = reminderFromQuery(query);
        if (reminder.remindAt.isValid() && reminder.remindAt.date() >= firstDay
            && reminder.remindAt.date() <= lastDay) {
            result.append(reminder);
        }
    }
    return result;
}

QList<AppDatabase::Reminder> AppDatabase::dueReminders(const QDateTime &now,
                                                        QString *errorMessage) const
{
    QList<Reminder> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, title, content, remind_at, repeat_mode, COALESCE(project_id, 0), "
        "completed, last_notified_at, created_at, updated_at "
        "FROM reminders WHERE completed = 0 ORDER BY remind_at, id"));
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        const Reminder reminder = reminderFromQuery(query);
        if (reminder.remindAt.isValid() && reminder.remindAt <= now
            && (reminder.repeatMode != QStringLiteral("none")
                || !reminder.lastNotifiedAt.isValid()
                || reminder.lastNotifiedAt < reminder.remindAt)) {
            result.append(reminder);
        }
    }
    return result;
}

bool AppDatabase::addReminder(const Reminder &reminder,
                              int *reminderId,
                              QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    if (!reminder.remindAt.isValid() || reminder.title.trimmed().isEmpty()) {
        return fail(errorMessage, QStringLiteral("提醒必须包含标题和有效时间。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO reminders(title, content, remind_at, repeat_mode, project_id, completed, "
        "last_notified_at, created_at, updated_at) VALUES (?, ?, ?, ?, NULLIF(?, 0), ?, ?, ?, ?)"));
    const QString timestamp = nowIso();
    query.addBindValue(reminder.title.trimmed());
    query.addBindValue(reminder.content.isNull() ? QStringLiteral("") : reminder.content);
    query.addBindValue(reminder.remindAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(reminder.repeatMode.isEmpty() ? QStringLiteral("none") : reminder.repeatMode);
    query.addBindValue(reminder.projectId);
    query.addBindValue(reminder.completed ? 1 : 0);
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (reminderId != nullptr) {
        *reminderId = query.lastInsertId().toInt();
    }
    return true;
}

bool AppDatabase::updateReminder(const Reminder &reminder, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    if (reminder.id <= 0 || !reminder.remindAt.isValid() || reminder.title.trimmed().isEmpty()) {
        return fail(errorMessage, QStringLiteral("提醒必须包含标题和有效时间。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE reminders SET title = ?, content = ?, remind_at = ?, repeat_mode = ?, "
        "project_id = NULLIF(?, 0), completed = ?, last_notified_at = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(reminder.title.trimmed());
    query.addBindValue(reminder.content.isNull() ? QStringLiteral("") : reminder.content);
    query.addBindValue(reminder.remindAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(reminder.repeatMode.isEmpty() ? QStringLiteral("none") : reminder.repeatMode);
    query.addBindValue(reminder.projectId);
    query.addBindValue(reminder.completed ? 1 : 0);
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(nowIso());
    query.addBindValue(reminder.id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::deleteReminder(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM reminders WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::completeReminder(int id, const QDateTime &now, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery find(QSqlDatabase::database(connectionName_));
    find.prepare(QStringLiteral(
        "SELECT id, title, content, remind_at, repeat_mode, COALESCE(project_id, 0), "
        "completed, last_notified_at, created_at, updated_at FROM reminders WHERE id = ?"));
    find.addBindValue(id);
    if (!find.exec() || !find.next()) {
        setError(errorMessage, find);
        return false;
    }
    Reminder reminder = reminderFromQuery(find);
    if (reminder.repeatMode != QStringLiteral("none")) {
        reminder.remindAt = nextReminderOccurrence(reminder.remindAt, reminder.repeatMode, now);
        reminder.completed = false;
        reminder.lastNotifiedAt = QDateTime();
        return updateReminder(reminder, errorMessage);
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE reminders SET completed = 1, updated_at = ? WHERE id = ?"));
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::processDueReminder(int id, const QDateTime &now, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery find(QSqlDatabase::database(connectionName_));
    find.prepare(QStringLiteral(
        "SELECT id, title, content, remind_at, repeat_mode, COALESCE(project_id, 0), "
        "completed, last_notified_at, created_at, updated_at FROM reminders WHERE id = ?"));
    find.addBindValue(id);
    if (!find.exec() || !find.next()) {
        setError(errorMessage, find);
        return false;
    }
    Reminder reminder = reminderFromQuery(find);
    if (reminder.repeatMode != QStringLiteral("none")) {
        reminder.remindAt = nextReminderOccurrence(reminder.remindAt, reminder.repeatMode, now);
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE reminders SET remind_at = ?, last_notified_at = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(reminder.remindAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

QList<AppDatabase::Project> AppDatabase::projects(QString *errorMessage) const
{
    QList<Project> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (!query.exec(QStringLiteral(
            "SELECT id, name, directory, outline, created_at, updated_at "
            "FROM projects ORDER BY updated_at DESC, id DESC"))) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        Project project;
        project.id = query.value(0).toInt();
        project.name = query.value(1).toString();
        project.directory = query.value(2).toString();
        project.outline = query.value(3).toString();
        project.createdAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        project.updatedAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
        result.append(project);
    }
    return result;
}

bool AppDatabase::addProject(const QString &name,
                             const QString &directory,
                             const QString &outline,
                             int *projectId,
                             QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO projects(name, directory, outline, created_at, updated_at) VALUES (?, ?, ?, ?, ?)"));
    const QString timestamp = nowIso();
    query.addBindValue(name);
    query.addBindValue(text(directory));
    query.addBindValue(text(outline));
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (projectId != nullptr) {
        *projectId = query.lastInsertId().toInt();
    }
    return true;
}

bool AppDatabase::updateProject(int id,
                                const QString &name,
                                const QString &directory,
                                const QString &outline,
                                QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE projects SET name = ?, directory = ?, outline = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(name);
    query.addBindValue(text(directory));
    query.addBindValue(text(outline));
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::deleteProject(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM projects WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

QList<AppDatabase::WorkRecord> AppDatabase::workRecords(int projectId, QString *errorMessage) const
{
    QList<WorkRecord> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, project_id, content, created_at, updated_at, title "
        "FROM project_work_records WHERE project_id = ? ORDER BY created_at DESC, id DESC"));
    query.addBindValue(projectId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        WorkRecord record;
        record.id = query.value(0).toInt();
        record.projectId = query.value(1).toInt();
        record.title = query.value(5).toString();
        record.content = query.value(2).toString();
        record.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        record.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        result.append(record);
    }
    return result;
}

bool AppDatabase::addWorkRecord(int projectId, const QString &content, QString *errorMessage)
{
    return addWorkRecord(projectId, QStringLiteral(""), content, errorMessage);
}

bool AppDatabase::addWorkRecord(int projectId, const QString &title, const QString &content, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO project_work_records(project_id, content, created_at, updated_at, title) VALUES (?, ?, ?, ?, ?)"));
    const QString timestamp = nowIso();
    query.addBindValue(projectId);
    query.addBindValue(content);
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    query.addBindValue(title.isNull() ? QStringLiteral("") : title);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::updateWorkRecord(int id, const QString &content, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_work_records SET content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(content);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::updateWorkRecord(int id, const QString &title, const QString &content, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE project_work_records SET title = ?, content = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(title.isNull() ? QStringLiteral("") : title);
    query.addBindValue(content);
    query.addBindValue(nowIso());
    query.addBindValue(id);
    if (!query.exec()) { setError(errorMessage, query); return false; }
    return true;
}

bool AppDatabase::deleteWorkRecord(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM project_work_records WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

QList<AppDatabase::ToolGroup> AppDatabase::toolGroups(QString *errorMessage) const
{
    QList<ToolGroup> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (!query.exec(QStringLiteral("SELECT id, name, sort_order FROM tool_groups ORDER BY sort_order, name"))) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        ToolGroup group;
        group.id = query.value(0).toInt();
        group.name = query.value(1).toString();
        group.sortOrder = query.value(2).toInt();
        result.append(group);
    }
    return result;
}

bool AppDatabase::addToolGroup(const QString &name, int *groupId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("INSERT INTO tool_groups(name, sort_order) VALUES (?, COALESCE((SELECT MAX(sort_order) + 1 FROM tool_groups), 0))"));
    query.addBindValue(name);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (groupId != nullptr) {
        *groupId = query.lastInsertId().toInt();
    }
    return true;
}

bool AppDatabase::renameToolGroup(int id, const QString &name, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE tool_groups SET name = ? WHERE id = ?"));
    query.addBindValue(name);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::deleteToolGroup(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM tool_groups WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

QList<AppDatabase::AutomationTool> AppDatabase::automationTools(int groupId, QString *errorMessage) const
{
    QList<AutomationTool> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (groupId > 0) {
        query.prepare(QStringLiteral(
            "SELECT id, external_id, COALESCE(group_id, 0), name, description, target_type, registration_path, state_path, "
            "ssh_host, working_directory, refresh_enabled, refresh_mode, refresh_interval_seconds, daily_refresh_time, builtin "
            "FROM automation_tools WHERE group_id = ? ORDER BY name"));
        query.addBindValue(groupId);
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, external_id, COALESCE(group_id, 0), name, description, target_type, registration_path, state_path, "
            "ssh_host, working_directory, refresh_enabled, refresh_mode, refresh_interval_seconds, daily_refresh_time, builtin "
            "FROM automation_tools ORDER BY name"));
    }
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        AutomationTool tool;
        tool.id = query.value(0).toInt();
        tool.externalId = query.value(1).toString();
        tool.groupId = query.value(2).toInt();
        tool.name = query.value(3).toString();
        tool.description = query.value(4).toString();
        tool.targetType = query.value(5).toString();
        tool.registrationPath = query.value(6).toString();
        tool.statePath = query.value(7).toString();
        tool.sshHost = query.value(8).toString();
        tool.workingDirectory = query.value(9).toString();
        tool.refreshEnabled = query.value(10).toInt() != 0;
        tool.refreshMode = query.value(11).toString();
        if (tool.refreshMode.isEmpty()) {
            tool.refreshMode = QStringLiteral("interval");
        }
        tool.refreshIntervalSeconds = qMax(60, query.value(12).toInt());
        tool.dailyRefreshTime = query.value(13).toString();
        if (tool.dailyRefreshTime.isEmpty()) {
            tool.dailyRefreshTime = QStringLiteral("08:00");
        }
        tool.builtin = query.value(14).toInt() != 0;
        result.append(tool);
    }
    return result;
}

bool AppDatabase::addAutomationTool(const AutomationTool &tool,
                                    int *toolId,
                                    QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO automation_tools(external_id, group_id, name, description, target_type, registration_path, state_path, "
        "ssh_host, working_directory, refresh_enabled, refresh_mode, refresh_interval_seconds, daily_refresh_time, builtin) "
        "VALUES (?, NULLIF(?, 0), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(text(tool.externalId));
    query.addBindValue(tool.groupId);
    query.addBindValue(text(tool.name));
    query.addBindValue(text(tool.description));
    query.addBindValue(text(tool.targetType));
    query.addBindValue(text(tool.registrationPath));
    query.addBindValue(text(tool.statePath));
    query.addBindValue(text(tool.sshHost));
    query.addBindValue(text(tool.workingDirectory));
    query.addBindValue(tool.refreshEnabled ? 1 : 0);
    query.addBindValue(tool.refreshMode.isEmpty() ? QStringLiteral("interval") : tool.refreshMode);
    query.addBindValue(qMax(60, tool.refreshIntervalSeconds));
    query.addBindValue(tool.dailyRefreshTime.isEmpty() ? QStringLiteral("08:00") : tool.dailyRefreshTime);
    query.addBindValue(tool.builtin ? 1 : 0);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (toolId != nullptr) {
        *toolId = query.lastInsertId().toInt();
    }
    return true;
}

bool AppDatabase::updateAutomationTool(const AutomationTool &tool, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    // Built-in ownership is app-managed, not part of the manifest. Startup sync
    // may promote a registration, but reload/re-import must never demote one.
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE automation_tools SET external_id = ?, name = ?, description = ?, target_type = ?, "
        "registration_path = ?, state_path = ?, ssh_host = ?, working_directory = ?, "
        "builtin = MAX(builtin, ?) WHERE id = ?"));
    query.addBindValue(text(tool.externalId));
    query.addBindValue(text(tool.name));
    query.addBindValue(text(tool.description));
    query.addBindValue(text(tool.targetType));
    query.addBindValue(text(tool.registrationPath));
    query.addBindValue(text(tool.statePath));
    query.addBindValue(text(tool.sshHost));
    query.addBindValue(text(tool.workingDirectory));
    query.addBindValue(tool.builtin ? 1 : 0);
    query.addBindValue(tool.id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::setToolGroup(int toolId, int groupId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("UPDATE automation_tools SET group_id = NULLIF(?, 0) WHERE id = ?"));
    query.addBindValue(groupId);
    query.addBindValue(toolId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}

bool AppDatabase::deleteAutomationTool(int id, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("DELETE FROM automation_tools WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}


bool AppDatabase::updateToolRefreshSettings(int id,
                                               bool enabled,
                                               const QString &mode,
                                               int intervalSeconds,
                                               const QString &dailyTime,
                                               QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "UPDATE automation_tools SET refresh_enabled = ?, refresh_mode = ?, "
        "refresh_interval_seconds = ?, daily_refresh_time = ? WHERE id = ?"));
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(mode.isEmpty() ? QStringLiteral("interval") : mode);
    query.addBindValue(qMax(60, intervalSeconds));
    query.addBindValue(dailyTime.isEmpty() ? QStringLiteral("08:00") : dailyTime);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}


QList<AppDatabase::ToolCommand> AppDatabase::toolCommands(int toolId, QString *errorMessage) const
{
    QList<ToolCommand> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "SELECT id, tool_id, name, description, executable, arguments, parameters, interactive, high_risk, arguments_json "
        "FROM tool_commands WHERE tool_id = ? ORDER BY id"));
    query.addBindValue(toolId);
    if (!query.exec()) {
        setError(errorMessage, query);
        return result;
    }
    while (query.next()) {
        ToolCommand command;
        command.id = query.value(0).toInt();
        command.toolId = query.value(1).toInt();
        command.name = query.value(2).toString();
        command.description = query.value(3).toString();
        command.executable = query.value(4).toString();
        command.arguments = query.value(5).toString().split(QChar('\x1f'), Qt::SkipEmptyParts);
        // New registrations preserve empty strings and separator characters. Old
        // rows retain the legacy representation until their manifest is reloaded.
        const auto argumentsJson = QJsonDocument::fromJson(query.value(9).toString().toUtf8());
        if (argumentsJson.isArray()) {
            command.arguments.clear();
            for (const auto &argument : argumentsJson.array()) command.arguments.append(argument.toString());
        }
        command.parameters = QJsonDocument::fromJson(query.value(6).toString().toUtf8()).array();
        command.interactive = query.value(7).toInt() != 0;
        command.highRisk = query.value(8).toInt() != 0;
        result.append(command);
    }
    return result;
}

bool AppDatabase::addToolCommand(const ToolCommand &command, int *commandId, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO tool_commands(tool_id, name, description, executable, arguments, parameters, "
        "interactive, high_risk, arguments_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(command.toolId);
    query.addBindValue(text(command.name));
    query.addBindValue(text(command.description));
    query.addBindValue(text(command.executable));
    query.addBindValue(text(command.arguments.join(QChar('\x1f'))));
    query.addBindValue(command.parameters.isEmpty()
                           ? QStringLiteral("")
                           : QString::fromUtf8(QJsonDocument(command.parameters).toJson(QJsonDocument::Compact)));
    query.addBindValue(command.interactive ? 1 : 0);
    query.addBindValue(command.highRisk ? 1 : 0);
    query.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(command.arguments)).toJson(QJsonDocument::Compact)));
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    if (commandId != nullptr) {
        *commandId = query.lastInsertId().toInt();
    }
    return true;
}

bool AppDatabase::replaceToolCommands(int toolId, const QList<ToolCommand> &commands, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlDatabase database = QSqlDatabase::database(connectionName_);
    if (!database.transaction()) {
        return fail(errorMessage, database.lastError().text());
    }
    if (!replaceToolCommandsInTransaction(toolId, commands, errorMessage)) {
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        const QString error = database.lastError().text();
        database.rollback();
        return fail(errorMessage, error);
    }
    return true;
}

bool AppDatabase::saveAutomationTool(const AutomationTool &tool, const QList<ToolCommand> &commands,
                                     int *toolId, QString *errorMessage)
{
    if (!isOpen()) return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    auto database = QSqlDatabase::database(connectionName_);
    if (!database.transaction()) return fail(errorMessage, database.lastError().text());
    int id = tool.id;
    if (id > 0) {
        QSqlQuery exists(database);
        exists.prepare(QStringLiteral("SELECT id FROM automation_tools WHERE id = ?"));
        exists.addBindValue(id);
        if (!exists.exec() || !exists.next()) {
            const QString error = exists.lastError().isValid() ? exists.lastError().text()
                                                              : QStringLiteral("工具注册已不存在。");
            database.rollback();
            return fail(errorMessage, error);
        }
    }
    const bool saved = id > 0 ? updateAutomationTool(tool, errorMessage)
                              : addAutomationTool(tool, &id, errorMessage);
    if (!saved || !replaceToolCommandsInTransaction(id, commands, errorMessage)) {
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        const QString error = database.lastError().text();
        database.rollback();
        return fail(errorMessage, error);
    }
    if (toolId) *toolId = id;
    return true;
}

bool AppDatabase::replaceToolCommandsInTransaction(int toolId, const QList<ToolCommand> &commands,
                                                   QString *errorMessage)
{
    auto database = QSqlDatabase::database(connectionName_);
    QSqlQuery clear(database);
    clear.prepare(QStringLiteral("DELETE FROM tool_commands WHERE tool_id = ?"));
    clear.addBindValue(toolId);
    if (!clear.exec()) {
        setError(errorMessage, clear);
        return false;
    }
    for (ToolCommand command : commands) {
        command.toolId = toolId;
        if (!addToolCommand(command, nullptr, errorMessage)) {
            return false;
        }
    }
    return true;
}

QString AppDatabase::setting(const QString &key, const QString &fallback) const
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

bool AppDatabase::setSetting(const QString &key, const QString &value, QString *errorMessage)
{
    if (!isOpen()) {
        return fail(errorMessage, QStringLiteral("数据库尚未打开。"));
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral(
        "INSERT INTO app_settings(key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        setError(errorMessage, query);
        return false;
    }
    return true;
}
