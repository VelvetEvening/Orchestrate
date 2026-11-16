#pragma once

#include "appdatabase.h"
#include "repositorybase.h"

class DiaryRepository final : public RepositoryBase
{
public:
    using RepositoryBase::RepositoryBase;
    using DiaryEntry = AppDatabase::DiaryEntry;
    using Reminder = AppDatabase::Reminder;

    QList<DiaryEntry> diaryEntries(const QDate &date, QString *errorMessage = nullptr) const;
    QList<DiaryEntry> allDiaryEntries(QString *errorMessage = nullptr) const;
    DiaryEntry diaryEntry(int id, QString *errorMessage = nullptr) const;
    QList<QDate> diaryDates(const QDate &from, const QDate &to, QString *errorMessage = nullptr) const;
    bool addDiaryEntry(const QDate &date, const QString &content, QString *errorMessage = nullptr);
    bool addDiaryEntry(const QDate &date, const QString &title, const QString &content, QString *errorMessage = nullptr, int *newId = nullptr);
    bool updateDiaryEntry(int id, const QString &content, QString *errorMessage = nullptr);
    bool updateDiaryEntry(int id, const QString &title, const QString &content, QString *errorMessage = nullptr);
    bool deleteDiaryEntry(int id, QString *errorMessage = nullptr);

    QList<Reminder> reminders(const QDate &date, QString *errorMessage = nullptr) const;
    QList<Reminder> remindersInMonth(int year, int month, QString *errorMessage = nullptr) const;
    QList<Reminder> dueReminders(const QDateTime &now, QString *errorMessage = nullptr) const;
    bool addReminder(const Reminder &reminder,
                     int *reminderId = nullptr,
                     QString *errorMessage = nullptr);
    bool updateReminder(const Reminder &reminder, QString *errorMessage = nullptr);
    bool deleteReminder(int id, QString *errorMessage = nullptr);
    bool completeReminder(int id, const QDateTime &now, QString *errorMessage = nullptr);
    bool processDueReminder(int id, const QDateTime &now, QString *errorMessage = nullptr);
};
