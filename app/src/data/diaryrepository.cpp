#include "diaryrepository.h"
#include "repositoryhelpers.h"
#include <QSqlDatabase>
#include <QSet>

using namespace RepositoryHelpers;

QList<AppDatabase::DiaryEntry> DiaryRepository::diaryEntries(const QDate &date, QString *errorMessage) const
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

bool DiaryRepository::addDiaryEntry(const QDate &date, const QString &content, QString *errorMessage)
{
    return addDiaryEntry(date, QStringLiteral(""), content, errorMessage);
}

QList<AppDatabase::DiaryEntry> DiaryRepository::allDiaryEntries(QString *errorMessage) const
{
    return diaryEntries(QDate(), errorMessage);
}

AppDatabase::DiaryEntry DiaryRepository::diaryEntry(int id, QString *errorMessage) const
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

QList<QDate> DiaryRepository::diaryDates(const QDate &from, const QDate &to, QString *errorMessage) const
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

bool DiaryRepository::addDiaryEntry(const QDate &date, const QString &title, const QString &content, QString *errorMessage, int *newId)
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
    if (newId) *newId = query.lastInsertId().toInt();
    return true;
}

bool DiaryRepository::updateDiaryEntry(int id, const QString &content, QString *errorMessage)
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

bool DiaryRepository::updateDiaryEntry(int id, const QString &title, const QString &content, QString *errorMessage)
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

bool DiaryRepository::deleteDiaryEntry(int id, QString *errorMessage)
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

QList<AppDatabase::Reminder> DiaryRepository::reminders(const QDate &date, QString *errorMessage) const
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

QList<AppDatabase::Reminder> DiaryRepository::remindersInMonth(int year,
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

QList<AppDatabase::Reminder> DiaryRepository::dueReminders(const QDateTime &now,
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

bool DiaryRepository::addReminder(const Reminder &reminder,
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

bool DiaryRepository::updateReminder(const Reminder &reminder, QString *errorMessage)
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

bool DiaryRepository::deleteReminder(int id, QString *errorMessage)
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

bool DiaryRepository::completeReminder(int id, const QDateTime &now, QString *errorMessage)
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

bool DiaryRepository::processDueReminder(int id, const QDateTime &now, QString *errorMessage)
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
